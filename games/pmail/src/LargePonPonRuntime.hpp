#pragma once

#include <Kotonoha/core/Runtime.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
#include <Pmail/LargePonPonAssets.hpp>
#include <Pmail/LargePonPonVm.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Kotonoha::PmailPort {

class LargePonPonRuntime : public Kotonoha::Runtime,
                           public Kotonoha::LargePonPon::LargePonPonHost {
public:
  LargePonPonRuntime(
      std::string gameDirectory,
      std::shared_ptr<Kotonoha::Providers::LargePonPonAssets> assets);
  ~LargePonPonRuntime() override;

  void shutdown();

  // Kotonoha::Runtime overrides
  void initGlobal(Kotonoha::Engine &engine) override;
  void initSplash(Kotonoha::Engine &engine) override;
  void initMenu(Kotonoha::Engine &engine) override;
  void initGameplay(Kotonoha::Engine &engine) override;

  SDL_Point viewport() const override { return {800, 600}; }
  bool continuousTouch() const override { return true; }
  void render(Kotonoha::RendererRef renderer, float scale) override;
  void renderOverlay(Kotonoha::RendererRef renderer, float scale) override;
  void update() override;
  bool onEvent(Kotonoha::EventRef event) override;

  void saveGame(const std::string &path) override;
  void loadGame(const std::string &path) override;

  std::string debugNode() const override;
  std::string dialogueText() const override { return currentText_; }
  std::string dialogueSpeaker() const override { return currentSpeaker_; }

  // Accessors for headless tests
  const Kotonoha::LargePonPon::LargePonPonVm &vm() const { return vm_; }
  Kotonoha::LargePonPon::LargePonPonVm &vm() { return vm_; }
  bool waitingForInput() const;
  void advanceInput();

  // LargePonPonHost overrides
  std::vector<uint8_t> loadScript(const std::string &name) override;
  void onMsgAppend(const std::vector<uint16_t> &words) override;
  bool onMsgShow() override;
  bool onWinOpen() override;
  bool onWinClose() override;
  void onSetSpeaker(const std::vector<uint16_t> &words) override;
  void onChoiceClear() override;
  void onChoiceAdd(int32_t id, int32_t flag,
                   const std::vector<uint16_t> &words) override;
  bool onChoiceRun(int32_t defaultId) override;
  void onSetSeenFlag(int32_t seen) override;
  void onAttachVoice(const std::string &voice, int32_t channel) override;

  bool onTransition(int32_t type, int32_t speed) override;
  void onLoadCg(const std::string &name) override;
  void onLoadBg(const std::string &name) override;
  void onLoadChar(int32_t slot, const std::string &name) override;
  void onFillColor(int32_t bgr24) override;
  bool onPlayPchr(const std::string &pchr, int32_t flags, int32_t x, int32_t y,
                  int32_t loops, const std::string &se) override;
  bool onScreenShake(int32_t dx, int32_t dy, int32_t reps) override;
  void onLoadCgVariant(const std::string &name, int32_t h, int32_t p) override;
  void onLoadCanm(int32_t slot, const std::string &name) override;
  void onComposite() override;
  void onDrawCanm(int32_t buf, int32_t slot, int32_t type,
                  int32_t frame) override;
  void onSetCharPos(int32_t slot, int32_t x, int32_t y) override;

  void onBgmPlay(const std::string &name, int32_t loop) override;
  void onBgmStop() override;
  void onBgmFade(int32_t speed) override;
  void onVoicePlay(const std::string &name, int32_t unk) override;
  void onVoiceStop() override;
  bool onVoiceWait(int32_t flags) override;
  void onSetVoiceSync(int32_t val) override;

  bool onWait(int32_t ms, int32_t flags) override;
  bool onNameInput() override;
  bool onMapMode() override;
  void onSaveCheckpoint() override;
  void onSetSaveEnable(int32_t enable) override;
  bool onCheckSaveData() override;
  bool onLoadDialog() override;
  bool onReturnTitle(int32_t noSysSave) override;
  bool onHeartMode(int32_t mode) override;
  bool onCgMode(int32_t mode) override;
  bool onTitleMenu(int32_t page) override;
  bool onEndingWait(int32_t count) override;

private:
  enum class Mode {
    Running,
    WaitClick,
    WaitTimer,
    WaitVoice,
    Transition,
    PlayPchr,
    Shake,
    Choice,
    TitleMenu,
    MapMode,
    HeartMode,
    CgMode,
    NameInput,
    SaveLoadDialog,
    EndingWait
  };

  struct ChoiceOption {
    int32_t id = 0;
    int32_t flag = 0;
    std::string text;
  };

  void bootGame(bool firstBoot);
  void stepVm();
  void ensureCanvasTexture(SDL_Renderer *renderer);
  void uploadDisplayBuffer(SDL_Renderer *renderer);
  void blitSurfaceToBuffer(std::vector<uint8_t> &dst, SDL_Surface *src,
                           int dstX, int dstY, bool alphaBlend);
  void drawCanmToBuffer(std::vector<uint8_t> &dst, int slot, int type,
                        int frame);
  void renderTransitionFrame(float t);
  void finishTransition();
  void updateAudioFade(uint64_t now);
  std::filesystem::path saveDir() const;
  std::filesystem::path saveSlotPath(int slot) const;
  void loadAllPersistentFlags();
  bool saveSlot(int slot);
  bool loadSlot(int slot);
  void ensureTitlePack();
  void ensureMapPack();
  void ensureHeartPack();
  void ensureCgModePack();
  void ensureMesIcon();
  void drawSurfaceAt(SDL_Renderer *renderer, SDL_Surface *src, int x, int y);

  std::string gameDirectory_;
  std::shared_ptr<Kotonoha::Providers::LargePonPonAssets> assets_;
  Kotonoha::Engine *engine_ = nullptr;
  Kotonoha::LargePonPon::LargePonPonVm vm_;
  bool booted_ = false;

  Mode mode_ = Mode::Running;
  uint64_t modeStartMs_ = 0;

  // 800x600 RGBA32 framebuffers
  static constexpr int kW = 800;
  static constexpr int kH = 600;
  std::vector<uint8_t> frontBuffer_;
  std::vector<uint8_t> backBuffer_;
  std::vector<uint8_t> displayBuffer_;
  bool displayDirty_ = true;
  Uint64 canvasSerial_ = 0;
  TexturePtr canvasTex_;

  // Scene composition state
  uint32_t sceneFlags_ = 0;
  SDL_Color fillColor_{0, 0, 0, 255};
  SurfacePtr bgSurface_;
  SurfacePtr cgSurface_;
  std::array<SurfacePtr, 2> charSurfaces_;
  std::array<SDL_Point, 2> charPos_{{{0, 0}, {0, 0}}};
  std::array<Kotonoha::Providers::CanmData, 3> canm_;

  // Transition state
  int32_t transType_ = 0;
  uint64_t transDurationMs_ = 0;
  std::vector<uint8_t> transMask_;
  std::vector<uint8_t> transStartFront_;

  // PCHR animation state
  std::vector<SurfacePtr> pchrFrames_;
  int32_t pchrFlags_ = 0;
  int32_t pchrX_ = 0;
  int32_t pchrY_ = 0;
  int32_t pchrLoops_ = 1;
  size_t pchrFrameIdx_ = 0;
  int32_t pchrLoopsDone_ = 0;

  // Screen shake state
  int32_t shakeDx_ = 0;
  int32_t shakeDy_ = 0;
  int32_t shakeReps_ = 0;
  int32_t shakeOffsetX_ = 0;
  int32_t shakeOffsetY_ = 0;

  // Wait / EndingWait state
  uint64_t waitDurationMs_ = 0;
  int32_t waitFlags_ = 0;
  bool voiceWaitNonSkippable_ = false;

  // Dialogue & Choices
  bool textBoxVisible_ = false;
  std::string pendingText_;
  std::string currentText_;
  std::string currentSpeaker_;
  int currentFontProfile_ = 0;
  std::string attachedVoice_;
  int32_t attachedVoiceCh_ = 0;
  std::vector<ChoiceOption> choices_;
  int selectedChoice_ = 0;
  int hoveredChoice_ = -1;

  // Interactive modes
  int32_t titlePage_ = 0;
  int titleHover_ = -1;
  int mapHover_ = -1;
  int32_t heartModeArg_ = 0;
  int cgPage_ = 0;
  int cgHover_ = -1;
  bool saveDialogIsLoad_ = true;
  bool saveEnabled_ = true;
  Mode modeBeforeSaveDialog_ = Mode::Running;

  // UI sprite caches
  std::vector<SurfacePtr> titleSprites_;
  std::vector<SurfacePtr> mapSprites_;
  std::vector<SurfacePtr> heartSprites_;
  std::vector<SurfacePtr> cgModeSprites_;
  std::vector<SurfacePtr> mesIconFrames_;
  std::array<std::vector<uint8_t>, 4> cgP256Raw_;

  // Audio
  Kotonoha::PcmPlayer bgmPlayer_;
  Kotonoha::PcmPlayer voicePlayer_;
  Kotonoha::PcmPlayer sePlayer_;
  bool bgmFading_ = false;
  uint64_t bgmFadeStartMs_ = 0;
  uint64_t bgmFadeDurationMs_ = 0;
  std::vector<uint8_t> voiceMouthFrames_;
  int32_t voiceSyncVal_ = 0;

  // Mouse tracking in 800x600 virtual space
  float mouseX_ = -1.0f;
  float mouseY_ = -1.0f;
};

} // namespace Kotonoha::PmailPort
