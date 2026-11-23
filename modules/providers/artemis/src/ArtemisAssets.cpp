// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Artemis/ArtemisAssets.hpp>

#include <Kotonoha/Filesystem/FileHandle.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace Artemis {
namespace {

class VfsFileStream final : public Kotonoha::AssetStream {
public:
  explicit VfsFileStream(
      std::unique_ptr<Kotonoha::Filesystem::FileHandle> handle)
      : handle_(std::move(handle)) {
    if (handle_)
      size_ = static_cast<std::uint64_t>(
          std::max<std::int64_t>(0, handle_->size()));
  }

  std::uint64_t size() const override { return size_; }
  std::uint64_t tell() const override { return pos_; }

  std::size_t read(Uint8 *buffer, std::size_t count) override {
    if (!handle_ || !buffer || count == 0 || pos_ >= size_)
      return 0;
    const auto got = handle_->read(buffer, count);
    if (got <= 0)
      return 0;
    pos_ += static_cast<std::uint64_t>(got);
    return static_cast<std::size_t>(got);
  }

  bool seek(std::uint64_t position) override {
    if (!handle_ || position > size_)
      return false;
    const auto res = handle_->seek(static_cast<std::int64_t>(position),
                                   Kotonoha::Filesystem::SeekOrigin::Begin);
    if (res < 0)
      return false;
    pos_ = static_cast<std::uint64_t>(res);
    return true;
  }

private:
  std::unique_ptr<Kotonoha::Filesystem::FileHandle> handle_;
  std::uint64_t size_ = 0;
  std::uint64_t pos_ = 0;
};

struct PfsFileOrder {
  std::string vfsPath;
  std::string baseNameLower;
  int patchNumber = -1; // -1 for base .pfs, 0..999 for .pfs.NNN
};

bool parsePfsFilename(std::string_view path, PfsFileOrder &out) {
  const auto slash = path.find_last_of("/\\");
  const std::string_view fname =
      (slash == std::string_view::npos) ? path : path.substr(slash + 1);
  const std::string lower = PfsArchive::toLowerAscii(fname);

  if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".pfs") == 0) {
    out.vfsPath = std::string(path);
    out.baseNameLower = lower.substr(0, lower.size() - 4);
    out.patchNumber = -1;
    return true;
  }

  const auto pos = lower.rfind(".pfs.");
  if (pos != std::string::npos && pos + 5 < lower.size()) {
    const std::string_view suffix = std::string_view(lower).substr(pos + 5);
    bool allDigits = !suffix.empty();
    for (char c : suffix) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        allDigits = false;
        break;
      }
    }
    if (allDigits) {
      out.vfsPath = std::string(path);
      out.baseNameLower = lower.substr(0, pos);
      out.patchNumber = std::stoi(std::string(suffix));
      return true;
    }
  }
  return false;
}

std::string trimAscii(std::string_view s) {
  std::size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
    ++b;
  std::size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
    --e;
  return std::string(s.substr(b, e - b));
}

bool endsWithCI(std::string_view text, std::string_view suffix) {
  if (text.size() < suffix.size())
    return false;
  return PfsArchive::toLowerAscii(text.substr(text.size() - suffix.size())) ==
         PfsArchive::toLowerAscii(suffix);
}

} // namespace

ArtemisAssets::ArtemisAssets(std::string_view rootDir,
                             std::filesystem::path overlayDir) {
  if (!overlayDir.empty())
    setOverlay(overlayDir);
  if (!rootDir.empty())
    setRootDirectory(rootDir);
}

void ArtemisAssets::setRootDirectory(std::string_view rootDir) {
  rootDirectory_ = std::filesystem::path(std::string(rootDir));
  archives_.clear();
  mergedIndex_.clear();
  systemConfig_ = SystemConfig{};

  if (rootDir.empty()) {
    vfs_.reset();
    return;
  }

  vfs_ = Kotonoha::Filesystem::Filesystem::mount(std::string(rootDir));
  if (!vfs_)
    return;

  // 1. Discover and mount all .pfs and .pfs.000..999 archives in order.
  std::vector<PfsFileOrder> pfsFiles;
  try {
    for (const auto &entry : vfs_->list("")) {
      if (entry.type != Kotonoha::Filesystem::FileType::Regular)
        continue;
      PfsFileOrder info;
      if (parsePfsFilename(entry.path, info))
        pfsFiles.push_back(std::move(info));
    }
  } catch (...) {
  }

  std::sort(pfsFiles.begin(), pfsFiles.end(),
            [](const PfsFileOrder &a, const PfsFileOrder &b) {
              if (a.baseNameLower != b.baseNameLower)
                return a.baseNameLower < b.baseNameLower;
              return a.patchNumber < b.patchNumber;
            });

  for (const auto &pfs : pfsFiles) {
    try {
      auto handle = vfs_->open(pfs.vfsPath);
      if (auto archive = PfsArchive::open(std::move(handle), pfs.vfsPath)) {
        mountArchive(std::move(archive));
      }
    } catch (...) {
    }
  }

  // 2. Index loose files on disk (highest priority, overriding PFS entries).
  indexLooseDirectory("");

  // 3. Parse system.ini if present.
  parseSystemIni();
}

void ArtemisAssets::mountArchive(std::shared_ptr<PfsArchive> archive) {
  if (!archive)
    return;
  for (const auto &entry : archive->entries()) {
    const std::string lower = PfsArchive::toLowerAscii(entry.path);
    EntryRef ref;
    ref.canonicalPath = entry.path;
    ref.archive = archive;
    ref.pfsEntry = entry;
    mergedIndex_[lower] = std::move(ref);
  }
  archives_.push_back(std::move(archive));
  if (systemConfig_.width == 1280 && systemConfig_.height == 720 &&
      systemConfig_.title.empty()) {
    parseSystemIni();
  }
}

void ArtemisAssets::indexLooseDirectory(const std::string &dirPath) {
  if (!vfs_)
    return;
  std::vector<Kotonoha::Filesystem::FileInfo> entries;
  try {
    entries = vfs_->list(dirPath);
  } catch (...) {
    return;
  }

  for (const auto &entry : entries) {
    const std::string norm = PfsArchive::normalizePath(entry.path);
    if (norm.empty())
      continue;
    if (entry.type == Kotonoha::Filesystem::FileType::Directory) {
      const std::string lower = PfsArchive::toLowerAscii(norm);
      if (lower == "savedata" || lower.rfind("savedata/", 0) == 0)
        continue;
      indexLooseDirectory(norm);
      continue;
    }
    if (entry.type != Kotonoha::Filesystem::FileType::Regular)
      continue;

    PfsFileOrder dummy;
    if (parsePfsFilename(norm, dummy))
      continue;
    if (endsWithCI(norm, ".exe") || endsWithCI(norm, ".dll"))
      continue;

    const std::string lower = PfsArchive::toLowerAscii(norm);
    EntryRef ref;
    ref.canonicalPath = norm;
    ref.archive = nullptr;
    mergedIndex_[lower] = std::move(ref);
  }
}

void ArtemisAssets::parseSystemIni() {
  const std::string iniPath = resolveActualPath("system.ini");
  if (iniPath.empty())
    return;

  std::vector<Uint8> bytes;
  try {
    bytes = readAsset(iniPath);
  } catch (...) {
    return;
  }

  std::string text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  std::istringstream iss(text);
  std::string line;
  std::string section;

  while (std::getline(iss, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    const std::string trimmed = trimAscii(line);
    if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
      continue;
    if (trimmed.front() == '[' && trimmed.back() == ']') {
      section = PfsArchive::toLowerAscii(
          trimAscii(trimmed.substr(1, trimmed.size() - 2)));
      continue;
    }
    const auto eq = trimmed.find('=');
    if (eq == std::string::npos)
      continue;
    const std::string key =
        PfsArchive::toLowerAscii(trimAscii(trimmed.substr(0, eq)));
    std::string val = trimAscii(trimmed.substr(eq + 1));
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
      val = val.substr(1, val.size() - 2);

    if (section.empty() || section == "windows" || section == "system") {
      if (key == "width") {
        try {
          const int w = std::stoi(val);
          if (w >= 320 && w <= 7680)
            systemConfig_.width = w;
        } catch (...) {
        }
      } else if (key == "height") {
        try {
          const int h = std::stoi(val);
          if (h >= 240 && h <= 4320)
            systemConfig_.height = h;
        } catch (...) {
        }
      } else if (key == "title" && !val.empty()) {
        systemConfig_.title = val;
      } else if (key == "boot" && !val.empty()) {
        systemConfig_.bootFile = val;
      }
    }
  }
}

std::string ArtemisAssets::expandArtemisPrefix(std::string_view path) {
  std::string p = PfsArchive::normalizePath(path);
  if (p.empty() || p[0] != ':')
    return p;

  const auto slash = p.find('/');
  const std::string tag = PfsArchive::toLowerAscii(
      slash == std::string::npos ? p : p.substr(0, slash));
  const std::string rest =
      (slash == std::string::npos) ? std::string() : p.substr(slash + 1);

  std::string base;
  if (tag == ":bg")
    base = "image/bg";
  else if (tag == ":fg")
    base = "image/fg";
  else if (tag == ":cg")
    base = "image/cg";
  else if (tag == ":ev")
    base = "image/ev";
  else if (tag == ":anime")
    base = "image/anime";
  else if (tag == ":rule")
    base = "image/rule";
  else if (tag == ":mask")
    base = "image/mask";
  else if (tag == ":bgm")
    base = "sound/bgm";
  else if (tag == ":se")
    base = "sound/se";
  else if (tag == ":vo")
    base = "sound/vo";
  else if (tag == ":sysse")
    base = "sound/sysse";
  else if (tag == ":mv" || tag == ":movie")
    base = "movie";
  else if (tag == ":script")
    base = "script";
  else
    return rest;

  return rest.empty() ? base : (base + "/" + rest);
}

std::string
ArtemisAssets::resolveActualPath(std::string_view requestedPath) const {
  if (requestedPath.empty())
    return {};
  const std::string expanded = expandArtemisPrefix(requestedPath);
  const std::string lower = PfsArchive::toLowerAscii(expanded);
  auto it = mergedIndex_.find(lower);
  if (it != mergedIndex_.end())
    return it->second.canonicalPath;
  return {};
}

std::string ArtemisAssets::resolveImagePath(std::string_view path,
                                            std::string_view file) const {
  if (file.empty())
    return {};

  std::vector<std::string> bases;
  if (!path.empty()) {
    std::string dir = expandArtemisPrefix(path);
    if (!dir.empty() && dir.back() != '/')
      dir.push_back('/');
    bases.push_back(dir + std::string(file));
  }
  bases.push_back(std::string(file));
  bases.push_back("image/bg/" + std::string(file));
  bases.push_back("image/cg/" + std::string(file));
  bases.push_back("image/fg/" + std::string(file));
  bases.push_back("image/ev/" + std::string(file));
  bases.push_back("image/anime/" + std::string(file));
  bases.push_back("image/rule/" + std::string(file));

  static constexpr const char *kExts[] = {"",     ".png", ".webp",
                                          ".jpg", ".bmp", ".ipt"};
  for (const auto &base : bases) {
    for (const char *ext : kExts) {
      const std::string hit = resolveActualPath(base + ext);
      if (!hit.empty())
        return hit;
    }
  }
  return {};
}

std::vector<std::string> ArtemisAssets::uiLanguages() const {
  std::vector<std::string> languages;
  for (const auto &[lower, ref] : mergedIndex_) {
    (void)ref;
    if (lower.rfind("pc/", 0) != 0)
      continue;
    const auto slash = lower.find('/', 3);
    if (slash == std::string::npos || slash == 3)
      continue;
    const std::string language = lower.substr(3, slash - 3);
    if (std::find(languages.begin(), languages.end(), language) ==
        languages.end())
      languages.push_back(language);
  }
  std::sort(languages.begin(), languages.end(),
            [](const std::string &a, const std::string &b) {
              if (a == "en")
                return true;
              if (b == "en")
                return false;
              return a < b;
            });
  return languages;
}

void ArtemisAssets::setUiLanguage(std::string language) {
  uiLanguage_ = PfsArchive::toLowerAscii(trimAscii(language));
  if (uiLanguage_.empty())
    uiLanguage_ = "en";
}

std::string ArtemisAssets::uiTablePath() const {
  const std::string localized =
      "system/table/list_windows_" + uiLanguage_ + ".tbl";
  if (!resolveActualPath(localized).empty())
    return localized;
  if (!resolveActualPath("system/table/list_windows.tbl").empty())
    return "system/table/list_windows.tbl";
  return {};
}

std::string
ArtemisAssets::resolveUiImagePath(std::string_view logicalFile) const {
  if (logicalFile.empty())
    return {};
  const std::string file = PfsArchive::normalizePath(logicalFile);
  if (file.empty())
    return {};

  std::vector<std::string> languages;
  for (const auto &lang : uiLanguages()) {
    if (lang == uiLanguage_)
      languages.insert(languages.begin(), lang);
    else
      languages.push_back(lang);
  }

  static constexpr const char *kExts[] = {"", ".png", ".webp", ".jpg", ".bmp"};
  for (const auto &lang : languages) {
    const std::string base = "pc/" + lang + "/" + file;
    for (const char *ext : kExts) {
      const std::string hit = resolveActualPath(base + ext);
      if (!hit.empty())
        return hit;
    }
  }
  // Some titles ship loose UI pictures outside the language tree.
  for (const char *ext : kExts) {
    const std::string hit = resolveActualPath(file + ext);
    if (!hit.empty())
      return hit;
  }
  return {};
}

std::string ArtemisAssets::resolveAudioPath(std::string_view category,
                                            std::string_view file) const {
  if (file.empty())
    return {};

  std::vector<std::string> bases;
  if (!category.empty()) {
    std::string dir = expandArtemisPrefix(category);
    if (!dir.empty() && dir.back() != '/')
      dir.push_back('/');
    bases.push_back(dir + std::string(file));
  }
  bases.push_back(std::string(file));
  bases.push_back("sound/se/" + std::string(file));
  bases.push_back("sound/vo/" + std::string(file));
  bases.push_back("sound/bgm/" + std::string(file));
  bases.push_back("sound/sysse/" + std::string(file));

  static constexpr const char *kExts[] = {"", ".ogg", ".opus", ".wav", ".mp3"};
  for (const auto &base : bases) {
    for (const char *ext : kExts) {
      const std::string hit = resolveActualPath(base + ext);
      if (!hit.empty())
        return hit;
    }
  }
  return {};
}

std::pair<std::string, std::string>
ArtemisAssets::resolveBgmPaths(std::string_view file) const {
  if (file.empty())
    return {{}, {}};

  const std::string direct = resolveAudioPath(":bgm/", file);
  if (!direct.empty())
    return {{}, direct};

  // Artemis commonly splits BGM into <name>_a.ogg (intro) and <name>_b.ogg
  // (loop).
  const std::string intro = resolveAudioPath(":bgm/", std::string(file) + "_a");
  const std::string loop = resolveAudioPath(":bgm/", std::string(file) + "_b");
  if (!intro.empty() || !loop.empty())
    return {intro, loop};

  return {{}, {}};
}

std::string ArtemisAssets::resolveMoviePath(std::string_view file) const {
  if (file.empty())
    return {};

  std::vector<std::string> bases = {
      std::string(file),
      "movie/" + std::string(file),
      "image/movie/" + std::string(file),
      "image/anime/" + std::string(file),
  };
  static constexpr const char *kExts[] = {"",      ".mp4", ".wmv", ".ogv",
                                          ".webm", ".dat", ".mpg"};
  for (const auto &base : bases) {
    for (const char *ext : kExts) {
      const std::string hit = resolveActualPath(base + ext);
      if (!hit.empty())
        return hit;
    }
  }
  return {};
}

std::string ArtemisAssets::resolveScriptPath(std::string_view file) const {
  if (file.empty())
    return {};

  auto tryName = [&](const std::string &candidate) -> std::string {
    for (const std::string &base :
         {candidate, "script/" + candidate, "scenario/" + candidate}) {
      for (const char *ext : {"", ".ast"}) {
        const std::string hit = resolveActualPath(base + ext);
        if (!hit.empty())
          return hit;
      }
    }
    return {};
  };

  if (auto hit = tryName(std::string(file)); !hit.empty())
    return hit;

  // Fallback for minor underscore differences (e.g., NUKITASHI 2 gamestart.ast
  // referencing "01_c_00" when the shipped archive contains
  // "script/01_c00.ast").
  std::string stripped;
  stripped.reserve(file.size());
  for (std::size_t i = 0; i < file.size(); ++i) {
    if (file[i] == '_' && i > 0 && i + 1 < file.size() &&
        std::isdigit(static_cast<unsigned char>(file[i + 1])) &&
        std::isalpha(static_cast<unsigned char>(file[i - 1]))) {
      continue;
    }
    stripped.push_back(file[i]);
  }
  if (stripped != file) {
    if (auto hit = tryName(stripped); !hit.empty())
      return hit;
  }

  return {};
}

std::vector<std::string> ArtemisAssets::discoverScripts() const {
  std::vector<std::string> scripts;
  for (const auto &[lower, ref] : mergedIndex_) {
    if (endsWithCI(lower, ".ast"))
      scripts.push_back(ref.canonicalPath);
  }
  std::sort(scripts.begin(), scripts.end());
  return scripts;
}

std::string ArtemisAssets::findStartScript() const {
  for (const char *candidate :
       {"script/gamestart.ast", "script/startup.ast", "script/first.ast",
        "script/01_c_00.ast", "script/01_c00.ast"}) {
    if (auto hit = resolveActualPath(candidate); !hit.empty())
      return hit;
  }
  const auto all = discoverScripts();
  for (const auto &s : all) {
    const std::string lower = PfsArchive::toLowerAscii(s);
    if (lower.find("pack.ast") == std::string::npos &&
        lower.find("brandlogo.ast") == std::string::npos) {
      return s;
    }
  }
  return all.empty() ? std::string{} : all.front();
}

bool ArtemisAssets::containsAsset(const std::string &path) const {
  return !resolveActualPath(path).empty();
}

std::vector<Uint8> ArtemisAssets::readAsset(const std::string &path) const {
  const std::string resolved = resolveActualPath(path);
  if (resolved.empty())
    throw std::runtime_error("Artemis asset not found: " + path);

  const std::string lower = PfsArchive::toLowerAscii(resolved);
  const auto &ref = mergedIndex_.at(lower);
  if (ref.archive) {
    return ref.archive->readEntry(ref.pfsEntry);
  }

  if (!vfs_)
    throw std::runtime_error("Artemis VFS not mounted: " + path);
  auto handle = vfs_->open(ref.canonicalPath);
  if (!handle)
    throw std::runtime_error("Artemis failed to open loose asset: " + path);

  const std::int64_t sz = handle->size();
  std::vector<Uint8> out(sz > 0 ? static_cast<std::size_t>(sz) : 0);
  std::size_t pos = 0;
  while (pos < out.size()) {
    const auto got = handle->read(out.data() + pos, out.size() - pos);
    if (got <= 0)
      break;
    pos += static_cast<std::size_t>(got);
  }
  out.resize(pos);
  return out;
}

std::vector<std::string>
ArtemisAssets::listAsset(const std::string &prefix) const {
  const std::string normPrefix =
      PfsArchive::toLowerAscii(expandArtemisPrefix(prefix));
  std::vector<std::string> out;
  for (const auto &[lower, ref] : mergedIndex_) {
    if (normPrefix.empty() || lower.rfind(normPrefix, 0) == 0)
      out.push_back(ref.canonicalPath);
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::string ArtemisAssets::localPathAsset(const std::string &path) const {
  const std::string resolved = resolveActualPath(path);
  if (resolved.empty() || rootDirectory_.empty())
    return {};
  const std::string lower = PfsArchive::toLowerAscii(resolved);
  const auto &ref = mergedIndex_.at(lower);
  if (ref.archive)
    return {}; // Packaged inside PFS; use openStream() or read().
  return (rootDirectory_ / ref.canonicalPath).string();
}

std::unique_ptr<Kotonoha::AssetStream>
ArtemisAssets::openStream(const std::string &path) const {
  const std::string resolved = resolveActualPath(path);
  if (resolved.empty())
    return nullptr;

  const std::string lower = PfsArchive::toLowerAscii(resolved);
  const auto &ref = mergedIndex_.at(lower);
  if (ref.archive)
    return ref.archive->openStream(ref.pfsEntry);

  if (!vfs_)
    return nullptr;
  auto handle = vfs_->open(ref.canonicalPath);
  if (!handle)
    return nullptr;
  return std::make_unique<VfsFileStream>(std::move(handle));
}

} // namespace Artemis
