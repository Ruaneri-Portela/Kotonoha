#pragma once

#include "SchoolDaysAssets.hpp"
#include "SchoolDaysScenes.hpp"
#include <Kotonoha/SchoolDaysSceneTimeline.hpp>
#include <Kotonoha/core/Runtime.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfGameplayBridge.hpp>
#include <Kotonoha/routing/SchoolDaysTitleModel.hpp>
#include <Kotonoha/routing/SchoolDaysEndBackgroundResolver.hpp>
#include <Kotonoha/routing/SchoolDaysBackLog.hpp>
#include <Kotonoha/routing/SchoolDaysBackLogPresenter.hpp>
#include <Kotonoha/routing/SchoolDaysRetailConfigPolicy.hpp>
#include <Kotonoha/routing/SchoolDaysSaveProfile.hpp>
#include <Kotonoha/routing/SchoolDaysSaveStorage.hpp>
#include <Kotonoha/routing/SchoolDaysSystemUi.hpp>
#include <memory>

namespace Kotonoha {
class FadeScene;
class SplashScreen;
class VNBacklog;
class VNSaveMenu;
} // namespace Kotonoha

namespace SchoolDays {

class Runtime final : public Kotonoha::Runtime {
  friend class GameScene;
  struct Playing {
    std::unique_ptr<Kotonoha::MediaPlayer> player;
    Kotonoha_orsEvent *event = nullptr;
    std::string loop;
    bool looping = false;
    double segmentStart = 0.0;
  };
  std::shared_ptr<Assets> assets;
  Kotonoha::Engine *engine = nullptr;
  Kotonoha::SchoolDaysKtrfGameplayBridge bridge;
  Kotonoha::SchoolDaysTitleModel title;
  Kotonoha::SchoolDaysEndBackgroundResolver::Profile titleProfile;
  std::unique_ptr<Kotonoha::MediaPlayer> titleBgm;
  std::unique_ptr<Subtitles> subtitles;
  std::unique_ptr<GameScene> gameScene;
  std::unique_ptr<Kotonoha::FadeScene> fadeScene;
  std::unique_ptr<Kotonoha::SplashScreen> engineSplash;
  std::unique_ptr<LogoSplash> logoSplash;
  // Engine fallbacks when the retail System artwork is unavailable.
  std::unique_ptr<Kotonoha::VNSaveMenu> saveMenu;
  std::unique_ptr<Kotonoha::VNBacklog> backlogFallback;
  // Native School Days panels (SaveLoad, Config, Exit, RouteMap, BackLog).
  Kotonoha::SchoolDaysSystemUi systemUi;
  Kotonoha::SchoolDaysBackLogStore backlogStore;
  Kotonoha::SchoolDaysBackLogModel backlogModel;
  Kotonoha::SchoolDaysBackLogPresenter backlogPresenter;
  Kotonoha::SchoolDaysSaveProfile profile;
  Kotonoha::SchoolDaysSaveStorage storage;
  Kotonoha::SchoolDaysRetailConfigPolicy::State config =
      Kotonoha::SchoolDaysRetailConfigPolicy::Defaults();
  std::vector<std::unique_ptr<Kotonoha::MediaPlayer>> sounds;
  bool hasProfile = false, saveLoadSaving = false;
  int saveLoadPage = -1, routeMapMajor = -1, routeMapSubpage = -1, backlogIndex = 0;
  std::string backlogScene;
  Uint64 statusTime = 0;
  std::string titleBackground;
  bool titleArtwork = true;
  int titleSelection = 0;
  Kotonoha::SchoolDaysKtrfScene scene;
  Kotonoha_orsData script{};
  Kotonoha::SchoolDaysSceneTimeline timeline;
  std::vector<Playing> playing;
  Kotonoha_orsEvent *caption = nullptr, *choice = nullptr, *fade = nullptr;
  std::string routePath, background, status;
  std::filesystem::path documents;
  Uint64 lastClock = 0;
  bool paused = false, committed = false;
  double speed = 1.0, elapsed = 0.0;
  Kotonoha_SceneTick tick = 0;

  void openScene(const Kotonoha::SchoolDaysKtrfScene &next, bool restoreChoice = false);
  void dispatch(Kotonoha_orsEvent &event);
  void play(Kotonoha_orsEvent &event, const std::string &logical,
            const std::string &channel, const std::string &loop = {});
  void choose(int value);
  void advance();
  void reset();
  void back();
  void seek(Kotonoha_SceneTick target);
  void flushGlobal();
  void continueHandoff();
  void openTitle();
  void activateControl(int control);
  void loadCheckpoint(int selector);
  void setStatus(std::string message);
  bool modal() const;
  bool panelOpen() const;
  void closePanels();
  void openSaveLoad(bool save);
  void openConfig(Kotonoha::SchoolDaysConfigMenuModel::Page page);
  void openRouteMap();
  void openExit();
  void openBacklog();
  void refreshSaveMetadata();
  void refreshRouteMap();
  void saveSlot(int slot, const std::u16string &comment);
  void loadSlot(int slot);
  void handlePanel(const Kotonoha::SchoolDaysSystemUi::Output &output);
  void handleConfig(const Kotonoha::SchoolDaysConfigMenuModel::Action &action);
  Kotonoha::SchoolDaysConfigMenuRenderPlan::VisualState configVisualState() const;
  void applyConfig();
  void loadConfig();
  void storeConfig() const;
  void playSound(Kotonoha::SchoolDaysSystemUi::UiSfxCue cue);
  void renderPanels(Kotonoha::RendererRef renderer);
  void renderStatus(Kotonoha::RendererRef renderer);
  void finishSplash();
  static Kotonoha::Gameplay *createPresentation(const char *, Kotonoha_Game *, void *);
  static void destroyPresentation(Kotonoha::Gameplay *, void *);

public:
  Runtime(std::shared_ptr<Assets> assets, std::string route, std::filesystem::path documents = {});
  ~Runtime() override;
  void shutdown();
  void init(Kotonoha::Engine &engine) override;
  void initGlobal(Kotonoha::Engine &engine) override;
  void initSplash(Kotonoha::Engine &engine) override;
  void initMenu(Kotonoha::Engine &engine) override;
  void update() override;
  void renderGameplay(Kotonoha::RendererRef renderer, float scale) override;
  void renderMenu(Kotonoha::RendererRef renderer, float scale) override;
  void onEvent(Kotonoha::EventRef event) override;
  // Native panel state (tests and the debug tools).
  const Kotonoha::SchoolDaysSystemUi &panels() const { return systemUi; }
  bool backlogOpen() const { return backlogModel.IsOpen(); }
  bool anyPanelOpen() const { return panelOpen(); }
  const Kotonoha::SchoolDaysRetailConfigPolicy::State &retailConfig() const { return config; }
  void openPanel(int control) { activateControl(control); }
  SDL_Point viewport() const override { return {1280, 720}; }
  std::string debugNode() const override;
  std::optional<double> debugNodeSeconds() const override { return elapsed; }
  std::vector<Kotonoha::DebugNode> debugNodes() const override;
  void debugJumpToNode(const std::string &id) override;
  bool hasCustomKtrf() const override { return true; }
  void loadKtrf(const std::string &path) override;
  void dumpKtrf(const std::string &path) const override;
  void saveGame(const std::string &path) override;
  void loadGame(const std::string &path) override;
};

} // namespace SchoolDays
