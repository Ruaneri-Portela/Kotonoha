#pragma once

#include <Kotonoha/components/SubtitleScene.hpp>
#include <filesystem>
#include <map>
#include <string>

namespace Kotonoha {

// Node/event-relative subtitles shared by native interpreters and VN scripts.
// A file for the full active node takes precedence; a node-only file is the
// fallback for runtimes whose active node includes an event/position suffix.
class ForcedSubtitleRenderer final : public SubtitleScene {
  std::filesystem::path sourceDirectory_;
  std::filesystem::path localeDirectory_;
  std::string locale_;
  std::map<std::string, std::filesystem::path> subtitleCache;
  std::map<std::string, std::filesystem::path> legacySubtitleCache;
  std::map<std::string,
           std::map<std::string, std::filesystem::path>> dumpedSubtitles;
  std::filesystem::path activeFile;
  std::string lastLoadedNodeId;
  bool useNodeClock = false;
  bool cacheBuilt = false;
  void buildSubtitleCache();
  static std::string fileName(const std::string &nodeId);

public:
  explicit ForcedSubtitleRenderer(
      Engine &engine,
      const std::filesystem::path &explicitDirectory = {});
  ~ForcedSubtitleRenderer() override;
  // BCP-47 language/region identifier, e.g. pt-BR, ru or zh-CN. An empty
  // selection uses the original English root directory. Explicit "en" prefers
  // the English subdirectory, then its legacy root files. Other languages
  // never inherit root subtitles.
  void setLocale(const std::string &locale);
  const std::string &locale() const { return locale_; }
  void updateForActiveNode();
  void loadForNode(const std::string &nodeId);
  void reload();
  const std::filesystem::path &sourceDirectory() const { return sourceDirectory_; }
  const std::filesystem::path &activePath() const { return activeFile; }
  std::filesystem::path dumpForActiveNode();
};

} // namespace Kotonoha
