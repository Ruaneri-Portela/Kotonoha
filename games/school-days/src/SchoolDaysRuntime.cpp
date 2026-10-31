#include "SchoolDaysRuntime.hpp"

#include <Kotonoha/SchoolDaysBgmEventState.hpp>
#include <Kotonoha/SchoolDaysMovieEventState.hpp>
#include <Kotonoha/componentes/FadeScene.hpp>
#include <Kotonoha/componentes/SettingsMenu.hpp>
#include <Kotonoha/componentes/SplashScreen.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNSaveMenu.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/routing/SchoolDaysTitleProfile.hpp>
#include <Kotonoha/routing/SchoolDaysTitleRenderPlan.hpp>
#include <Kotonoha/routing/SchoolDaysRouteMapModel.hpp>
#include <Kotonoha/routing/SchoolDaysRouteMapOracle.hpp>
#include <Kotonoha/routing/SchoolDaysSaveMetadata.hpp>
#include <Kotonoha/routing/SchoolDaysSaveService.hpp>
#include <Kotonoha/routing/SchoolDaysSubtitleTypography.hpp>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace SchoolDays {
namespace {
using Policy = Kotonoha::SchoolDaysRetailConfigPolicy;
using Ui = Kotonoha::SchoolDaysSystemUi;
// NeoStart presents School Days in its 16:9 1280x720 WideFull layout.
constexpr auto WideFull = Kotonoha::SchoolDaysSaveFeelModel::Variant::WideFull;
constexpr Uint64 StatusMilliseconds = 6000;
constexpr const char *ConfigFile = "SchoolDaysConfig.ini";

void Check(bool ok, const Kotonoha_KtrfError &error) {
  if (!ok) throw std::runtime_error(error.message[0] ? error.message : "School Days routing failed");
}

void Check(bool ok, const std::string &error, const char *fallback) {
  if (!ok) throw std::runtime_error(error.empty() ? fallback : error);
}

void WriteBytes(const std::string &path, const std::vector<uint8_t> &bytes) {
  Kotonoha::writeTextFileAtomically(path, std::string(bytes.begin(), bytes.end()));
}

struct Presentation final : Kotonoha::SchoolDaysScenePresentation {
  Kotonoha_orsData data{};
  bool choiceCommitted = false;
  ~Presentation() override { Kotonoha_OrsClean(&data); }
  void RestoreChoice(int64_t) override { choiceCommitted = true; }
  void SetPaused(bool) override {}
};

const char *Text(const char *text) { return text ? text : ""; }

std::u16string Utf16(const std::string &text) {
  if (text.empty()) return {};
  char *converted = SDL_iconv_string("UTF-16LE", "UTF-8", text.c_str(), text.size() + 1);
  if (!converted) return {};
  std::u16string result(reinterpret_cast<const char16_t *>(converted));
  SDL_free(converted);
  return result;
}

struct TtfFont {
  TTF_Font *font = nullptr;
  ~TtfFont() { if (font) TTF_CloseFont(font); }
};
}

Runtime::Runtime(std::shared_ptr<Assets> source, std::string route, std::filesystem::path saveDirectory)
    : Kotonoha::Runtime("School Days HQ", "0.1.0", "me.hirameki.kotonoha.schooldays"),
      assets(std::move(source)), systemUi(assets.get()), backlogPresenter(assets.get()),
      routePath(std::move(route)), documents(std::move(saveDirectory)) {
  Kotonoha_KtrfError error{};
  Check(bridge.Open(routePath.c_str(), assets->orsRoot().string().c_str(), nullptr,
                    {createPresentation, destroyPresentation, this}, &error), error);
}

Runtime::~Runtime() { shutdown(); }

Kotonoha::Gameplay *Runtime::createPresentation(const char *path, Kotonoha_Game *, void *userdata) {
  auto &self = *static_cast<Runtime *>(userdata);
  try {
    const auto bytes = self.assets->read(path);
    auto presentation = std::make_unique<Presentation>();
    presentation->data = Kotonoha_OrsParserBytes(bytes.data(), bytes.size());
    if (!presentation->data.size) throw std::runtime_error("Empty or invalid ORS: " + std::string(path));
    return presentation.release();
  } catch (const std::exception &error) {
    self.setStatus(error.what());
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.what());
    return nullptr;
  }
}

void Runtime::destroyPresentation(Kotonoha::Gameplay *presentation, void *) { delete presentation; }

void Runtime::shutdown() {
  if (bridge.IsOpen() && !documents.empty()) {
    try { flushGlobal(); }
    catch (const std::exception &error) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error.what()); }
  }
  playing.clear();
  sounds.clear();
  titleBgm.reset();
  ClearScenes();
  // Panel textures belong to the engine renderer: release them first.
  systemUi.Close();
  backlogModel.Close();
  backlogPresenter.Close();
  subtitles.reset();
  gameScene.reset();
  fadeScene.reset();
  engineSplash.reset();
  logoSplash.reset();
  saveMenu.reset();
  backlogFallback.reset();
  assets->clearCache();
  bridge.Close();
  script = {};
  engine = nullptr;
}

void Runtime::init(Kotonoha::Engine &owner) {
  if (engine != &owner) {
    engine = &owner;
    if (documents.empty()) documents = Kotonoha::preferredDocumentsDirectory("school-days");
    else std::filesystem::create_directories(documents);
    subtitles = std::make_unique<Subtitles>(owner);
    gameScene = std::make_unique<GameScene>(owner, *this);
    fadeScene = std::make_unique<Kotonoha::FadeScene>(owner);
    logoSplash = std::make_unique<LogoSplash>(owner, *assets);
    saveMenu = std::make_unique<Kotonoha::VNSaveMenu>(owner);
    backlogFallback = std::make_unique<Kotonoha::VNBacklog>(owner);

    // FILMENGINE.INI drives the retail save layout, FONTDATA and system SFX.
    std::string failure;
    hasProfile = Kotonoha::SchoolDaysSaveProfile::Load(assets->root().string(), &profile, &failure);
    if (hasProfile && !storage.Initialize(documents, profile, &failure))
      SDL_LogWarn(Kotonoha::LogUI, "School Days save storage: %s", failure.c_str());
    if (!hasProfile)
      SDL_LogInfo(Kotonoha::LogUI, "School Days FILMENGINE.INI unavailable (%s); using engine save menu",
                  failure.c_str());

    std::vector<uint8_t> bytes;
    bool present = false;
    if (storage.IsInitialized() && !storage.LoadGlobalFlag(&bytes, &present, &failure)) present = false;
    const auto legacy = documents / "GlobalFlag";
    if (!present && std::filesystem::is_regular_file(legacy)) {
      bytes = assets->read(legacy.string());
      present = true;
    }
    if (!present) {
      std::string codecError;
      if (!Kotonoha::SchoolDaysPersistenceCodec::SerializeFlgH({}, &bytes, &codecError))
        throw std::runtime_error(codecError);
    }
    Kotonoha_KtrfError error{};
    Check(bridge.Session().LoadGlobalFlag(bytes, &error), error);
    loadConfig();
    applyConfig();
    lastClock = SDL_GetTicks();
  }
  Kotonoha::Runtime::init(owner);
}

void Runtime::initGlobal(Kotonoha::Engine &) {
  AddScene(subtitles.get(), Kotonoha::GameState::Gameplay);
  AddScene(fadeScene.get(), Kotonoha::GameState::Gameplay);
  AddScene(gameScene.get(), Kotonoha::GameState::Gameplay);
  AddScene(backlogFallback.get(), Kotonoha::GameState::Gameplay);
  AddScene(saveMenu.get(), Kotonoha::GameState::Menu);
  AddScene(saveMenu.get(), Kotonoha::GameState::Gameplay);
}

void Runtime::initSplash(Kotonoha::Engine &owner) {
  // Same order as the original port: engine SPLASH.PNG, then the retail logo.
  if (!engineSplash) engineSplash = std::make_unique<Kotonoha::SplashScreen>(owner, splashPictures());
  else engineSplash->restart();
  logoSplash->restart();
  AddScene(engineSplash.get(), Kotonoha::GameState::Splash);
}

void Runtime::initMenu(Kotonoha::Engine &) { openTitle(); }

void Runtime::finishSplash() {
  SetState(Kotonoha::GameState::Menu);
  Kotonoha::Runtime::init(*engine);
}

void Runtime::setStatus(std::string message) {
  status = std::move(message);
  statusTime = SDL_GetTicks();
}

bool Runtime::panelOpen() const {
  return systemUi.IsOpen() || backlogModel.IsOpen() || (saveMenu && saveMenu->isOpen()) ||
         (backlogFallback && backlogFallback->isVisible()) ||
         (engine && engine->GetSettingsMenu().isOpen());
}

bool Runtime::modal() const { return panelOpen() || (engine && engine->isStoryPaused()); }

void Runtime::closePanels() {
  systemUi.Close();
  backlogModel.Close();
  backlogPresenter.Close();
  if (saveMenu && saveMenu->isOpen()) saveMenu->close();
  if (backlogFallback && backlogFallback->isVisible()) backlogFallback->close();
  saveLoadPage = routeMapMajor = routeMapSubpage = -1;
}

void Runtime::openTitle() {
  playing.clear();
  if (subtitles) subtitles->clear();
  if (fadeScene) fadeScene->clear();
  closePanels();
  titleArtwork = true;
  currentState = Kotonoha::GameState::Menu;
  Kotonoha::SchoolDaysTitleModel::HostState host;
  Kotonoha_KtrfError error{};
  int64_t endClear = 0;
  Check(bridge.Session().State().ReadGlobalInt("EndClear", &endClear, &error), error);
  host.endClearReplay = endClear != 0;
  std::string ignored;
  if (Kotonoha::SchoolDaysTitleProfile::Load(assets->root().string(), &titleProfile, &ignored)) {
    host.endClearVisual = titleProfile.endBgView && endClear != 0;
    Kotonoha::SchoolDaysEndBackgroundResolver::Result background;
    if (Kotonoha::SchoolDaysEndBackgroundResolver::Resolve(
            titleProfile, &bridge.Session().State(), &background, &error)) {
      try { titleBackground = assets->resolve(background.asset); }
      catch (const std::exception &) { titleBackground.clear(); }
    }
    if (engine && !titleProfile.titleBgmStem.empty()) {
      try {
        titleBgm = std::make_unique<Kotonoha::MediaPlayer>(engine->GetAudio());
        titleBgm->open(*assets, assets->resolve(titleProfile.titleBgmStem + "_loop.ogg"), true, "BGM");
      } catch (const std::exception &) { titleBgm.reset(); }
    }
  }
  title.Open(assets->root().string(), WideFull, host, &ignored);
}

void Runtime::openScene(const Kotonoha::SchoolDaysKtrfScene &next, bool restoreChoice) {
  titleBgm.reset();
  playing.clear();
  assets->clearCache();
  auto *presentation = static_cast<Presentation *>(bridge.CurrentGameplay());
  if (!presentation) throw std::runtime_error("No School Days scene presentation");
  scene = next;
  script = presentation->data;
  timeline.Bind(&script);
  if (subtitles) subtitles->bind(script);
  if (fadeScene) fadeScene->clear();
  // Native ScriptChange truncates abandoned-branch BackLog messages.
  if (!scene.sceneKey.empty() && scene.sceneKey != backlogScene) {
    backlogStore.ObserveScriptChange(scene.sceneKey);
    backlogScene = scene.sceneKey;
  }
  caption = choice = nullptr;
  background.clear();
  committed = restoreChoice || presentation->choiceCommitted;
  paused = false;
  elapsed = 0;
  tick = 0;
  lastClock = SDL_GetTicks();
  currentState = Kotonoha::GameState::Gameplay;
}

void Runtime::reset() {
  playing.clear();
  Kotonoha_KtrfError error{};
  Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
  Check(bridge.ResetNewGame(&result, &error), error);
  backlogStore.ResetTransient();
  backlogFallback->clear();
  backlogIndex = 0;
  backlogScene.clear();
  openScene(result.scene);
}

void Runtime::play(Kotonoha_orsEvent &event, const std::string &logical,
                   const std::string &channel, const std::string &loop) {
  try {
    auto player = std::make_unique<Kotonoha::MediaPlayer>(engine->GetAudio());
    player->open(*assets, assets->resolve(logical), channel != "Movie", channel);
    Playing item;
    item.player = std::move(player);
    item.event = &event;
    item.loop = loop;
    item.segmentStart = event.startTick / 24.0;
    playing.push_back(std::move(item));
  } catch (const std::exception &error) {
    setStatus(error.what());
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error.what());
  }
}

void Runtime::dispatch(Kotonoha_orsEvent &event) {
  const auto stop = [&](Kotonoha_orsType command) {
    playing.erase(std::remove_if(playing.begin(), playing.end(),
        [&](const Playing &item) { return item.event->command == command; }), playing.end());
  };
  switch (event.command) {
  case CREATE_BG:
    try { background = assets->resolve(Text(event.data.create_bg->path), true); }
    catch (const std::exception &error) { setStatus(error.what()); background.clear(); }
    break;
  case PLAY_MOVIE:
    stop(PLAY_MOVIE);
    if (event.data.play_movie->a == 0)
      play(event, std::string(Text(event.data.play_movie->path)) + ".wmv", "Movie");
    else setStatus("Unsupported PlayMovie numeric mode");
    break;
  case END_ROLL:
    stop(END_ROLL);
    play(event, std::string(Text(event.data.path_end->path)) + ".wmv", "Movie");
    break;
  case PLAY_VOICE:
    // Retail MenVoice: numbered (male) voices only play when enabled.
    if (event.data.play_voice->a == 0 || config.menVoice)
      play(event, std::string(Text(event.data.play_voice->path)) + ".OGG", "Voice");
    break;
  case PLAY_SE:
    playing.erase(std::remove_if(playing.begin(), playing.end(), [&](const Playing &item) {
      return item.event->command == PLAY_SE && item.event->data.play_se->a == event.data.play_se->a;
    }), playing.end());
    play(event, std::string(Text(event.data.play_se->path)) + ".OGG", "SE");
    break;
  case PLAY_BGM: {
    stop(PLAY_BGM);
    auto resources = Kotonoha::SchoolDaysBgmResources::Normal(Text(event.data.path_end->path));
    play(event, resources.intro, "BGM", resources.loop);
    break;
  }
  case END_BGM:
    stop(END_BGM);
    play(event, std::string(Text(event.data.path_end->path)) + ".ogg", "BGM");
    break;
  case PRINT_TEXT:
    caption = &event;
    backlogStore.AppendLogMessage(backlogIndex++, Utf16(Text(event.data.print_text->character)),
                                  Utf16(Text(event.data.print_text->text)));
    backlogFallback->addEntry(Text(event.data.print_text->character), Text(event.data.print_text->text));
    break;
  case SetSELECT:
    if (!committed) choice = &event;
    break;
  case BLACK_FADE:
  case WHITE_FADE: {
    // data.fade->a selects fade-in (from the color); otherwise fade out and
    // keep the color until the next fade event.
    const Uint8 value = event.command == WHITE_FADE ? 255 : 0;
    Kotonoha::FadeScene::Event fade;
    fade.start = event.startTick / 24.0;
    fade.end = event.endTick / 24.0;
    fade.color = {value, value, value, 255};
    fade.direction = event.data.fade && event.data.fade->a ? Kotonoha::FadeScene::Direction::In
                                                            : Kotonoha::FadeScene::Direction::Out;
    fade.hold = fade.direction == Kotonoha::FadeScene::Direction::Out;
    fadeScene->add(fade);
    break;
  }
  default:
    break;
  }
}

void Runtime::choose(int value) {
  if (committed) return;
  Kotonoha_KtrfError error{};
  int accepted = 0;
  Check(bridge.CommitChoice(value, &accepted, &error), error);
  if (!accepted) throw std::runtime_error("School Days choice was not accepted");
  committed = true;
  choice = nullptr;
}

void Runtime::advance() {
  playing.clear();
  Kotonoha_KtrfError error{};
  Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
  Check(bridge.Advance(nullptr, &result, &error), error);
  using Kind = Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind;
  if (result.kind == Kind::BlockedChoice) {
    choose(-1);
    advance();
  } else if (result.kind == Kind::SwappedScene) {
    openScene(result.scene);
  } else if (result.kind == Kind::Unresolved) {
    paused = true;
    setStatus("Unresolved School Days routing transition: " + scene.sceneKey);
  }
  flushGlobal();
}

void Runtime::continueHandoff() {
  playing.clear();
  Kotonoha_KtrfError error{};
  Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
  Check(bridge.ContinueHandoff(&result, &error), error);
  if (result.kind == Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::Terminal) {
    script = {};
    openTitle();
  } else {
    openScene(result.scene);
  }
  flushGlobal();
}

void Runtime::back() {
  playing.clear();
  Kotonoha_KtrfError error{};
  Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
  Check(bridge.Back(&result, &error), error);
  if (result.kind == Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene)
    openScene(result.scene);
  else setStatus("Beginning of available route history");
}

void Runtime::seek(Kotonoha_SceneTick target) {
  playing.clear();
  caption = choice = nullptr;
  fadeScene->clear();
  background.clear();
  timeline.Restart();
  elapsed = target / 24.0;
  tick = target;
  for (auto *event : timeline.AdvanceTo(target))
    if (event->endTick > target || event->command == CREATE_BG)
      dispatch(*event);
  lastClock = SDL_GetTicks();
}

void Runtime::update() {
  const Uint64 now = SDL_GetTicks();
  const double delta = (now - lastClock) / 1000.0;
  lastClock = now;
  if (currentState == Kotonoha::GameState::Splash) {
    const auto active = GetActiveScenes();
    for (auto *scene : active) scene->update();
    const bool logo = std::find(active.begin(), active.end(), logoSplash.get()) != active.end();
    if (!logo && engineSplash && engineSplash->finished()) {
      RemoveScene(engineSplash.get());
      AddScene(logoSplash.get(), Kotonoha::GameState::Splash);
    } else if (logo && logoSplash->finished()) {
      finishSplash();
    }
    return;
  }
  gameScene->update();
  if (backlogFallback->isVisible()) backlogFallback->update();
  if (systemUi.IsOpen()) {
    if (systemUi.IsSaveLoadOpen() && saveLoadPage != systemUi.SaveLoad().Page()) refreshSaveMetadata();
    if (systemUi.IsRouteMapOpen() && (routeMapMajor != systemUi.RouteMap().Major() ||
                                      routeMapSubpage != systemUi.RouteMap().Subpage()))
      refreshRouteMap();
    Ui::Output output;
    systemUi.Update(static_cast<uint32_t>(now), 0, 0, &output);
    handlePanel(output);
  }
  sounds.erase(std::remove_if(sounds.begin(), sounds.end(),
      [](const auto &sound) { return sound->finished(); }), sounds.end());
  const bool stopped = modal();
  if (titleBgm) titleBgm->setPaused(stopped && currentState != Kotonoha::GameState::Menu);
  if (currentState != Kotonoha::GameState::Gameplay) return;
  const bool halted = paused || stopped || bridge.HasPendingHandoff();
  for (auto &item : playing) item.player->setPaused(halted);
  if (halted) return;
  elapsed += delta * speed;
  tick = static_cast<Kotonoha_SceneTick>(elapsed * 24);
  for (auto *event : timeline.AdvanceTo(tick)) dispatch(*event);
  if (choice && tick >= choice->endTick) choose(-1);
  if (timeline.ReadyToEnd(tick)) advance();
}

void Runtime::renderMenu(Kotonoha::RendererRef renderer, float) {
  const SDL_FRect area{0, 0, 1280, 720};
  if (!titleBackground.empty()) {
    try { SDL_RenderTexture(&renderer, &assets->texture(renderer, titleBackground), nullptr, &area); }
    catch (const std::exception &) { titleBackground.clear(); }
  }
  if (titleBgm) {
    titleBgm->update(renderer);
    if (titleBgm->finished())
      titleBgm->open(*assets, assets->resolve(titleProfile.titleBgmStem + "_loop.ogg"), true, "BGM");
  }
  if (!systemUi.IsOpen() && titleArtwork) try {
    for (const auto &sprite : Kotonoha::SchoolDaysTitleRenderPlan::Build(title)) {
      const auto stem = sprite.texture == Kotonoha::SchoolDaysTitleRenderPlan::Texture::Overlay
          ? Kotonoha::SchoolDaysTitleModel::ResourceStem(title.CurrentFamily())
          : Kotonoha::SchoolDaysTitleModel::ChipStem(title.CurrentFamily());
      const auto path = assets->resolve(stem, true);
      const SDL_FRect destination{sprite.destination.x, sprite.destination.y,
                                 sprite.destination.w, sprite.destination.h};
      const SDL_FRect source{sprite.source.x, sprite.source.y, sprite.source.w, sprite.source.h};
      SDL_RenderTexture(&renderer, &assets->texture(renderer, path),
                        sprite.hasSource ? &source : nullptr, &destination);
    }
  } catch (const std::exception &error) {
    SDL_LogWarn(Kotonoha::LogUI, "School Days title: %s", error.what());
    titleArtwork = false;
  }
  if (!titleArtwork && !panelOpen()) {
    auto &ui = engine->GetUI();
    ui.panel(renderer, {360, 140, 560, 440});
    ui.text(renderer, "School Days HQ", 400, 175, 480);
    constexpr const char *labels[]{"New game", "Load game", "Configuration", "Quit"};
    for (int i = 0; i < 4; ++i)
      ui.button(renderer, {400, 250.0f + i * 70, 480, 55}, labels[i], titleSelection == i);
  }
  renderPanels(renderer);
}

void Runtime::renderGameplay(Kotonoha::RendererRef renderer, float) {
  const SDL_FRect area{0, 0, 1280, 720};
  if (!background.empty()) {
    try { SDL_RenderTexture(&renderer, &assets->texture(renderer, background), nullptr, &area); }
    catch (const std::exception &error) { setStatus(error.what()); background.clear(); }
  }
  const bool halted = paused || modal() || bridge.HasPendingHandoff();
  for (auto it = playing.begin(); it != playing.end();) {
    if (tick >= it->event->endTick) { it = playing.erase(it); continue; }
    auto &item = *it++;
    try {
      if (!halted) item.player->update(renderer, std::max(0.0, elapsed - item.segmentStart));
      item.player->render(renderer, area);
      if (!item.loop.empty() && item.player->finished()) {
        const auto channel = item.player->getChannel();
        item.player->open(*assets, assets->resolve(item.loop), true,
                          channel ? channel->getName() : "BGM");
        item.looping = true;
        item.segmentStart = elapsed;
      }
    } catch (const std::exception &error) { setStatus(error.what()); item.player->stop(); }
  }
  fadeScene->setPosition(elapsed);
  renderScene(*fadeScene, renderer, 1);
  // Retail TextView=Off hides the message layer.
  if (config.textView) {
    subtitles->setPosition(elapsed);
    renderScene(*subtitles, renderer, 1);
  }
  renderScene(*gameScene, renderer, 1);
  renderPanels(renderer);
}

void Runtime::renderPanels(Kotonoha::RendererRef renderer) {
  for (auto &sound : sounds) sound->update(renderer);
  if (systemUi.IsOpen()) {
    std::string failure;
    if (!systemUi.Render(&renderer, &failure)) {
      SDL_LogWarn(Kotonoha::LogUI, "School Days panel: %s", failure.c_str());
      systemUi.Close();
      setStatus("School Days panel failed: " + failure);
    }
  }
  if (backlogModel.IsOpen()) {
    std::string failure;
    if (!backlogPresenter.Render(&renderer, backlogModel, &failure)) {
      SDL_LogWarn(Kotonoha::LogUI, "School Days BackLog: %s", failure.c_str());
      backlogModel.Close();
      backlogPresenter.Close();
      backlogFallback->open();
    }
  }
  if (saveMenu->isOpen()) renderScene(*saveMenu, renderer, 1);
  if (backlogFallback->isVisible()) renderScene(*backlogFallback, renderer, 1);
  renderStatus(renderer);
}

void Runtime::renderStatus(Kotonoha::RendererRef renderer) {
  if (status.empty()) return;
  if (SDL_GetTicks() - statusTime > StatusMilliseconds) {
    status.clear();
    return;
  }
  auto &ui = engine->GetUI();
  const float height = ui.textHeight(renderer, status, 1200) + 20;
  ui.panel(renderer, {30, 690 - height, 1220, height});
  ui.text(renderer, status, 40, 700 - height, 1200);
}

void Runtime::onEvent(Kotonoha::EventRef event) {
  if (currentState == Kotonoha::GameState::Splash) {
    Kotonoha::Runtime::onEvent(event);
    return;
  }
  const bool escape = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                      event.key.key == SDLK_ESCAPE;
  if (saveMenu->isOpen()) {
    dispatchEvent(*saveMenu, event);
    return;
  }
  if (backlogFallback->isVisible()) {
    dispatchEvent(*backlogFallback, event);
    return;
  }
  if (backlogModel.IsOpen()) {
    if (escape) {
      playSound(Ui::UiSfxCue::Cancel);
      closePanels();
    } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
      backlogModel.SetSelectionFromPointer(int(event.motion.x), int(event.motion.y));
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
      Kotonoha::SchoolDaysBackLogModel::Action action;
      if (backlogModel.ActivatePointer(int(event.button.x), int(event.button.y), &action) &&
          action.kind == Kotonoha::SchoolDaysBackLogModel::ActionKind::Close) {
        playSound(Ui::UiSfxCue::Cancel);
        closePanels();
      }
    }
    return;
  }
  if (systemUi.IsOpen()) {
    if (escape) {
      playSound(Ui::UiSfxCue::Cancel);
      if (systemUi.IsRouteMapOpen()) openSaveLoad(false);
      else closePanels();
      return;
    }
    Ui::Output output;
    try {
      if (systemUi.HandleEvent(&engine->GetRenderer(), &event, &output)) handlePanel(output);
    } catch (const std::exception &error) { setStatus(error.what()); }
    return;
  }
  if (modal()) return;
  try {
    if (currentState == Kotonoha::GameState::Gameplay && gameScene->handleEvent(event))
      return;
    if (currentState == Kotonoha::GameState::Menu && !titleArtwork) {
      if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if (event.key.key == SDLK_UP) titleSelection = (titleSelection + 3) % 4;
        if (event.key.key == SDLK_DOWN) titleSelection = (titleSelection + 1) % 4;
      }
      for (int i = 0; i < 4; ++i)
        if (Kotonoha::UI::clicked(event, {400, 250.0f + i * 70, 480, 55}) ||
            (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_RETURN &&
             !event.key.repeat && titleSelection == i)) {
          if (i == 0) reset();
          if (i == 1) openSaveLoad(false);
          if (i == 2) openConfig(Kotonoha::SchoolDaysConfigMenuModel::Page::Def);
          if (i == 3) openExit();
          return;
        }
    } else if (currentState == Kotonoha::GameState::Menu && title.IsOpen()) {
      if (event.type == SDL_EVENT_MOUSE_MOTION)
        title.SetSelectionFromPointer(int(event.motion.x), int(event.motion.y));
      if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
        title.SetSelectionFromPointer(int(event.button.x), int(event.button.y));
        Kotonoha::SchoolDaysTitleModel::Activation action;
        if (title.ActivateSelection(&action)) {
          using Result = Kotonoha::SchoolDaysTitleModel::Result;
          playSound(Ui::UiSfxCue::Click);
          if (action.result == Result::Start) reset();
          else if (action.result == Result::Load) openSaveLoad(false);
          else if (action.result == Result::Config) openConfig(Kotonoha::SchoolDaysConfigMenuModel::Page::Def);
          else if (action.result == Result::Replay)
            setStatus("The retail replay gallery is not ported yet; use the route map from Load.");
          else if (action.result == Result::Exit) openExit();
        }
      }
    }
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) return;
    if (event.key.key == SDLK_ESCAPE) {
      openConfig(Kotonoha::SchoolDaysConfigMenuModel::Page::Def);
      return;
    }
    if (currentState != Kotonoha::GameState::Gameplay) return;
    if (event.key.key == SDLK_SPACE) paused = !paused;
    if (event.key.key == SDLK_F5) saveGame((documents / "QuickSave").string());
    if (event.key.key == SDLK_F6) loadGame((documents / "QuickSave").string());
    if (event.key.key == SDLK_LEFT) back();
    if (event.key.key == SDLK_RIGHT)
      if (auto target = timeline.ComputeSkipToEndTarget(tick)) seek(*target);
    if (choice && event.key.key >= SDLK_1 && event.key.key <= SDLK_9) {
      const int value = int(event.key.key - SDLK_1);
      if (value < int(choice->data.set_select->size)) choose(value);
    }
  } catch (const std::exception &error) {
    setStatus(error.what());
  }
}

void Runtime::activateControl(int control) {
  if (control == 1) paused = !paused;
  else if (control == 2) back();
  else if (control == 3) {
    if (auto target = timeline.ComputeSkipToEndTarget(tick)) seek(*target);
  } else if (control == 4) {
    for (auto *event = script.data; event; event = event->next)
      if (event->command == SetSELECT && event->startTick > tick) {
        seek(event->startTick);
        return;
      }
    if (auto target = timeline.NextTick()) seek(*target);
  } else if (control == 10 || control == 11) {
    openSaveLoad(control == 10 && currentState == Kotonoha::GameState::Gameplay);
  } else if (control == 12) openBacklog();
  else if (control == 13) openConfig(Kotonoha::SchoolDaysConfigMenuModel::Page::Def);
  else if (control == 15) openRouteMap();
  else if (control == 16) openExit();
  else if (control == 14) {
    playing.clear();
    Kotonoha_KtrfError error{};
    Check(bridge.ReleaseCurrentGameplayForFrontend(&error), error);
    script = {};
    flushGlobal();
    openTitle();
  }
}

void Runtime::openSaveLoad(bool save) {
  closePanels();
  saveLoadSaving = save;
  std::string failure;
  if (storage.IsInitialized() &&
      systemUi.OpenSaveLoad(&engine->GetRenderer(), assets->root().string(), WideFull,
                            save ? Kotonoha::SchoolDaysSaveLoadModel::Mode::Save
                                 : Kotonoha::SchoolDaysSaveLoadModel::Mode::Load,
                            profile.textInput, profile.useEnglish, profile.fontData, &failure)) {
    playSound(Ui::UiSfxCue::View);
    refreshSaveMetadata();
    return;
  }
  if (!failure.empty()) SDL_LogWarn(Kotonoha::LogUI, "School Days SaveLoad: %s", failure.c_str());
  const auto directory = documents / "Saves";
  std::filesystem::create_directories(directory);
  saveMenu->open(directory.string(), !save);
}

void Runtime::refreshSaveMetadata() {
  if (!systemUi.IsSaveLoadOpen()) return;
  const int page = systemUi.SaveLoad().Page();
  std::vector<Kotonoha::SchoolDaysSaveService::SlotMetadata> rows(10u);
  for (int row = 0; row < 10; ++row) {
    bool openable = false;
    std::string failure;
    Kotonoha_KtrfError error{};
    // One unreadable slot must not hide the rest of the page.
    if (!storage.ProbeSaveFile(page * 10 + row, &openable, &failure) ||
        !Kotonoha::SchoolDaysSaveService::ReadSlotMetadata(
            profile, page * 10 + row, openable, bridge.Session().Persistence(),
            &rows[size_t(row)], &error))
      rows[size_t(row)] = {};
  }
  std::string failure;
  if (!systemUi.SetSaveLoadMetadata(rows, &failure))
    SDL_LogWarn(Kotonoha::LogUI, "School Days save metadata: %s", failure.c_str());
  saveLoadPage = page;
}

void Runtime::saveSlot(int slot, const std::u16string &comment) {
  if (currentState != Kotonoha::GameState::Gameplay) throw std::runtime_error("No game to save");
  auto &session = bridge.Session();
  SDL_Time now = 0;
  SDL_DateTime date{};
  if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &date, true))
    throw std::runtime_error(SDL_GetError());
  const Kotonoha::SchoolDaysSaveMetadata::LocalTime time{
      date.year, date.month, date.day, date.day_of_week, date.hour, date.minute};
  Kotonoha::SchoolDaysSaveMetadata::Result metadata;
  Kotonoha_KtrfError error{};
  Check(Kotonoha::SchoolDaysSaveMetadata::Build(session.State(), time, profile.useEnglish, comment,
                                                &metadata, &error), error);
  Kotonoha::SchoolDaysSaveService::Plan plan;
  Check(Kotonoha::SchoolDaysSaveService::PlanSlot(profile, slot, metadata.primary, metadata.sub,
                                                  session.Persistence(), session.State(), &plan,
                                                  &error), error);
  if (bridge.HasPendingHandoff()) {
    // A pending handoff keeps the script snapshot taken at the scene boundary.
    plan.saveFileBytes.clear();
    Check(session.Persistence().SerializeSaveLogWithLiveChoiceCommitMarker(
        false, &plan.saveFileBytes, &error), error);
  }
  std::string failure;
  Check(storage.WriteSaveFile(plan, &failure), failure, "School Days save write failed");
  Check(Kotonoha::SchoolDaysSaveService::Commit(plan, &session.Persistence(), &error), error);
  Check(storage.WriteGlobalFlag(plan, &failure), failure, "School Days GlobalFlag write failed");
}

void Runtime::loadSlot(int slot) {
  std::vector<uint8_t> bytes;
  std::string failure;
  Check(storage.ReadSaveFile(slot, &bytes, &failure), failure, "School Days save read failed");
  playing.clear();
  Kotonoha_KtrfError error{};
  Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
  Check(bridge.LoadSave(bytes, &result, &error), error);
  closePanels();
  openScene(result.scene);
}

Kotonoha::SchoolDaysConfigMenuRenderPlan::VisualState Runtime::configVisualState() const {
  Kotonoha::SchoolDaysConfigMenuRenderPlan::VisualState state;
  state.defWideCurrent = 4; // NeoStart always presents the 16:9 layout.
  state.defFullCurrent = engine->GetSettings().mode == Kotonoha::WindowMode::Windowed ? 7 : 6;
  state.defSkipCurrent = config.skip ? 8 : 9;
  state.defSuperSkipCurrent = config.superSkip ? 10 : 11;
  state.defDrawMessageCurrent = config.textView ? 12 : 13;
  state.soundMenVoiceCurrent = config.menVoice ? 10 : 11;
  state.soundMuteCurrent = engine->GetSettings().masterMuted ? 12 : 13;
  state.soundLevels = {Policy::Level(config, Policy::NativeAudioBank::Bgm),
                       Policy::Level(config, Policy::NativeAudioBank::Se),
                       Policy::Level(config, Policy::NativeAudioBank::Voice)};
  return state;
}

void Runtime::openConfig(Kotonoha::SchoolDaysConfigMenuModel::Page page) {
  closePanels();
  std::string failure;
  if (systemUi.OpenConfig(&engine->GetRenderer(), assets->root().string(),
                          Kotonoha::SchoolDaysConfigMenuModel::Variant::WideFull, page,
                          configVisualState(), &failure)) {
    playSound(Ui::UiSfxCue::View);
    return;
  }
  SDL_LogWarn(Kotonoha::LogUI, "School Days ConfigMenu: %s", failure.c_str());
  engine->openSettings();
}

void Runtime::handleConfig(const Kotonoha::SchoolDaysConfigMenuModel::Action &action) {
  using Kind = Kotonoha::SchoolDaysConfigMenuModel::ActionKind;
  switch (action.kind) {
  case Kind::Close:
    closePanels();
    return;
  case Kind::SelectPage:
    openConfig(action.page);
    return;
  case Kind::SetMenVoice: config.menVoice = action.value != 0; break;
  case Kind::SetSkip: config.skip = action.value != 0; break;
  case Kind::SetSuperSkip: config.superSkip = action.value != 0; break;
  case Kind::SetDrawMessage: config.textView = action.value != 0; break;
  case Kind::SetMute: {
    // Retail Mute is the engine's master mute.
    auto settings = engine->GetSettings();
    settings.masterMuted = action.value != 0;
    engine->applySettings(settings);
    break;
  }
  case Kind::StepSoundBank:
  case Kind::SetSoundLevel: {
    if (action.soundBank < 0 || action.soundBank > 2) return;
    const auto bank = static_cast<Policy::UiAudioBank>(action.soundBank);
    if (action.kind == Kind::StepSoundBank) Policy::StepUiLevel(&config, bank, action.choice);
    else Policy::SetUiLevel(&config, bank, action.value);
    break;
  }
  case Kind::RequestFull: {
    auto settings = engine->GetSettings();
    settings.mode = action.value ? Kotonoha::WindowMode::Borderless : Kotonoha::WindowMode::Windowed;
    engine->applySettings(settings);
    break;
  }
  case Kind::RequestWide: // Only the 16:9 layout exists in this port.
  default:
    break;
  }
  applyConfig();
  storeConfig();
  systemUi.SetConfigVisualState(configVisualState());
}

void Runtime::applyConfig() {
  if (!engine) return;
  auto &audio = engine->GetAudio();
  audio.channel("BGM")->setVolume(Policy::LinearGain(config, Policy::NativeAudioBank::Bgm));
  audio.channel("SE")->setVolume(Policy::LinearGain(config, Policy::NativeAudioBank::Se));
  audio.channel("Voice")->setVolume(Policy::LinearGain(config, Policy::NativeAudioBank::Voice));
  config.mute = engine->GetSettings().masterMuted;
}

void Runtime::loadConfig() {
  std::ifstream file(documents / ConfigFile);
  std::string line;
  auto next = config;
  while (std::getline(file, line)) {
    const auto split = line.find('=');
    if (split == std::string::npos) continue;
    const auto key = line.substr(0, split);
    int value = 0;
    try { value = std::stoi(line.substr(split + 1)); } catch (const std::exception &) { continue; }
    if (key == "BgmVolume") next.bgmVolume = value;
    else if (key == "SeVolume") next.seVolume = value;
    else if (key == "VoiceVolume") next.voiceVolume = value;
    else if (key == "TextView") next.textView = value != 0;
    else if (key == "MenVoice") next.menVoice = value != 0;
    else if (key == "Skip") next.skip = value != 0;
    else if (key == "SuperSkip") next.superSkip = value != 0;
    else if (key == "AutoMode") next.autoMode = value != 0;
  }
  if (Policy::Validate(next)) config = next;
}

void Runtime::storeConfig() const {
  std::ostringstream out;
  out << "BgmVolume=" << config.bgmVolume << "\nSeVolume=" << config.seVolume
      << "\nVoiceVolume=" << config.voiceVolume << "\nTextView=" << config.textView
      << "\nMenVoice=" << config.menVoice << "\nSkip=" << config.skip
      << "\nSuperSkip=" << config.superSkip << "\nAutoMode=" << config.autoMode << '\n';
  try { Kotonoha::writeTextFileAtomically((documents / ConfigFile).string(), out.str()); }
  catch (const std::exception &error) { SDL_LogWarn(Kotonoha::LogUI, "%s", error.what()); }
}

void Runtime::openRouteMap() {
  closePanels();
  auto &state = bridge.Session().State();
  Kotonoha_KtrfError error{};
  int64_t route = 0, endClear = 0;
  int major = 0, subpage = 0, percent = 0;
  if (state.ReadSessionInt("ROUTE", &route, &error) &&
      state.ReadGlobalInt("EndClear", &endClear, &error)) {
    // Native RouteMap init: GetStory()-1, its page, then the -1 -> 0 clamp.
    const int raw = Kotonoha::SchoolDaysSaveMetadata::StoryFor(route, endClear) - 1;
    Kotonoha_KtrfError pageError{};
    subpage = Kotonoha::SchoolDaysRouteMapOracle::GetRouteMapPage(state, raw, &pageError);
    major = std::clamp(raw, 0, 5);
    if (pageError.code != KOTONOHA_KTRF_OK || subpage < 0 ||
        subpage >= Kotonoha::SchoolDaysRouteMapModel::SubpageCountForMajor(major))
      subpage = 0;
  }
  Kotonoha::SchoolDaysRouteMapOracle::Completion completion;
  if (Kotonoha::SchoolDaysRouteMapOracle::ComputeCompletion(
          state, bridge.Session().Adapter()->document, &completion, &error))
    percent = completion.percent;
  std::string failure;
  if (!systemUi.OpenRouteMap(&engine->GetRenderer(), assets->root().string(), WideFull, major,
                             subpage, false, percent, profile.useEnglish, profile.fontData,
                             &failure)) {
    setStatus("Route map unavailable: " + failure);
    return;
  }
  playSound(Ui::UiSfxCue::View);
  refreshRouteMap();
}

void Runtime::refreshRouteMap() {
  if (!systemUi.IsRouteMapOpen()) return;
  const int major = systemUi.RouteMap().Major(), subpage = systemUi.RouteMap().Subpage();
  const auto page = Kotonoha::SchoolDaysRouteMapModel::Page(major, subpage);
  const auto &state = bridge.Session().State();
  for (int i = 0; i < page.nodeCount; ++i) {
    int64_t value = 0;
    Kotonoha_KtrfError error{};
    const auto key = Kotonoha::SchoolDaysRouteMapModel::NodeSpKey(major, subpage, i);
    systemUi.SetRouteMapNodeAvailable(i, state.ReadSessionInt(key.c_str(), &value, &error) && value);
  }
  int marker = -1;
  Kotonoha_KtrfError error{};
  if (Kotonoha::SchoolDaysRouteMapOracle::GetRouteMapPage(state, major, &error) == subpage &&
      error.code == KOTONOHA_KTRF_OK) {
    const int check = Kotonoha::SchoolDaysRouteMapOracle::CheckScript(state, major, &error);
    if (error.code == KOTONOHA_KTRF_OK) marker = check - page.nodeOffset;
  }
  systemUi.SetRouteMapCurrentMarker(marker);
  routeMapMajor = major;
  routeMapSubpage = subpage;
}

void Runtime::openExit() {
  closePanels();
  std::string failure;
  if (systemUi.OpenExit(&engine->GetRenderer(), assets->root().string(),
                        Kotonoha::SchoolDaysExitViewModel::Variant::WideFull,
                        Kotonoha::SchoolDaysExitViewModel::BaseArtwork::Exit, &failure)) {
    playSound(Ui::UiSfxCue::View);
    return;
  }
  SDL_Event quit{};
  quit.type = SDL_EVENT_QUIT;
  SDL_PushEvent(&quit);
}

void Runtime::openBacklog() {
  closePanels();
  std::string failure;
  TtfFont font;
  try {
    font.font = TTF_OpenFont(Kotonoha::FileResolver::font(engine->GetSettings().fontPath).string().c_str(),
                             Kotonoha::SchoolDaysSubtitleTypography::PointSize);
  } catch (const std::exception &error) { failure = error.what(); }
  // The authenticated retail text layout is the English horizontal BackLog.
  if (profile.useEnglish && font.font &&
      backlogModel.Open(assets->root().string(), backlogStore,
                        Kotonoha::SchoolDaysBackLogModel::Orientation::Horizon,
                        Kotonoha::SchoolDaysBackLogModel::Variant::WideFull, &failure) &&
      backlogPresenter.Open(&engine->GetRenderer(), assets->root().string(),
                            backlogModel.CurrentOrientation(), true, font.font, nullptr, &failure)) {
    playSound(Ui::UiSfxCue::View);
    return;
  }
  if (!failure.empty()) SDL_LogWarn(Kotonoha::LogUI, "School Days BackLog: %s", failure.c_str());
  backlogModel.Close();
  backlogPresenter.Close();
  backlogFallback->open();
}

void Runtime::handlePanel(const Ui::Output &output) {
  playSound(output.uiSfx);
  if (output.hasConfigAction) handleConfig(output.configAction);
  if (output.hasExitActivation) {
    if (output.exitActivation.outcome == Kotonoha::SchoolDaysExitViewModel::Outcome::ConfirmExit) {
      SDL_Event quit{};
      quit.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&quit);
    }
    closePanels();
  }
  if (output.hasRouteMapAction) {
    using Kind = Kotonoha::SchoolDaysRouteMapModel::ActionKind;
    if (output.routeMapAction.kind == Kind::ReturnToLoad) openSaveLoad(false);
    else if (output.routeMapAction.kind == Kind::NodeRequested) {
      closePanels();
      loadCheckpoint(output.routeMapAction.selector);
    }
  }
  if (output.hasSaveLoadOutput) {
    using Action = Kotonoha::SchoolDaysSaveLoadModel::Action;
    const auto &result = output.saveLoadOutput;
    switch (result.action) {
    case Action::Close: closePanels(); break;
    case Action::RouteMap: openRouteMap(); break;
    case Action::LoadSlotRequested: loadSlot(result.slot); break;
    case Action::CommentInputRequested:
      // No text-entry overlay: the slot is saved with an empty comment.
      systemUi.SubmitSaveLoadComment({});
      break;
    case Action::SaveSlotRequested:
      try {
        saveSlot(result.slot, result.comment);
        setStatus("Saved to slot " + std::to_string(result.slot + 1));
      } catch (const std::exception &error) { setStatus(error.what()); }
      saveLoadPage = -1;
      break;
    default: break;
    }
  }
}

void Runtime::playSound(Ui::UiSfxCue cue) {
  if (!engine || !hasProfile || cue == Ui::UiSfxCue::None) return;
  const std::string *stem = nullptr;
  switch (cue) {
  case Ui::UiSfxCue::Cancel: stem = &profile.seCancel; break;
  case Ui::UiSfxCue::Select: stem = &profile.seSelect; break;
  case Ui::UiSfxCue::Click: stem = &profile.seClick; break;
  case Ui::UiSfxCue::Up: stem = &profile.seUp; break;
  case Ui::UiSfxCue::Down: stem = &profile.seDown; break;
  case Ui::UiSfxCue::View: stem = &profile.seView; break;
  case Ui::UiSfxCue::Open: stem = &profile.seOpen; break;
  default: return;
  }
  if (stem->empty()) return;
  try {
    auto sound = std::make_unique<Kotonoha::MediaPlayer>(engine->GetAudio());
    sound->open(*assets, assets->resolve(*stem + ".ogg"), true, "SE");
    sounds.push_back(std::move(sound));
  } catch (const std::exception &error) {
    SDL_LogWarn(Kotonoha::LogUI, "School Days system sound %s: %s", stem->c_str(), error.what());
  }
}

void Runtime::loadCheckpoint(int selector) {
  playing.clear();
  Kotonoha_KtrfError error{};
  Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
  Check(bridge.LoadRouteMapSelector(selector, &result, &error), error);
  backlogStore.ResetTransient();
  backlogFallback->clear();
  backlogIndex = 0;
  backlogScene.clear();
  openScene(result.scene);
}

void Runtime::flushGlobal() {
  if (documents.empty() || !bridge.Session().Persistence().HasGlobalFlag()) return;
  std::vector<uint8_t> bytes;
  Kotonoha_KtrfError error{};
  Check(bridge.Session().SerializeGlobalFlag(&bytes, &error), error);
  std::string failure;
  if (storage.IsInitialized())
    Check(storage.WriteGlobalFlagBytes(bytes, &failure), failure, "School Days GlobalFlag write failed");
  else
    WriteBytes((documents / "GlobalFlag").string(), bytes);
}

void Runtime::saveGame(const std::string &path) {
  Kotonoha_KtrfError error{};
  auto &session = bridge.Session();
  if (!bridge.HasPendingHandoff())
    Check(session.Persistence().RefreshCurrentScriptSnapshot(session.State(), &error), error);
  std::vector<uint8_t> bytes;
  Check(session.Persistence().SerializeSaveLogWithLiveChoiceCommitMarker(
      !bridge.HasPendingHandoff() && committed, &bytes, &error), error);
  WriteBytes(path, bytes);
  flushGlobal();
}

void Runtime::loadGame(const std::string &path) {
  playing.clear();
  Kotonoha_KtrfError error{};
  Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
  Check(bridge.LoadSave(assets->read(path), &result, &error), error);
  closePanels();
  openScene(result.scene);
}

std::string Runtime::debugNode() const { return bridge.CurrentScene().sceneKey; }

std::vector<Kotonoha::DebugNode> Runtime::debugNodes() const {
  const auto *document = bridge.Session().Adapter()->document;
  std::vector<Kotonoha::DebugNode> nodes;
  const auto *section = Kotonoha_KtrfFindSection(document, "NODE");
  Kotonoha_KtrfError error{};
  if (!section) return nodes;
  for (uint32_t i = 0; i < section->item_count; ++i) {
    Kotonoha_KtrfNode node{};
    Check(Kotonoha_KtrfGetNode(document, i, &node, &error), error);
    const char *label = nullptr;
    size_t size = 0;
    Check(Kotonoha_KtrfGetString(document,
          node.label_str == KOTONOHA_KTRF_NULL_INDEX ? node.id_str : node.label_str,
          &label, &size, &error), error);
    Kotonoha::DebugNode output{std::to_string(i), std::string(label, size), {}};
    Kotonoha::SchoolDaysKtrfScene resolved;
    if (bridge.Session().ResolveSceneNode(i, &resolved, &error))
      output.assets.push_back(resolved.orsPath);
    nodes.push_back(std::move(output));
  }
  return nodes;
}

void Runtime::debugJumpToNode(const std::string &id) {
  Kotonoha_KtrfError error{};
  uint32_t index = 0;
  if (!id.empty() && id.find_first_not_of("0123456789") == std::string::npos) {
    const auto parsed = std::stoull(id);
    if (parsed > UINT32_MAX) throw std::out_of_range("School Days node index");
    index = uint32_t(parsed);
  } else {
    Kotonoha::SchoolDaysKtrfScene resolved;
    Check(bridge.Session().ResolveSceneKey(id, &resolved, &error), error);
    index = resolved.nodeIndex;
  }
  playing.clear();
  Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
  Check(bridge.DebugJumpToNode(index, &result, &error), error);
  openScene(result.scene);
}

void Runtime::loadKtrf(const std::string &path) {
  // Validate and probe first so a malformed document cannot destroy the live route.
  Kotonoha::SchoolDaysKtrfSession candidate;
  Kotonoha_KtrfError error{};
  Check(candidate.Open(path.c_str(), assets->orsRoot().string().c_str(), &error), error);
  candidate.Close();
  std::vector<uint8_t> global;
  const bool hasGlobal = bridge.Session().Persistence().HasGlobalFlag();
  if (hasGlobal) Check(bridge.Session().SerializeGlobalFlag(&global, &error), error);
  flushGlobal();
  playing.clear();
  Check(bridge.Open(path.c_str(), assets->orsRoot().string().c_str(), nullptr,
                    {createPresentation, destroyPresentation, this}, &error), error);
  if (hasGlobal) Check(bridge.Session().LoadGlobalFlag(global, &error), error);
  routePath = path;
  openScene(bridge.CurrentScene());
}

void Runtime::dumpKtrf(const std::string &path) const {
  const auto *document = bridge.Session().Adapter()->document;
  if (!document) throw std::runtime_error("No School Days KTRF loaded");
  Kotonoha::writeTextFileAtomically(path,
      std::string(reinterpret_cast<const char *>(document->bytes), document->size));
}

} // namespace SchoolDays
