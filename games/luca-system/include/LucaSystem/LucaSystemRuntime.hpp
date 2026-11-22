#pragma once

#include <Kotonoha/core/Runtime.hpp>
#include <LucaSystem/LucaSystemAssets.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace LucaSystem {

class LucaSystemRuntime : public Kotonoha::Runtime {
public:
  LucaSystemRuntime(
      std::shared_ptr<Kotonoha::Providers::LucaSystemAssets> assets);
  ~LucaSystemRuntime() override;
  void shutdown();

  void init(Kotonoha::Engine &engine) override;
  void initGlobal(Kotonoha::Engine &engine) override;
  void initSplash(Kotonoha::Engine &engine) override;
  void initMenu(Kotonoha::Engine &engine) override;
  void initGameplay(Kotonoha::Engine &engine) override;
  void update() override;
  void renderGameplay(Kotonoha::RendererRef renderer, float scale) override;
  bool requiresNativeTextResolution() const override { return true; }
  bool onEvent(Kotonoha::EventRef event) override;
  bool onScreenEvent(Kotonoha::EventRef event) override;

  bool handleAdvanceAction() override;
  bool handleBackAction() override;

  void saveGame(const std::string &path) override;
  void loadGame(const std::string &path) override;

  // Start / resume a scenario script by SCRIPT.PAK entry name.
  // resume=true restores a saved PC without executing script instructions.
  bool startScenario(const std::string &name, size_t position = 0,
                     bool resume = false);
  bool debugJumpToLabel(const std::string &label);

  void advanceDialogue();

  // Programmatic / headless inspection & interaction helpers for integration
  // testing and debug tooling.
  std::string currentScenarioName() const;
  size_t instructionPosition() const;
  bool isRunning() const;
  bool isDialogueVisible() const;
  bool isChoiceActive() const;
  std::vector<std::string> activeChoices() const;
  bool selectChoice(int optionIndex);
  bool isMoviePlaying() const;
  void skipMovie();
  void skipWait();
  int32_t getVariable(uint32_t varId) const;
  void setVariable(uint32_t varId, int32_t value);
  uint32_t activeBgmId() const;
  uint32_t lastVoiceId() const;
  size_t visibleLayerCount() const;
  bool isGalleryOpen() const;
  uint16_t galleryImageId() const;
  bool isCgUnlocked(uint32_t id) const;

  std::string dialogueText() const override { return dialogueText_; }
  std::string dialogueSpeaker() const override { return dialogueSpeaker_; }
  std::string debugNode() const override;
  std::string debugNodeCatalogKey() const override;
  std::vector<Kotonoha::DebugNode> debugNodes() const override;
  void debugJumpToNode(const std::string &id) override;
  int translationRoute() const override { return 0; }
  std::vector<Kotonoha::DebugAudioAsset> debugSceneAudio() override;
  void debugRestartScene() override;
  std::vector<std::string> activeTranslationKeys() const override;
  Kotonoha::Translation::SourceTexts translationSources() const override;
  bool continuousTouch() const override { return true; }

  const Kotonoha::Providers::LucaSystemAssets &assets() const {
    return *assets_;
  }

  // Current dialogue state (filled by the script interpreter).
  std::string dialogueText_;
  std::string dialogueSpeaker_;
  std::vector<std::string> originalActiveKeys_;
  std::vector<std::string> scenarioTexts_;
  uint32_t currentDialogueVoiceId_ = 0;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::shared_ptr<Kotonoha::Providers::LucaSystemAssets> assets_;
};

} // namespace LucaSystem
