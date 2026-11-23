#include <Kotonoha/Filesystem/Filesystem.hpp>

#include <Kotonoha/Filesystem/Path.hpp>
#include <Kotonoha/Formats/BlockPcm.hpp>
#include <SummerRadish/SummerAssets.hpp>
#include <algorithm>
#include <cstring>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
namespace Kotonoha::Providers {
using namespace SummerRadish;
namespace {
std::shared_ptr<Kotonoha::Filesystem::Filesystem>
mountSummer(std::filesystem::path root, std::filesystem::path overlay) {
  // A PACK subfolder is a legacy CLI input: mount its parent so
  // movies and sibling sources remain visible as virtual files.
  std::error_code ec;
  if (std::filesystem::is_directory(root, ec) &&
      (canonical(root.filename().string()) == "PACK" ||
       canonical(root.filename().string()) == "PACKS"))
    root = root.parent_path();
  auto source = Kotonoha::Filesystem::Filesystem::mount(root.string());
  if (!overlay.empty())
    source->addOverlay(overlay.string());
  return source;
}
} // namespace
SummerAssets::SummerAssets(std::filesystem::path root,
                           std::filesystem::path overlay)
    : SummerAssets(mountSummer(root, overlay)) {
  // The game still uses a physical media path in legacy MediaPlayer calls.
  // Package sources themselves are already opened through the V2 filesystem.
  if (source_->exists("DISC1/PACK") &&
      source_->stat("DISC1/PACK").type ==
          Kotonoha::Filesystem::FileType::Directory) {
    discDirectory = std::filesystem::absolute(root) / "DISC1";
  } else if (source_->exists("PACK") &&
             source_->stat("PACK").type ==
                 Kotonoha::Filesystem::FileType::Directory) {
    discDirectory = std::filesystem::absolute(root);
  } else {
    discDirectory = std::filesystem::absolute(root).parent_path();
  }
}

SummerAssets::SummerAssets(
    std::shared_ptr<Kotonoha::Filesystem::Filesystem> mounted)
    : source_(std::move(mounted)) {
  if (!source_)
    throw std::invalid_argument("Summer CMP mount is missing");
  std::string packRoot;
  if (source_->exists("Personal/MISSEACHOTHER/PACKS") &&
      source_->stat("Personal/MISSEACHOTHER/PACKS").type ==
          Kotonoha::Filesystem::FileType::Directory)
    packRoot = "Personal/MISSEACHOTHER/PACKS";
  else if (source_->exists("Personal/MISSEACHOTHER/PACK") &&
           source_->stat("Personal/MISSEACHOTHER/PACK").type ==
               Kotonoha::Filesystem::FileType::Directory)
    packRoot = "Personal/MISSEACHOTHER/PACK";
  else if (source_->exists("PACKS") &&
           source_->stat("PACKS").type ==
               Kotonoha::Filesystem::FileType::Directory)
    packRoot = "PACKS";
  else if (source_->exists("DISC1/PACK") &&
           source_->stat("DISC1/PACK").type ==
               Kotonoha::Filesystem::FileType::Directory)
    packRoot = "DISC1/PACK";
  else if (source_->exists("PACK") &&
           source_->stat("PACK").type ==
               Kotonoha::Filesystem::FileType::Directory)
    packRoot = "PACK";

  const bool meoLayout = packRoot.find("Personal/MISSEACHOTHER") == 0 ||
                         packRoot == "PACKS" || source_->exists("MEO.exe") ||
                         source_->exists("gEngine.gesl");
  meoLayout_ = meoLayout;

  std::vector<std::string> packRoots;
  packRoots.push_back(packRoot);
  if (packRoot == "DISC1/PACK" && source_->exists("DISC2/PACK") &&
      source_->stat("DISC2/PACK").type ==
          Kotonoha::Filesystem::FileType::Directory) {
    packRoots.push_back("DISC2/PACK");
  }

  std::vector<Kotonoha::Filesystem::FileInfo> files;
  for (const auto &dir : packRoots) {
    auto listed = source_->list(dir);
    files.insert(files.end(), listed.begin(), listed.end());
  }

  // Detect Imouto de Ikou! 8-pack layout (BGDATA/CHRDATA/EVDATA/SYSDAT, no INI)
  {
    bool hasBgData = false, hasSysDat = false, hasIni = false;
    for (const auto &file : files) {
      if (file.type != Kotonoha::Filesystem::FileType::Regular)
        continue;
      const std::string upperName =
          canonical(std::filesystem::path(file.path).filename().string());
      if (upperName == "BGDATA.CMP")
        hasBgData = true;
      else if (upperName == "SYSDAT.CMP")
        hasSysDat = true;
      else if (upperName == "INI.CMP")
        hasIni = true;
    }
    if (hasBgData && hasSysDat && !hasIni)
      imoutoLayout_ = true;
  }

  // Match FileSystem.mod @ 0x10003801: probe numbered patch packs
  // TYPE099.CMP .. TYPE000.CMP before the base TYPE.CMP, silently skipping
  // invalid non-CMP patch files (e.g. Lost M's 25 KB UPX PE VOICE000.cmp).
  auto parsePatchIndex = [](const std::string &upperFile,
                            const std::string &type) -> int {
    const std::string ext = ".CMP";
    if (upperFile.size() != type.size() + 3 + ext.size())
      return -1;
    if (upperFile.compare(0, type.size(), type) != 0)
      return -1;
    if (upperFile.compare(upperFile.size() - ext.size(), ext.size(), ext) != 0)
      return -1;
    int idx = 0;
    for (size_t i = 0; i < 3; ++i) {
      char ch = upperFile[type.size() + i];
      if (ch < '0' || ch > '9')
        return -1;
      idx = idx * 10 + (ch - '0');
    }
    return idx;
  };

  std::vector<std::string> packTypes;
  if (imoutoLayout_) {
    packTypes = {"BGDATA", "CHRDATA", "EVDATA",  "SCRIPT",
                 "SEDATA", "SYSDAT",  "BGMDATA", "VDATA"};
  } else {
    packTypes = {"INI", "SCRIPT", "GRAPHIC", "SOUND", "VOICE"};
  }

  for (const auto &typeStr : packTypes) {
    const std::string desired = typeStr + ".CMP";
    std::vector<std::pair<int, std::string>> patches;
    std::string basePath;

    for (const auto &file : files) {
      if (file.type != Kotonoha::Filesystem::FileType::Regular)
        continue;
      const std::string upperName =
          canonical(std::filesystem::path(file.path).filename().string());
      if (upperName == desired) {
        if (basePath.empty())
          basePath = file.path;
      } else if (int patchIdx = parsePatchIndex(upperName, typeStr);
                 patchIdx >= 0) {
        patches.emplace_back(patchIdx, file.path);
      }
    }

    std::sort(patches.begin(), patches.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });

    std::vector<std::unique_ptr<Pack>> stack;
    for (const auto &[patchIdx, patchPath] : patches) {
      (void)patchIdx;
      try {
        auto pack = std::make_unique<Pack>(source_, patchPath);
        if (pack->keyProfile() == CmpKeyProfile::LostM)
          lostMLayout_ = true;
        stack.push_back(std::move(pack));
      } catch (...) {
        // Native FileSystem.mod silently ignores non-CMP patch files.
      }
    }

    if (!basePath.empty()) {
      auto pack = std::make_unique<Pack>(source_, basePath);
      if (pack->keyProfile() == CmpKeyProfile::LostM)
        lostMLayout_ = true;
      stack.push_back(std::move(pack));
    }

    if (stack.empty()) {
      if (typeStr == "VOICE" && meoLayout)
        continue;
      throw std::runtime_error("Missing " + desired + " in virtual source");
    }
    packs.emplace(typeStr, std::move(stack));
  }
}
std::filesystem::path
SummerAssets::moviePath(const std::string &filename) const {
  const std::filesystem::path requested(filename);
  if (requested.empty() || requested.is_absolute())
    throw std::runtime_error("Invalid Summer movie filename: " + filename);

  const auto findMovie =
      [](const std::filesystem::path &path) -> std::filesystem::path {
    const auto directory = path.parent_path();
    if (!std::filesystem::is_directory(directory))
      return {};
    const auto wanted = canonical(path.filename().string());
    for (const auto &entry : std::filesystem::directory_iterator(directory))
      if (entry.is_regular_file() &&
          canonical(entry.path().filename().string()) == wanted)
        return entry.path();
    return {};
  };

  // 1. Direct attempt
  auto target = (discDirectory / requested).lexically_normal();
  if (auto found = findMovie(target); !found.empty())
    return found;

  // 2. If the candidate starts with the name of the discDirectory, strip it.
  // e.g. discDirectory is ".../DISC1", filename is "DISC1/movie.mpg"
  auto discName = discDirectory.filename();
  auto it = requested.begin();
  if (it != requested.end() && it->string() == discName.string()) {
    std::filesystem::path subPath;
    auto next_it = std::next(it);
    for (; next_it != requested.end(); ++next_it) {
      subPath /= *next_it;
    }
    target = (discDirectory / subPath).lexically_normal();
    if (auto found = findMovie(target); !found.empty())
      return found;
  }

  // The first disc calls its opening summer_op.mpg while some scripts ask
  // for opening.mpg. Resolve this known edition alias in the same directory.
  if (canonical(target.filename().string()) == "OPENING.MPG") {
    target.replace_filename("summer_op.mpg");
    if (auto found = findMovie(target); !found.empty())
      return found;
  }

  throw std::runtime_error("Missing Summer movie: " + filename);
}
std::unique_ptr<Kotonoha::Filesystem::FileHandle>
SummerAssets::movieHandle(const std::string &filename) const {
  const auto name = Kotonoha::Filesystem::normalizePath(filename);
  std::vector<std::string> candidates;
  const bool discLayout = source_->exists("DISC1/PACK");
  const auto prefix = discLayout ? std::string("DISC1/") : std::string();
  candidates.push_back(name);
  if (name.size() >= 6 && canonical(name.substr(0, 6)) == "DISC1/")
    candidates.push_back(name.substr(6));
  else if (!prefix.empty())
    candidates.push_back(prefix + name);

  // Keep the known release alias used by the original moviePath().
  for (const auto &candidate : std::vector<std::string>(candidates)) {
    if (canonical(std::filesystem::path(candidate).filename().string()) ==
        "OPENING.MPG") {
      auto path = std::filesystem::path(candidate);
      path.replace_filename("SUMMER_OP.MPG");
      candidates.push_back(path.generic_string());
    }
  }
  for (const auto &candidate : candidates) {
    if (source_->exists(candidate) &&
        source_->stat(candidate).type ==
            Kotonoha::Filesystem::FileType::Regular)
      return source_->open(candidate);
  }
  throw std::runtime_error("Missing Summer virtual movie: " + filename);
}

bool SummerAssets::containsAsset(const std::string &id) const {
  auto s = canonical(id);
  auto slash = s.find('/');
  if (slash == s.npos)
    return false;
  auto packIt = packs.find(s.substr(0, slash));
  if (packIt == packs.end())
    return false;
  const auto name = s.substr(slash + 1);
  for (const auto &pack : packIt->second) {
    if (pack->contains(name))
      return true;
    for (const auto &[entryName, entry] : pack->entries()) {
      (void)entry;
      auto stemName = entryName;
      const auto dot = stemName.find_last_of('.');
      if (dot != std::string::npos)
        stemName.resize(dot);
      if (canonical(stemName) == canonical(name))
        return true;
    }
  }
  return false;
}
Bytes SummerAssets::readAsset(const std::string &id) const {
  auto s = canonical(id);
  auto slash = s.find('/');
  auto packIt = packs.find(s.substr(0, slash));
  if (slash == s.npos || packIt == packs.end())
    throw std::runtime_error("Unknown Summer asset: " + id);
  const auto requested = s.substr(slash + 1);
  for (const auto &pack : packIt->second) {
    if (pack->contains(requested))
      return pack->read(requested);
    for (const auto &[entryName, entry] : pack->entries()) {
      (void)entry;
      if (canonical(entryName) == canonical(requested))
        return pack->read(entryName);
    }
  }
  throw std::runtime_error("Unknown Summer asset: " + id);
}

std::optional<Bytes> SummerAssets::tryReadAsset(const std::string &id) const {
  try {
    return readAsset(id);
  } catch (...) {
    return std::nullopt;
  }
}
std::vector<std::string>
SummerAssets::listAsset(const std::string &prefix) const {
  std::vector<std::string> result;
  std::set<std::string> seen;
  auto search = canonical(prefix);
  for (auto &[type, stack] : packs)
    for (const auto &pack : stack)
      for (auto &[n, e] : pack->entries()) {
        (void)e;
        auto id = type + "/" + n;
        if (id.compare(0, search.size(), search) == 0 && seen.insert(id).second)
          result.push_back(id);
      }
  return result;
}
Image SummerAssets::picture(const std::string &id) const {
  return decodeImage(read(id), id.substr(id.find_last_of('.') + 1));
}
Kotonoha::AudioClip SummerAssets::loadAudioAsset(const std::string &id) const {
  const auto name = canonical(id);
  if (meoLayout_ && name.size() >= 4 &&
      name.compare(name.size() - 4, 4, ".BGM") == 0) {
    const auto dec = Kotonoha::Formats::BlockPcm::decodeBgm(readAsset(id));
    AudioClip clip;
    clip.spec = {SDL_AUDIO_S16, 2, 44100};
    clip.data.resize(dec.pcm.samples.size() * sizeof(int16_t));
    if (!dec.pcm.samples.empty())
      std::memcpy(clip.data.data(), dec.pcm.samples.data(), clip.data.size());
    return clip;
  }
  if (meoLayout_ && name.size() >= 4 &&
      name.compare(name.size() - 4, 4, ".SND") == 0) {
    const auto pcm = Kotonoha::Formats::BlockPcm::decodeSnd(readAsset(id));
    AudioClip clip;
    clip.spec = {SDL_AUDIO_S16, 2, 44100};
    clip.data.resize(pcm.samples.size() * sizeof(int16_t));
    if (!pcm.samples.empty())
      std::memcpy(clip.data.data(), pcm.samples.data(), clip.data.size());
    return clip;
  }
  return AssetProvider::loadAudioAsset(id);
}
Kotonoha::SurfacePtr
SummerAssets::loadSurfaceAsset(const std::string &id) const {
  auto img = picture(id);
  Kotonoha::SurfacePtr s(
      SDL_CreateSurface(img.width, img.height, SDL_PIXELFORMAT_RGBA32));
  if (!s)
    throw std::runtime_error(SDL_GetError());
  for (int y = 0; y < img.height; ++y)
    std::memcpy(static_cast<uint8_t *>(s->pixels) + y * s->pitch,
                img.rgba.data() + size_t(y) * img.width * 4, img.width * 4);
  return s;
}

} // namespace Kotonoha::Providers
