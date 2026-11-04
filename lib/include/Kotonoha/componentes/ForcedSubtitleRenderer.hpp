#pragma once

#include <Kotonoha/componentes/SubtitleScene.hpp>
#include <filesystem>
#include <map>
#include <string>

namespace Kotonoha {

// Node/event-relative subtitles shared by native interpreters and VN scripts.
class ForcedSubtitleRenderer final : public SubtitleScene {
  std::map<std::string, std::filesystem::path> subtitleCache;
  std::map<std::string, std::filesystem::path> dumpedSubtitles;
  std::filesystem::path activeFile;
  std::string lastLoadedNodeId;
  bool cacheBuilt = false;
  void buildSubtitleCache();
  static std::string fileName(const std::string &nodeId);

public:
  explicit ForcedSubtitleRenderer(Engine &engine);
  ~ForcedSubtitleRenderer() override;
  void updateForActiveNode();
  void loadForNode(const std::string &nodeId);
  void reload();
  const std::filesystem::path &activePath() const { return activeFile; }
  std::filesystem::path dumpForActiveNode();
};

} // namespace Kotonoha
