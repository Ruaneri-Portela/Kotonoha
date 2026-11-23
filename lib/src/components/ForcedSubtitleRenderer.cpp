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
#include <utility>

namespace Kotonoha {
namespace {

std::string asciiLower(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

} // namespace

ForcedSubtitleRenderer::ForcedSubtitleRenderer(Engine &engine)
    : SubtitleScene(engine) {
  for (const auto &candidate : FileResolver::candidates("forced_subtitles")) {
    if (std::filesystem::is_directory(candidate)) {
      sourceDirectory_ = std::filesystem::canonical(candidate);
      break;
    }
  }
  const auto size = engine.GetRuntime().sceneSize(*this);
  setLogicalSize(static_cast<int>(std::lround(size.x)),
                 static_cast<int>(std::lround(size.y)));
}
ForcedSubtitleRenderer::~ForcedSubtitleRenderer() = default;

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
  std::map<std::string, std::filesystem::path> cache;
  if (sourceDirectory_.empty() || !std::filesystem::exists(sourceDirectory_)) {
    cacheBuilt = true;
    subtitleCache.clear();
    return;
  }
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(sourceDirectory_)) {
    if (!entry.is_regular_file() ||
        asciiLower(entry.path().extension().string()) != ".ass")
      continue;
    const auto key = asciiLower(entry.path().filename().string());
    if (!cache.emplace(key, entry.path()).second)
      throw std::runtime_error("Duplicate forced subtitle filename: " +
                               entry.path().filename().string());
  }
  subtitleCache = std::move(cache);
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
    std::string nodeOnlyId = nodeId;
    if (const auto separator = nodeOnlyId.find(':');
        separator != std::string::npos)
      nodeOnlyId.resize(separator);
    auto findPath =
        [&](const std::string &id) -> std::optional<std::filesystem::path> {
      if (const auto dumped = dumpedSubtitles.find(id);
          dumped != dumpedSubtitles.end())
        return dumped->second;
      if (const auto found = subtitleCache.find(asciiLower(fileName(id)));
          found != subtitleCache.end())
        return found->second;
      return std::nullopt;
    };
    auto selected = findPath(nodeId);
    const bool eventSpecific = selected.has_value();
    if (!selected && nodeOnlyId != nodeId)
      selected = findPath(nodeOnlyId);
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
  clearForcedSubtitles();
  activeFile.clear();
  useNodeClock = false;
}

std::filesystem::path ForcedSubtitleRenderer::dumpForActiveNode() {
  const auto node = engine.activeNode();
  if (node.empty())
    throw std::logic_error("No active node/event to dump");
  const auto path =
      std::filesystem::current_path() / "forced_subtitles" / fileName(node);
  std::filesystem::create_directories(path.parent_path());
  if (std::filesystem::exists(path))
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
  dumpedSubtitles[node] = path;
  reload();
  return path;
}

} // namespace Kotonoha
