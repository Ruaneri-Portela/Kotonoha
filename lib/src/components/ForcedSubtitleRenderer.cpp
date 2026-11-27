#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/components/ForcedSubtitleRenderer.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Log.hpp>

#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace Kotonoha {
namespace {

std::string asciiLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return value;
}

// A locale must always be exactly one safe directory name. Standard BCP-47
// identifiers get canonical casing; other safe slugs (e.g. my_custom, used by
// user-selected JSON translations) retain a lowercase isolated namespace.
std::string normalizeLocale(const std::string &locale) {
  if (locale.empty())
    return {};
  if (locale.size() > 240)
    throw std::invalid_argument("Invalid forced subtitle locale");
  for (const unsigned char ch : locale) {
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') || ch == '-' || ch == '_'))
      throw std::invalid_argument("Invalid forced subtitle locale");
  }
  std::string result;
  size_t begin = 0;
  unsigned part = 0;
  bool standard = locale.find('_') == std::string::npos;
  while (begin < locale.size()) {
    const auto end = locale.find('-', begin);
    const auto length = (end == std::string::npos ? locale.size() : end) - begin;
    if (length == 0 ||
        (part == 0 ? (length < 2 || length > 3)
                   : (length < 2 || length > 8)))
      standard = false;
    bool alpha = true;
    for (size_t i = begin; i < begin + length; ++i) {
      const auto ch = static_cast<unsigned char>(locale[i]);
      const bool letter = (ch >= 'a' && ch <= 'z') ||
                          (ch >= 'A' && ch <= 'Z');
      alpha &= letter;
    }
    if (part == 0 && !alpha)
      standard = false;
    if (part != 0)
      result.push_back('-');
    for (size_t i = begin; i < begin + length; ++i) {
      const auto ch = static_cast<unsigned char>(locale[i]);
      char normalized = static_cast<char>(std::tolower(ch));
      if (standard && part != 0 && alpha && length == 2)
        normalized = static_cast<char>(std::toupper(ch));
      if (standard && part != 0 && alpha && length == 4 && i == begin)
        normalized = static_cast<char>(std::toupper(ch));
      result.push_back(normalized);
    }
    ++part;
    if (end == std::string::npos)
      return standard ? result : asciiLower(locale);
    begin = end + 1;
  }
  // Custom JSON stems may contain non-BCP-47 runs of safe separators. They
  // remain valid, isolated locale directories without path interpretation.
  return asciiLower(locale);
}

bool localeDirectory(const std::filesystem::path &path) {
  const auto name = path.filename().string();
  try {
    // Keep the pre-existing arbitrary nested legacy folders supported, while
    // excluding locale-like roots (en, ru, pt-BR, zh-cn, my_custom, etc.).
    return !normalizeLocale(name).empty() &&
           (name.size() <= 3 || name.find_first_of("-_") != std::string::npos);
  } catch (const std::invalid_argument &) {
    return false;
  }
}

std::filesystem::path findLocaleDirectory(const std::filesystem::path &root,
                                          const std::string &locale) {
  std::filesystem::path selected;
  const auto requested = asciiLower(locale);
  for (const auto &entry : std::filesystem::directory_iterator(root)) {
    if (asciiLower(entry.path().filename().string()) != requested)
      continue;
    if (!selected.empty())
      throw std::runtime_error("Ambiguous forced subtitle locale directory: " +
                               locale);
    if (entry.is_symlink())
      throw std::runtime_error("Forced subtitle locale is a symlink: " +
                               entry.path().string());
    if (entry.is_directory())
      selected = entry.path();
  }
  return selected;
}

bool safeRegularFile(const std::filesystem::path &path,
                     const std::filesystem::path &directory) {
  std::error_code error;
  if (directory.empty() || std::filesystem::is_symlink(directory, error) ||
      error)
    return false;
  const auto relativePath = path.lexically_relative(directory);
  if (relativePath.empty() || relativePath.is_absolute() ||
      *relativePath.begin() == "..")
    return false;
  auto parent = directory;
  for (const auto &part : relativePath) {
    parent /= part;
    if (std::filesystem::is_symlink(parent, error) || error)
      return false;
  }
  if (!std::filesystem::is_regular_file(
          std::filesystem::symlink_status(path, error)) || error)
    return false;
  const auto canonicalDirectory = std::filesystem::canonical(directory, error);
  if (error)
    return false;
  const auto resolved = std::filesystem::weakly_canonical(path, error);
  if (error)
    return false;
  const auto relative = resolved.lexically_relative(canonicalDirectory);
  return !relative.empty() && !relative.is_absolute() &&
         *relative.begin() != "..";
}

} // namespace

ForcedSubtitleRenderer::ForcedSubtitleRenderer(
    Engine &engine, const std::filesystem::path &explicitDirectory)
    : SubtitleScene(engine) {
  std::error_code error;
  if (!explicitDirectory.empty()) {
    if (std::filesystem::is_directory(explicitDirectory, error) && !error &&
        !std::filesystem::is_symlink(explicitDirectory, error) && !error) {
      sourceDirectory_ = std::filesystem::canonical(explicitDirectory, error);
      if (error)
        sourceDirectory_.clear();
    }
  } else {
    for (const auto &candidate : FileResolver::candidates("forced_subtitles")) {
      if (std::filesystem::is_directory(candidate) &&
          !std::filesystem::is_symlink(candidate)) {
        sourceDirectory_ = std::filesystem::canonical(candidate);
        break;
      }
    }
  }
  const auto size = engine.GetRuntime().sceneSize(*this);
  setLogicalSize(static_cast<int>(std::lround(size.x)),
                 static_cast<int>(std::lround(size.y)));
}
ForcedSubtitleRenderer::~ForcedSubtitleRenderer() = default;

void ForcedSubtitleRenderer::setLocale(const std::string &locale) {
  auto normalized = normalizeLocale(locale);
  if (locale_ == normalized)
    return;
  locale_ = std::move(normalized);
  reload();
}

std::string ForcedSubtitleRenderer::fileName(const std::string &nodeId) {
  std::string name = nodeId;
  std::replace_if(
      name.begin(), name.end(),
      [](unsigned char ch) {
        return ch < 32 ||
               std::string("<>:\"/\\|?*").find(ch) != std::string::npos;
      },
      '-');
  return name + ".ass";
}

void ForcedSubtitleRenderer::buildSubtitleCache() {
  const auto scan = [&](const std::filesystem::path &directory,
                        bool excludeLocales) {
    std::map<std::string, std::filesystem::path> cache;
    std::error_code error;
    if (directory.empty() ||
        std::filesystem::is_symlink(
            std::filesystem::symlink_status(directory, error)) ||
        error || !std::filesystem::is_directory(directory))
      return cache;
    for (std::filesystem::recursive_directory_iterator iterator(directory), end;
         iterator != end; ++iterator) {
      const auto &entry = *iterator;
      const auto status = entry.symlink_status(error);
      if (error)
        throw std::filesystem::filesystem_error(
            "Cannot inspect forced subtitle entry", entry.path(), error);
      if (std::filesystem::is_symlink(status)) {
        iterator.disable_recursion_pending();
        continue;
      }
      if (std::filesystem::is_directory(status)) {
        if (excludeLocales && entry.path().parent_path() == directory &&
            localeDirectory(entry.path()))
          iterator.disable_recursion_pending();
        continue;
      }
      if (!std::filesystem::is_regular_file(status) ||
          asciiLower(entry.path().extension().string()) != ".ass" ||
          !safeRegularFile(entry.path(), directory))
        continue;
      const auto name = entry.path().stem().string() + ".ass";
      if (!cache.emplace(name, entry.path()).second)
        throw std::runtime_error("Duplicate forced subtitle filename: " + name);
    }
    return cache;
  };
  std::map<std::string, std::filesystem::path> selected;
  std::map<std::string, std::filesystem::path> legacy;
  if (!sourceDirectory_.empty() &&
      !std::filesystem::is_symlink(sourceDirectory_)) {
    if (!locale_.empty()) {
      localeDirectory_ = findLocaleDirectory(sourceDirectory_, locale_);
      selected = scan(localeDirectory_, false);
    }
    if (locale_.empty() || locale_ == "en")
      legacy = scan(sourceDirectory_, true);
  }
  subtitleCache = std::move(selected);
  legacySubtitleCache = std::move(legacy);
  cacheBuilt = true;
}

void ForcedSubtitleRenderer::updateForActiveNode() {
  // Both the engine-wide option and native game settings must allow ASS.
  // Toggling off clears stale cues; toggling on reloads the current node.
  const auto node = engine.GetSettings().enableForcedSubtitles &&
                            engine.GetRuntime().allowForcedSubtitles()
                        ? engine.activeNode()
                        : std::string();
  loadForNode(node);
  if (node.empty())
    setPosition(0);
  else if (useNodeClock)
    setPosition(engine.activeNodeSeconds());
  else
    setPosition(engine.activeEventSeconds());
}

void ForcedSubtitleRenderer::loadForNode(const std::string &nodeId) {
  if (nodeId == lastLoadedNodeId)
    return;
  lastLoadedNodeId = nodeId;
  if (nodeId.empty()) {
    clearForcedSubtitles();
    activeFile.clear();
    useNodeClock = false;
    return;
  }
  try {
    if (!cacheBuilt)
      buildSubtitleCache();
    if (!sourceDirectory_.empty() &&
        std::filesystem::is_symlink(sourceDirectory_))
      throw std::runtime_error("Forced subtitle source is a symlink");
    std::string nodeOnlyId = nodeId;
    if (const auto separator = nodeOnlyId.find(':');
        separator != std::string::npos)
      nodeOnlyId.resize(separator);
    auto findPath = [&](const std::string &id,
                        const std::map<std::string, std::filesystem::path> &cache,
                        const std::string &dumpLocale)
        -> std::optional<std::filesystem::path> {
      if (const auto localeDumps = dumpedSubtitles.find(dumpLocale);
          localeDumps != dumpedSubtitles.end()) {
        if (const auto dumped = localeDumps->second.find(id);
            dumped != localeDumps->second.end())
          return dumped->second;
      }
      if (const auto found = cache.find(fileName(id)); found != cache.end())
        return found->second;
      return std::nullopt;
    };
    auto findForLocale = [&](const auto &cache, const std::string &dumpLocale) {
      auto selected = findPath(nodeId, cache, dumpLocale);
      const bool eventSpecific = selected.has_value();
      if (!selected && nodeOnlyId != nodeId)
        selected = findPath(nodeOnlyId, cache, dumpLocale);
      return std::make_pair(selected, eventSpecific);
    };
    auto [selected, eventSpecific] = findForLocale(subtitleCache, locale_);
    if (!selected && (locale_.empty() || locale_ == "en"))
      std::tie(selected, eventSpecific) = findForLocale(legacySubtitleCache, "");
    if (!selected) {
      clearForcedSubtitles();
      activeFile.clear();
      useNodeClock = false;
      return;
    }
    useNodeClock = !eventSpecific;
    const auto &path = *selected;
    if (path == activeFile)
      return;
    clearForcedSubtitles();
    activeFile.clear();
    const auto inSelected = !locale_.empty() &&
        safeRegularFile(path, localeDirectory_);
    const auto inLegacy = (locale_.empty() || locale_ == "en") &&
        safeRegularFile(path, sourceDirectory_);
    const auto dumped = dumpedSubtitles.find(locale_);
    const bool isDump = dumped != dumpedSubtitles.end() &&
        std::any_of(dumped->second.begin(), dumped->second.end(),
                    [&](const auto &entry) { return entry.second == path; });
    if (!(inSelected || (inLegacy && !isDump) ||
          (isDump && safeRegularFile(path, path.parent_path()))))
      throw std::runtime_error("Unsafe forced subtitle file: " + path.string());
    Kotonoha::Filesystem::InputFile input(path, std::ios::binary);
    if (!input)
      throw std::runtime_error("Cannot open " + path.string());
    std::string content(16 * 1024 * 1024 + 1, '\0');
    input.read(content.data(), static_cast<std::streamsize>(content.size()));
    const auto size = input.gcount();
    if (input.bad() || size > 16 * 1024 * 1024)
      throw std::runtime_error(
          "Cannot read forced subtitles or file exceeds 16 MiB");
    content.resize(static_cast<std::size_t>(size));
    setForcedSubtitles(std::move(content));
    activeFile = path;
  } catch (const std::exception &error) {
    clearForcedSubtitles();
    activeFile.clear();
    useNodeClock = false;
    SDL_LogError(LogMedia, "Forced subtitles for %s: %s", nodeId.c_str(),
                 error.what());
  }
}

void ForcedSubtitleRenderer::reload() {
  cacheBuilt = false;
  lastLoadedNodeId.clear();
  subtitleCache.clear();
  legacySubtitleCache.clear();
  localeDirectory_.clear();
  clearForcedSubtitles();
  activeFile.clear();
  useNodeClock = false;
}

std::filesystem::path ForcedSubtitleRenderer::dumpForActiveNode() {
  const auto node = engine.activeNode();
  if (node.empty())
    throw std::logic_error("No active node/event to dump");
  const auto root = std::filesystem::current_path() / "forced_subtitles";
  if (std::filesystem::is_symlink(root))
    throw std::runtime_error("Forced subtitle dump directory is a symlink");
  std::filesystem::create_directory(root);
  const auto directory = locale_.empty() ? root : root / locale_;
  if (std::filesystem::is_symlink(directory))
    throw std::runtime_error("Forced subtitle locale directory is a symlink");
  if (directory != root)
    std::filesystem::create_directory(directory);
  const auto path = directory / fileName(node);
  if (std::filesystem::exists(path) || std::filesystem::is_symlink(path))
    throw std::runtime_error("Subtitle file already exists: " + path.string());
  std::ostringstream ass;
  const auto size = logicalSize();
  ass << "[Script Info]\nTitle: " << path.stem().string()
      << "\nScriptType: v4.00+\nPlayResX: " << size.x
      << "\nPlayResY: " << size.y << "\nWrapStyle: 0\n\n"
      << "[V4+ Styles]\n"
      << "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
         "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
         "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
         "Alignment, MarginL, MarginR, MarginV, Encoding\n"
      << "Style: Default,Noto Sans JP,32,&H00FFF5F8,&H00FFFFFF,&H00201A2E,"
         "&H96000000,0,0,0,0,100,100,0,0,1,2.6,1.2,2,48,48,36,1\n\n"
      << "[Events]\n"
      << "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, "
         "Effect, Text\n"
      << "Comment: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,"
         "Replace Comment with Dialogue and edit the node-relative times and "
         "text.\n";
  writeTextFileAtomically(path.string(), ass.str());
  dumpedSubtitles[locale_][node] = path;
  reload();
  return path;
}

} // namespace Kotonoha
