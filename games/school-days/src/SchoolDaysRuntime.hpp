#pragma once

#include "SchoolDaysScenes.hpp"
#include <Kotonoha/SchoolDaysMoveSomState.hpp>
#include <Kotonoha/SchoolDaysMovieEventState.hpp>
#include <Kotonoha/SchoolDaysSceneTimeline.hpp>
#include <Kotonoha/components/DisplayModeCatalog.hpp>
#include <Kotonoha/core/Runtime.hpp>
#include <Kotonoha/core/Settings.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/routing/SchoolDaysAbcAnimation.hpp>
#include <Kotonoha/routing/SchoolDaysBackLog.hpp>
#include <Kotonoha/routing/SchoolDaysBackLogEngine.hpp>
#include <Kotonoha/routing/SchoolDaysBackLogPresenter.hpp>
#include <Kotonoha/routing/SchoolDaysConfigStorage.hpp>
#include <Kotonoha/routing/SchoolDaysDisplayPolicy.hpp>
#include <Kotonoha/routing/SchoolDaysEndBackgroundResolver.hpp>
#include <Kotonoha/routing/SchoolDaysEpisodeOverlay.hpp>
#include <Kotonoha/routing/SchoolDaysExitTransition.hpp>
#include <Kotonoha/routing/SchoolDaysFrontendTransition.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfGameplayBridge.hpp>
#include <Kotonoha/routing/SchoolDaysLoadTransition.hpp>
#include <Kotonoha/routing/SchoolDaysMenuBarPresenter.hpp>
#include <Kotonoha/routing/SchoolDaysPlayDataSession.hpp>
#include <Kotonoha/routing/SchoolDaysPromptBehavior.hpp>
#include <Kotonoha/routing/SchoolDaysReplayPlaybackSession.hpp>
#include <Kotonoha/routing/SchoolDaysRetailConfigPolicy.hpp>
#include <Kotonoha/routing/SchoolDaysSaveProfile.hpp>
#include <Kotonoha/routing/SchoolDaysSaveStorage.hpp>
#include <Kotonoha/routing/SchoolDaysSystemPanelTransition.hpp>
#include <Kotonoha/routing/SchoolDaysSystemUi.hpp>
#include <Kotonoha/routing/SchoolDaysTitleModel.hpp>
#include <Kotonoha/routing/SchoolDaysTransportFade.hpp>
#include <SchoolDays/SchoolDaysAssets.hpp>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace Kotonoha {
class FadeScene;
class SplashScreen;
class VNBacklog;
class VNSaveMenu;
} // namespace Kotonoha

namespace SchoolDays {

class AbcAnalysisWorker;

class Runtime final : public Kotonoha::Runtime {
  friend class GameScene;
  friend class RuntimeReplayIsolationTestAccess;
  friend class RuntimeReplayUiTestAccess;
  friend class RuntimeConfigStorageTestAccess;
  friend class RuntimeHandoffTestAccess;
  friend class RuntimeMenuBarTestAccess;
  friend class RuntimeChoiceTestAccess;
  friend class RuntimeMediaParityTestAccess;
  enum class HandoffUiState : uint8_t {
    Idle,
    Deferred,
    SaveFeel,
    SaveLoad,
    Continuing,
  };
  enum class FrontendBgmOwner : uint8_t {
    None,
    Title,
    System,
  };
  enum class FrontendHandoff : uint8_t {
    None,
    TitleStart,
    TitleLoad,
    TitleConfig,
    TitleReplay,
    LoadToTitle,
    ConfigToTitle,
    ReplayToTitle,
  };
  enum class FrontendChild : uint8_t {
    None,
    Load,
    Config,
    Replay,
  };
  enum class SaveLoadOwner : uint8_t {
    None,
    Narrative,
    PlayData,
  };
  enum class BackLogOwner : uint8_t {
    None,
    Narrative,
    PlayData,
  };
  struct Playing {
    std::unique_ptr<Kotonoha::MediaPlayer> player;
    std::unique_ptr<Kotonoha::MediaPlayer::PreparedInput> preparedLoop;
    Kotonoha_orsEvent *event = nullptr;
    std::string loop;
    bool looping = false;
    bool movie = false;
    bool terminalFramePresented = false;
    double segmentStart = 0.0;
  };
  struct MenuBarProjection {
    bool speedControlsEnabled = true;
    bool transportLocked = false;
    bool actionInhibit = false;
    bool fullBarSuppressed = false;
    bool replayIndicator = false;
  };
  std::shared_ptr<Kotonoha::Providers::SchoolDaysAssets> assets;
  Kotonoha::Engine *engine = nullptr;
  Kotonoha::SchoolDaysKtrfGameplayBridge bridge;
  Kotonoha::SchoolDaysTitleModel title;
  Kotonoha::SchoolDaysEndBackgroundResolver::Profile titleProfile;
  Kotonoha::SchoolDaysEpisodeOverlay episodeOverlay;
  std::unique_ptr<Kotonoha::SchoolDaysReplayPlaybackSession> replayPlayback;
  std::unique_ptr<Kotonoha::SchoolDaysPlayDataSession> playDataPlayback;
  std::unique_ptr<Kotonoha::MediaPlayer> titleBgm;
  std::unique_ptr<Kotonoha::MediaPlayer> titleMovie;
  std::unique_ptr<Subtitles> subtitles;
  std::unique_ptr<GameScene> gameScene;
  std::unique_ptr<Kotonoha::SplashScreen> engineSplash;
  std::unique_ptr<LogoSplash> logoSplash;
  // Engine fallbacks when the retail System artwork is unavailable.
  std::unique_ptr<Kotonoha::VNSaveMenu> saveMenu;
  std::unique_ptr<Kotonoha::SchoolDaysBackLogEngine> backlogFallback;
  // Native School Days panels (SaveLoad, Config, Exit, RouteMap, BackLog).
  Kotonoha::SchoolDaysSystemUi systemUi;
  Kotonoha::SchoolDaysBackLogStore backlogStore;
  Kotonoha::SchoolDaysBackLogStore playDataBacklogStore;
  Kotonoha::SchoolDaysBackLogModel backlogModel;
  Kotonoha::SchoolDaysBackLogPresenter backlogPresenter;
  Kotonoha::SchoolDaysSaveProfile profile;
  Kotonoha::SchoolDaysSaveStorage storage;
  Kotonoha::SchoolDaysConfigStorage configStorage;
  Kotonoha::SchoolDaysDisplayPolicy::RetailBackendState displayBackend;
  Kotonoha::SchoolDaysDisplayPolicy::RetailState displayState;
  Kotonoha::SchoolDaysDisplayPolicy::Variant uiVariant =
      Kotonoha::SchoolDaysDisplayPolicy::Variant::WideFull;
  Kotonoha::SchoolDaysFrontendTransition frontendTransition;
  Kotonoha::SchoolDaysLoadTransition loadTransition;
  Kotonoha::SchoolDaysTransportFade transportFade;
  Kotonoha::SchoolDaysExitTransition exitTransition;
  Kotonoha::SchoolDaysSystemPanelTransition systemPanelTransition;
  Kotonoha::SchoolDaysRetailConfigPolicy::State config =
      Kotonoha::SchoolDaysRetailConfigPolicy::Defaults();
  std::vector<std::unique_ptr<Kotonoha::MediaPlayer>> sounds;
  bool hasProfile = false, saveLoadSaving = false;
  std::optional<bool> routeMapReturnSave;
  int routeMapReturnSelection = -1;
  int saveLoadPage = -1, routeMapMajor = -1, routeMapSubpage = -1,
      backlogIndex = 0;
  int playDataBacklogIndex = 0;
  std::string backlogScene;
  std::string playDataBacklogScene;
  Uint64 statusTime = 0;
  std::string titleBackground;
  std::string episodeTelop;
  Uint64 titleMovieStartMs = 0;
  float titleBackgroundHeight = 450.0f;
  bool titleArtwork = true;
  int titleSelection = 0;
  Kotonoha::SchoolDaysKtrfScene scene;
  Kotonoha_orsData script{};
  Kotonoha::SchoolDaysSceneTimeline timeline;
  std::vector<Playing> playing;
  std::unordered_map<Kotonoha_orsEvent *, Kotonoha::SchoolDaysMovieEventState>
      movieStates;
  struct AbcVoiceState {
    Kotonoha_orsEvent *event = nullptr;
    std::string key;
    std::vector<Kotonoha::SchoolDaysAbcActivityRecord> activity;
    Kotonoha::SchoolDaysAbcAnimationCursor cursor;
    int groupIndex = -1;
  };
  std::vector<Kotonoha::SchoolDaysAbcGroupResources> abcGroups;
  std::vector<AbcVoiceState> abcVoices;
  std::unordered_map<std::string, Kotonoha::SurfacePtr> abcDecodedSurfaces;
  std::unordered_map<Kotonoha_orsEvent *,
                     std::vector<Kotonoha::SchoolDaysAbcActivityRecord>>
      abcActivityCache;
  std::unique_ptr<AbcAnalysisWorker> abcAnalysisWorker;
  std::unordered_set<size_t> abcAnalysisQueued;
  std::unordered_set<Kotonoha_orsEvent *> mediaPreparationFailed;
  uint64_t abcAnalysisGeneration = 0;
  struct PreparedMedia {
    std::unique_ptr<Kotonoha::MediaPlayer> player;
    std::unique_ptr<Kotonoha::MediaPlayer::PreparedInput> loopInput;
    std::string logical;
    std::string channel;
    std::string loop;
    bool movie = false;
  };
  std::unordered_map<Kotonoha_orsEvent *, PreparedMedia> preparedMedia;
  std::unordered_map<size_t, PreparedMedia> pendingMedia;
  std::unordered_set<size_t> pendingBackgrounds;
  std::unordered_set<Kotonoha_orsEvent *> backgroundPreparationFailed;
  std::unordered_map<Kotonoha_orsEvent *, std::string> preparedBackgrounds;
  std::unordered_map<Kotonoha_orsEvent *,
                     std::vector<Kotonoha::SchoolDaysAbcGroupResources>>
      preparedBackgroundAbc;
  std::unordered_map<Kotonoha_orsEvent *,
                     std::vector<std::pair<std::string, Kotonoha::SurfacePtr>>>
      preparedBackgroundAbcSurfaces;
  std::unordered_map<std::string, Kotonoha::TexturePtr> abcTextures;
  Kotonoha::TexturePtr backgroundComposite;
  int backgroundCompositeWidth = 0;
  int backgroundCompositeHeight = 0;
  Uint64 backgroundCompositeSerial = 0;
  std::string backgroundCompositeKey;
  Kotonoha::SchoolDaysMoveSomNoOp moveSom;
  Kotonoha_orsEvent *caption = nullptr, *choice = nullptr, *fade = nullptr;
  struct ChoicePresentationState {
    int result = -2;
    int exitSelected = -1;
    Kotonoha_SceneTick exitStartTick = 0;
    bool exitActive = false;
    bool finalFrameRendered = false;
  } choicePresentation;
  Kotonoha::SchoolDaysPromptInteraction choiceInteraction;
  std::string routePath, background, status;
  Kotonoha_SceneTick backgroundStartTick = 0;
  Kotonoha_SceneTick backgroundEndTick = 0;
  std::filesystem::path documents;
  Uint64 lastClock = 0;
  bool paused = false, committed = false;
  bool backAtSceneStart = false;
  bool titleHasPresented = false;
  FrontendBgmOwner frontendBgmOwner = FrontendBgmOwner::None;
  FrontendHandoff frontendHandoff = FrontendHandoff::None;
  FrontendChild frontendChild = FrontendChild::None;
  SaveLoadOwner saveLoadOwner = SaveLoadOwner::None;
  BackLogOwner backlogOwner = BackLogOwner::None;
  HandoffUiState handoffUiState = HandoffUiState::Idle;
  int replaySelectResult = -2;
  // Retail SETSELECT overrides only the effective media/story clock to 1x;
  // the player's selected speed index remains the persistent MenuBar state.
  bool choiceSpeedOverrideActive = false;
  int pendingReplayIndex = -1;
  int pendingReplayChoice = -1;
  bool replayHSceneTransitionActive = false;
  bool exitConfirmedByPlayer = false;
  double speed = 1.0, elapsed = 0.0;
  int speedIndex = 0;
  Kotonoha_SceneTick tick = 0;
  int displayAspectPreference =
      0; // 0 Auto, 1 4:3, 2 16:9 (commercial remaster)
  int displayPanelSelectedResolution = -1;
  int displayPanelHoverRow = -1;
  int displayPanelHoverResolution = -1;
  int displayPanelHoverRenderScale = -1;
  int displayPanelHoverPreset = -1;
  int displayPanelResolutionScroll = 0;
  bool displayPanelResolutionOpen = false;
  bool displayPanelRenderScaleOpen = false;
  bool displayPanelPresetOpen = false;
#if !defined(__ANDROID__) && !defined(ANDROID)
  // Desktop Display Options is transactional: other tabs and Close discard
  // these fields. The Android panel above intentionally keeps its own flow.
  Kotonoha::EngineSettings desktopDisplayDraft;
  bool desktopDisplayDraftActive = false;
  enum class DesktopDisplayPopup : uint8_t {
    None,
    Resolution,
    Upscaler,
    Shader,
    GlslPath
  };
  DesktopDisplayPopup desktopDisplayPopup = DesktopDisplayPopup::None;
  int desktopDisplayPopupHover = -1;
  int desktopDisplayPopupScroll = 0;
  std::string desktopDisplayPathEdit;
  std::vector<Kotonoha::DisplayModeChoice> desktopDisplayModes;
#endif
  bool framePrimaryReady = false;
  bool frameBoundary = false;
  bool pendingSceneAdvance = false;
  bool pendingBackCapture = false;
  std::optional<Kotonoha_SceneTick> pendingSeek;

  void openScene(const Kotonoha::SchoolDaysKtrfScene &next,
                 bool restoreChoice = false);
  void bindScene(const Kotonoha::SchoolDaysKtrfScene &next,
                 Kotonoha::Gameplay *presentation, bool restoreChoice = false);
  void dispatch(Kotonoha_orsEvent &event);
  void discoverAbcForBackground();
  bool prepareMediaEvent(Kotonoha_orsEvent &event);
  void warmOneFutureMedia();
  bool completePendingSeek();
  void resetPreparedMedia();
  void retireSceneEventPointers();
  std::unique_ptr<Kotonoha::MediaPlayer> takePreparedMedia(
      Kotonoha_orsEvent &event, const std::string &logical,
      const std::string &channel, const std::string &loop,
      std::unique_ptr<Kotonoha::MediaPlayer::PreparedInput> *loopInput);
  void prepareAbcActivity(Kotonoha_orsEvent &event, const std::string &logical);
  void pollAbcActivity();
  bool waitForDueMedia(Kotonoha_SceneTick candidateTick);
  void bindAbcVoice(AbcVoiceState &voice);
  void rebindActiveAbcVoices();
  void updateAbcVoices();
  void renderAbcOverlays(Kotonoha::RendererRef renderer, const SDL_FRect &area);
  SDL_Texture &abcTexture(Kotonoha::RendererRef renderer,
                          const std::string &resource);
  bool renderBackgroundWithAbc(Kotonoha::RendererRef renderer,
                               const SDL_FRect &area);
  void resetBackgroundComposition();
  bool play(Kotonoha_orsEvent &event, const std::string &logical,
            const std::string &channel, const std::string &loop = {});
  void choose(int value);
  void configureChoice(Kotonoha_orsEvent &event);
  int semanticChoiceForSlot(int slot) const;
  void updateChoiceInteraction();
  void armChoiceExit(int selected);
  bool choiceTailActive() const;
  bool choiceNeedsFinalFrame() const;
  bool choicePresentationComplete() const;
  void advance();
  void reset();
  void back();
  void seek(Kotonoha_SceneTick target);
  void skipToNextChoice();
  void flushGlobal();
  void armHandoff();
  void updateHandoffUi();
  void submitSaveFeel(Kotonoha::SchoolDaysSaveFeelModel::Result result);
  void continueHandoff();
  void openTitle(bool startBgm = true);
  bool openReplay();
  bool refreshReplayAvailability();
  void startReplayPlayback(int index, int choice = -1);
  void beginReplayHScene(int index, int choice = -1);
  void startPlayDataPlayback(int slot);
  void finishTemporaryPlayback(bool returnToReplay);
  bool temporaryPlaybackActive() const;
  bool startFrontendBgm(FrontendBgmOwner owner);
  void suspendFrontendBgm();
  const std::string &frontendBgmStem() const;
  void beginTitleHandoff(FrontendHandoff handoff, uint64_t nowMs);
  void beginChildToTitle(uint64_t nowMs);
  void updateFrontendTransition(uint64_t nowMs);
  void renderFrontendTransition(Kotonoha::RendererRef renderer, uint64_t nowMs);
  void updateLoadTransition(uint64_t nowMs);
  void renderLoadTransition(Kotonoha::RendererRef renderer, uint64_t nowMs);
  void
  beginTransportFade(Kotonoha::SchoolDaysTransportFade::Operation operation);
  void updateTransportFade(uint64_t nowMs);
  void renderTransportFade(Kotonoha::RendererRef renderer, uint64_t nowMs);
  void updateExitTransition(uint64_t nowMs);
  void renderExitTransition(Kotonoha::RendererRef renderer, uint64_t nowMs);
  void updateSystemPanelTransition(uint64_t nowMs);
  void renderSystemPanelTransition(Kotonoha::RendererRef renderer,
                                   uint64_t nowMs);
  void
  beginSystemPanel(Kotonoha::SchoolDaysSystemPanelTransition::Target target);
  bool beginSystemPanelClose();
  void openExitDialog(bool returnToTitle);
  void finishMainMenuReturn();
  bool pointerToTitle(float x, float y, int *virtualX, int *virtualY) const;
  bool pointerToBackLog(float x, float y, int *virtualX, int *virtualY) const;
  void refreshDisplayProjection();
  void prepareCarryFrame();
  void commitCarryFrame();
  void cancelPreparedCarryFrame();
  void resetCarryFrame();
  void activateControl(int control);
  void activateMenuAction(
      const Kotonoha::SchoolDaysMenuBarPresenter::Action &action);
  void setPlaybackSpeedIndex(int index);
  void applyEffectivePlaybackRate(double rate);
  MenuBarProjection menuBarProjection() const;
  void updateMenuBar(Kotonoha::SchoolDaysMenuBarPresenter &presenter,
                     uint32_t nowMs);
  void loadCheckpoint(int selector);
  void setStatus(std::string message);
  bool modal() const;
  bool panelOpen() const;
  void closePanels();
  void openSaveLoad(bool save, bool playView = true);
  void openConfig(Kotonoha::SchoolDaysConfigMenuModel::Page page,
                  bool playView = true);
  void openRouteMap(bool playView = true);
  void openExit();
  void openMainMenuConfirm();
  void openBacklog(bool playView = true);
  bool handleBacklogAction() override;
  void refreshSaveMetadata();
  void refreshRouteMap();
  void saveSlot(int slot, const std::u16string &comment);
  void loadSlot(int slot);
  void handlePanel(const Kotonoha::SchoolDaysSystemUi::Output &output);
  void handleConfig(const Kotonoha::SchoolDaysConfigMenuModel::Action &action);
  bool displayPanelOpen() const;
  bool applyDisplayAspectPreference();
  bool handleDisplayPanel(Kotonoha::EventRef event);
  void renderDisplayPanel(Kotonoha::RendererRef renderer);
  void activateDesktopDisplayControl(int control);
  void resetDesktopDisplayDraft();
  void discardDesktopDisplayDraft();
  bool handleDesktopDisplayPanel(Kotonoha::EventRef event);
  void renderDesktopDisplayPanel(Kotonoha::RendererRef renderer);
  Kotonoha::SchoolDaysConfigMenuRenderPlan::VisualState
  configVisualState() const;
  void applyConfig();
  void loadConfig();
  void cacheConfigState();
  void playSound(Kotonoha::SchoolDaysSystemUi::UiSfxCue cue);
  void renderPanels(Kotonoha::RendererRef renderer);
  void renderStatus(Kotonoha::RendererRef renderer);
  void finishSplash();
  static Kotonoha::Gameplay *createPresentation(const char *, Kotonoha_Game *,
                                                void *);
  static void destroyPresentation(Kotonoha::Gameplay *, void *);
  static bool resolveReplayScene(const char *, Kotonoha::SchoolDaysKtrfScene *,
                                 void *, Kotonoha_KtrfError *);

public:
  Runtime(std::shared_ptr<Kotonoha::Providers::SchoolDaysAssets> assets,
          std::string route, std::filesystem::path documents = {});
  ~Runtime() override;
  void shutdown();
  // SDL close/quit requests are routed through the retail EXIT confirmation.
  // Returns true when the native request was consumed by School Days.
  bool handleNativeQuitRequest();
  void init(Kotonoha::Engine &engine) override;
  void initGlobal(Kotonoha::Engine &engine) override;
  void initSplash(Kotonoha::Engine &engine) override;
  void initMenu(Kotonoha::Engine &engine) override;
  void update() override;
  void renderGameplay(Kotonoha::RendererRef renderer, float scale) override;
  bool useOwnedNarrativeTarget() const override { return true; }
  float narrativePresentationHeightScale() const override;
  void renderNarrativeOverlay(Kotonoha::RendererRef renderer,
                              float scale) override;
  bool narrativeFrameDrawable() const override {
    return currentState == Kotonoha::GameState::Gameplay && framePrimaryReady;
  }
  bool narrativeBoundaryFrame() const override {
    return currentState == Kotonoha::GameState::Gameplay && frameBoundary;
  }
  std::string narrativeVisualOwner() const override { return scene.sceneKey; }
  bool devPauseRequested() const override;
  bool devConsumeStep() override;
  void devIdle() override;
  void devControlPump(Kotonoha::RendererRef renderer) override;
  void devInputProcessed(Kotonoha::EventRef event) override;
  void traceDev(const char *event);
  void renderMenu(Kotonoha::RendererRef renderer, float scale) override;
  bool onEvent(Kotonoha::EventRef event) override;
  bool onScreenEvent(Kotonoha::EventRef event) override;
  void renderScreen(Kotonoha::RendererRef renderer, float scale) override;
  // Native panel state (tests and the debug tools).
  const Kotonoha::SchoolDaysSystemUi &panels() const { return systemUi; }
  bool backlogOpen() const { return backlogModel.IsOpen(); }
  bool anyPanelOpen() const { return panelOpen(); }
  const Kotonoha::SchoolDaysRetailConfigPolicy::State &retailConfig() const {
    return config;
  }
  void openPanel(int control) { activateControl(control); }
  SDL_Point viewport() const override { return {1280, 720}; }
  bool continuousTouch() const override { return true; }
  bool advanceStoryTime(double seconds) override;
  std::string debugNode() const override;
  std::string nodeClockKey() const override;
  bool usesTimedNodeClock() const override { return true; }
  std::optional<double> debugNodeSeconds() const override { return elapsed; }
  std::vector<Kotonoha::DebugNode> debugNodes() const override;
  std::vector<Kotonoha::DebugAudioAsset> debugSceneAudio() override;
  void debugOverlayAssetChanged(const std::string &id) override;
  void debugJumpToNode(const std::string &id) override;
  void debugRestartScene() override;
  bool hasCustomKtrf() const override { return true; }
  void loadKtrf(const std::string &path) override;
  void dumpKtrf(const std::string &path) const override;
  void saveGame(const std::string &path) override;
  void loadGame(const std::string &path) override;
  // Live translation: expose the currently displayed subtitle texts so the
  // debug editor can find and translate them, and reload them on change.
  std::vector<std::string> activeTranslationKeys() const override;
  Kotonoha::Translation::SourceTexts translationSources() const override;
};

} // namespace SchoolDays
