#include <Kotonoha/componentes/ForcedSubtitleRenderer.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/Log.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace Kotonoha {

ForcedSubtitleRenderer::ForcedSubtitleRenderer(Engine &engine)
    : SubtitleScene(engine) {}
ForcedSubtitleRenderer::~ForcedSubtitleRenderer() = default;

std::string ForcedSubtitleRenderer::fileName(const std::string &nodeId) {
  std::string name = nodeId;
  std::replace_if(name.begin(), name.end(), [](unsigned char ch) {
    return ch < 32 || std::string("<>:\"/\\|?*").find(ch) != std::string::npos;
  }, '-');
  return name + ".ass";
}

void ForcedSubtitleRenderer::buildSubtitleCache() {
  std::map<std::string, std::filesystem::path> cache;
  const auto directory =
      preferredDocumentsDirectory(engine.GetRuntime().identify) /
      "forced_subtitles";
  if (!std::filesystem::exists(directory)) {
    cacheBuilt = true;
    subtitleCache.clear();
    return;
  }
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(directory)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".ass")
      continue;
    const auto name = entry.path().filename().string();
    if (!cache.emplace(name, entry.path()).second)
      throw std::runtime_error("Duplicate forced subtitle filename: " + name);
  }
  subtitleCache = std::move(cache);
  cacheBuilt = true;
}

void ForcedSubtitleRenderer::updateForActiveNode() {
  const auto node = engine.GetSettings().enableForcedSubtitles
                        ? engine.activeNode()
                        : std::string();
  loadForNode(node);
  setPosition(node.empty() ? 0 : engine.activeNodeSeconds());
}

void ForcedSubtitleRenderer::loadForNode(const std::string &nodeId) {
  if (nodeId == lastLoadedNodeId)
    return;
  lastLoadedNodeId = nodeId;
  clear();
  if (nodeId.empty())
    return;
  try {
    if (!cacheBuilt)
      buildSubtitleCache();
    const auto found = subtitleCache.find(fileName(nodeId));
    if (found == subtitleCache.end())
      return;
    std::ifstream input(found->second, std::ios::binary);
    if (!input)
      throw std::runtime_error("Cannot open " + found->second.string());
    std::string content(16 * 1024 * 1024 + 1, '\0');
    input.read(content.data(), static_cast<std::streamsize>(content.size()));
    const auto size = input.gcount();
    if (input.bad() || size > 16 * 1024 * 1024)
      throw std::runtime_error(
          "Cannot read forced subtitles or file exceeds 16 MiB");
    content.resize(static_cast<std::size_t>(size));
    loadAss(content);
  } catch (const std::exception &error) {
    clear();
    SDL_LogError(LogMedia, "Forced subtitles for %s: %s", nodeId.c_str(),
                 error.what());
  }
}

void ForcedSubtitleRenderer::reload() {
  cacheBuilt = false;
  lastLoadedNodeId.clear();
  clear();
}

std::filesystem::path ForcedSubtitleRenderer::dumpForActiveNode() {
  const auto node = engine.activeNode();
  if (node.empty())
    throw std::logic_error("No active node/event to dump");
  const auto path =
      preferredDocumentsDirectory(engine.GetRuntime().identify) /
      "forced_subtitles" / fileName(node);
  std::filesystem::create_directories(path.parent_path());
  if (std::filesystem::exists(path))
    throw std::runtime_error("Subtitle file already exists: " + path.string());
  std::ostringstream ass;
  ass << "[Script Info]\nTitle: " << path.stem().string()
      << "\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\nWrapStyle: 0\n\n"
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
         "Replace Comment with Dialogue and edit the node-relative times and text.\n";
  writeTextFileAtomically(path.string(), ass.str());
  reload();
  return path;
}

} // namespace Kotonoha
