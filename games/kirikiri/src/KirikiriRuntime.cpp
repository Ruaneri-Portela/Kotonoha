#include <Kirikiri/KirikiriRuntime.hpp>
#include <Kirikiri/TjsBootstrap.hpp>
#include <Kirikiri/TjsStartupVm.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/components/ChoiceQuestionScene.hpp>
#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/components/VNManager.hpp>
#include <Kotonoha/components/VNSaveMenu.hpp>
#include <Kotonoha/components/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>

namespace Kirikiri {

namespace {

std::string scenarioTitle(const std::string &fileName) {
  auto p = std::filesystem::path(fileName);
  return p.stem().string();
}

// KAG storage names often omit the extension and directory. Resolve only
// formats the provider can supply, rather than constructing filesystem paths.
std::string resolveVisual(const Kotonoha::Providers::KirikiriAssets &assets,
                          std::string storage, bool foreground) {
  std::replace(storage.begin(), storage.end(), '\\', '/');
  if (storage.empty() || storage.front() == '/' ||
      storage.find("://") != std::string::npos)
    return {};
  // Operate on UTF-8 bytes directly; std::filesystem::path(string) may
  // reinterpret Japanese storage names on Windows.
  for (std::size_t start = 0; start < storage.size();) {
    const auto end = storage.find('/', start);
    if (storage.substr(start, end == std::string::npos ? std::string::npos
                                                       : end - start) == "..")
      return {};
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  const auto slash = storage.find_last_of('/');
  const auto dot = storage.find_last_of('.');
  const bool hasExtension =
      dot != std::string::npos && (slash == std::string::npos || dot > slash);
  const std::vector<std::string> folders =
      foreground
          ? std::vector<std::string>{"", "fgimage/", "image/"}
          : std::vector<std::string>{"", "bgimage/", "image/", "system/"};
  for (const auto &folder : folders) {
    const auto base = folder + storage;
    for (const char *extension :
         {"", ".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tlg"}) {
      if (hasExtension && *extension)
        continue;
      const auto candidate = base + extension;
      if (assets.contains(candidate))
        return candidate;
    }
  }
  return {};
}

// This is only an optional illustration behind the fallback KAG menu.
// It does not pretend to execute startup.tjs or reproduce a title's TJS UI.
std::string
findTitlePicture(const Kotonoha::Providers::KirikiriAssets &assets) {
  const auto all = assets.list();
  std::map<std::string, std::string> images;
  for (const auto &id : all) {
    std::string key = id;
    std::replace(key.begin(), key.end(), '\\', '/');
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
      return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a')
                                  : static_cast<char>(c);
    });
    images.emplace(std::move(key), id);
  }
  for (const char *base :
       {"image/title", "image/title_bg", "image/titlebase", "system/title",
        "system/title_bg", "bgimage/title", "title/titlebase", "title/title",
        "title", "menu/title", "image/menu"}) {
    for (const char *ext : {".png", ".jpg", ".jpeg", ".webp", ".bmp", ".tlg"}) {
      if (auto it = images.find(std::string(base) + ext); it != images.end())
        return it->second;
    }
  }
  return {};
}

} // namespace

struct KirikiriRuntime::Impl {
  KirikiriRuntime &runtime;
  std::shared_ptr<Kotonoha::Providers::KirikiriAssets> assets;
  Kotonoha::Engine *engine = nullptr;

  // Presentation state
  std::string currentScenario;
  std::unique_ptr<KagScript::Runner> runner;
  std::string dialogueSpeaker;
  std::string dialogueText;
  bool dialogueVisible = false;
  struct Visual {
    std::string id;
    int x = 0, y = 0;
    bool decodeFailed = false;
  };
  Visual titlePicture, background;
  std::map<std::string, Visual> characters;
  bool titleRestartRequested = false;

  // Scenes
  Kotonoha::VNManager *manager = nullptr;
  Kotonoha::VNTextBox *textbox = nullptr;
  Kotonoha::ChoiceQuestionScene *choiceScene = nullptr;
  Kotonoha::VNBacklog *backlog = nullptr;
  Kotonoha::InGameMenu *menu = nullptr;
  Kotonoha::VNSaveMenu *saveMenu = nullptr;

  bool running = false;
  bool titleShown = false;
  bool nativeTitleUi = false;
  bool nativeTitleDisabled = false;
  std::set<std::string> failedNativePictures;
  Kotonoha::Providers::TjsBootstrapReport bootstrap;
  Kotonoha::Providers::TjsStartupResult startupExecution;
  bool waitingChoice = false;
  bool waitingWait = false;
  std::uint64_t waitingDeadline = 0;
  std::function<void()> finishWait;
  Kotonoha::PcmPlayer bgm;
  Kotonoha::PcmPlayer sound;

  Impl(KirikiriRuntime &r,
       std::shared_ptr<Kotonoha::Providers::KirikiriAssets> a)
      : runtime(r), assets(std::move(a)) {}
};

KirikiriRuntime::KirikiriRuntime(
    std::shared_ptr<Kotonoha::Providers::KirikiriAssets> assets)
    : Runtime("Kirikiri", "0.1.0", "me.hirameki.kotonoha.kirikiri"),
      impl_(std::make_unique<Impl>(*this, std::move(assets))) {}

KirikiriRuntime::~KirikiriRuntime() = default;

void KirikiriRuntime::init(Kotonoha::Engine &engine) {
  auto &impl = *impl_;
  impl.engine = &engine;
  Runtime::init(engine);
}

void KirikiriRuntime::initGlobal(Kotonoha::Engine &engine) {
  auto &impl = *impl_;
  impl.manager = &Kotonoha::GetVNManagerRuntime(engine);
  impl.textbox = &Kotonoha::GetVNTextBoxRuntime(engine, "", "", 30);
  impl.choiceScene = &Kotonoha::GetChoiceQuestionRuntime(engine);
  impl.backlog = &Kotonoha::GetVNBacklogRuntime(engine);
  impl.menu = &Kotonoha::GetInGameMenuRuntime(engine);
  impl.saveMenu =
      nullptr; // KAG quicksaves use VNManager; no extra scene owned here.

  auto &menu = *impl.menu;
  menu.SetOnReturnToMenu([this] {
    // Recreate the KAG title after the menu event has been dispatched.
    // Switching to GameState::Menu alone left no scripted scenes active.
    impl_->menu->close();
    impl_->titleRestartRequested = true;
  });
  menu.SetOnSave([this] {
    auto path =
        Kotonoha::preferredDocumentsDirectory("kirikiri") / "quicksave.kns";
    saveGame(path.string());
  });
  menu.SetOnLoad([this] {
    auto path =
        Kotonoha::preferredDocumentsDirectory("kirikiri") / "quicksave.kns";
    loadGame(path.string());
  });
}

std::string KirikiriRuntime::diagnostic() const {
  const auto &state = *impl_;
  const auto &report = state.bootstrap;
  if (report.entry.empty())
    return "KiriKiri: startup.tjs não detectado; suporte KAG experimental";
  std::ostringstream message;
  message << "KiriKiri: " << report.entry << " (TJS2 não executado), "
          << report.staticallyReferenced.size() << " dependências estáticas, "
          << report.missing.size() << " ausentes, " << report.dynamicCalls
          << " referências dinâmicas";
  if (!report.missing.empty()) {
    message << "; primeiras ausentes: ";
    for (std::size_t i = 0; i < report.missing.size() && i < 3; ++i) {
      if (i)
        message << ", ";
      message << report.missing[i];
    }
  }
  if (!report.error.empty())
    message << "; erro de análise: " << report.error;
  const auto &run = state.startupExecution;
  if (!run.visitedScripts.empty())
    message << "; TJS2 subset percorreu " << run.visitedScripts.size()
            << " arquivos, " << run.statements << " instruções";
  if (!run.kagEntry.empty())
    message << "; roteiro KAG: " << run.kagEntry;
  if (!run.error.empty())
    message << "; limite da VM: " << run.error;
  else if (run.subsetCompleted && run.requiresNativeRuntime)
    message << "; requer UI/plugins nativos";
  return message.str();
}

void KirikiriRuntime::initSplash(Kotonoha::Engine &engine) {
  // Kirikiri games go directly to gameplay; no engine splash screen.
  SetState(Kotonoha::GameState::Gameplay);
  initGameplay(engine);
}

void KirikiriRuntime::initMenu(Kotonoha::Engine &engine) {
  auto &impl = *impl_;
  impl.menu->open();
  impl.menu->SetOnResume([this] {
    SetState(Kotonoha::GameState::Gameplay);
    init(*impl_->engine);
  });
  AddScene(impl.menu);
}

void KirikiriRuntime::initGameplay(Kotonoha::Engine &engine) {
  auto &impl = *impl_;
  SDL_Log("Kirikiri: initGameplay called");
  Kotonoha::GetVNBacklogRuntime(engine).clear();
  Kotonoha::GetVNBacklogRuntime(engine).close();

  // Rebuilding a title after a failed native UI must not register the same
  // scenes twice (duplicate input and rendering callbacks).
  const auto attachOnce = [&](Kotonoha::Scene *scene) {
    const auto &scenes = GetScenes(Kotonoha::GameState::Gameplay);
    if (std::find(scenes.begin(), scenes.end(), scene) == scenes.end())
      AddScene(scene);
  };
  attachOnce(impl.textbox);
  attachOnce(impl.choiceScene);
  attachOnce(impl.backlog);
  attachOnce(impl.menu);

  if (impl.runner && !impl.runner->isFinished()) {
    // Resume a running scenario.
    impl.running = true;
    impl.runner->run();
  } else if (impl.currentScenario.empty()) {
    // Present a real entry menu. Older builds jumped straight into the first
    // alphabetically sorted script, which skipped Kirikiri title flow and was
    // especially visible with NEKOPARA translation packs.
    auto scenarios = impl.assets->scenarioFiles();
    SDL_Log("Kirikiri: found %zu scenarios", scenarios.size());
    for (const auto &s : scenarios)
      SDL_Log("Kirikiri: scenario: %s", s.c_str());
    if (!impl.titleShown) {
      // Display the engine-owned fallback title even when a release has only
      // startup.tjs/compiled scripts. Previously an empty .ks list made the
      // game a blank screen with no explanation or accessible menu.
      impl.titleShown = true;
      impl.bootstrap = Kotonoha::Providers::inspectTjsBootstrap(*impl.assets);
      impl.startupExecution = {};
      impl.nativeTitleUi = false;
      impl.failedNativePictures.clear();
      if (!impl.bootstrap.entry.empty()) {
        impl.startupExecution =
            Kotonoha::Providers::runTjsStartupSubset(*impl.assets);
        const auto &run = impl.startupExecution;
        SDL_Log("KiriKiri: TJS2 subset visited %zu scripts, %zu statements, "
                "KAG entry='%s', completed=%d, native UI needed=%d",
                run.visitedScripts.size(), run.statements, run.kagEntry.c_str(),
                run.subsetCompleted ? 1 : 0, run.requiresNativeRuntime ? 1 : 0);
        if (!run.error.empty())
          SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                      "KiriKiri: TJS2 subset stopped: %s", run.error.c_str());
        SDL_Log("KiriKiri: found TJS2 entry %s; %zu static dependencies, "
                "%zu missing and %zu dynamic calls",
                impl.bootstrap.entry.c_str(),
                impl.bootstrap.staticallyReferenced.size(),
                impl.bootstrap.missing.size(), impl.bootstrap.dynamicCalls);
        for (const auto &id : impl.bootstrap.missing)
          SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                      "KiriKiri: startup TJS dependency unavailable: %s",
                      id.c_str());
      }
      if (!impl.bootstrap.error.empty())
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "KiriKiri: cannot inspect startup.tjs: %s",
                    impl.bootstrap.error.c_str());
      // Enable the script-authored title only when it has drawable,
      // interactive layers and the bounded source VM completed its entry.
      // Otherwise retain the accessible Kotonoha fallback menu.
      const auto &ui = impl.startupExecution;
      const bool imagePresent = std::any_of(
          ui.layers.begin(), ui.layers.end(),
          [&](const Kotonoha::Providers::TjsUiLayer &layer) {
            return layer.visible && !layer.storage.empty() &&
                   !resolveVisual(*impl.assets, layer.storage, true).empty();
          });
      const bool hasClickTarget =
          std::any_of(ui.layers.begin(), ui.layers.end(),
                      [](const Kotonoha::Providers::TjsUiLayer &layer) {
                        return layer.visible && layer.enabled &&
                               !layer.onClick.empty() &&
                               ((layer.width > 0 && layer.height > 0) ||
                                !layer.storage.empty());
                      });
      impl.nativeTitleUi = !impl.nativeTitleDisabled && ui.subsetCompleted &&
                           ui.error.empty() && imagePresent && hasClickTarget;
      SDL_Log("KiriKiri: TJS2 title projection: %zu layers, interactive=%d",
              ui.layers.size(), impl.nativeTitleUi ? 1 : 0);
      impl.titlePicture = {findTitlePicture(*impl.assets), 0, 0, false};
      if (impl.nativeTitleUi) {
        impl.choiceScene->hide();
        return;
      }
      const auto startup = !impl.startupExecution.kagEntry.empty()
                               ? impl.startupExecution.kagEntry
                               : impl.assets->startupScenario();
      if (!startup.empty() && startScenario(startup)) {
        impl.choiceScene->hide();
        return;
      }
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "KiriKiri: no supported KAG startup script found");
    }
  } else {
    startScenario(impl.currentScenario);
  }
}

void KirikiriRuntime::update() {
  Runtime::update();
  auto &impl = *impl_;
  if (impl.titleRestartRequested && impl.engine) {
    impl.titleRestartRequested = false;
    impl.runner.reset();
    impl.running = false;
    impl.titleShown = false;
    impl.currentScenario.clear();
    impl.nativeTitleUi = false;
    impl.nativeTitleDisabled = false;
    impl.startupExecution = {};
    impl.failedNativePictures.clear();
    impl.background = {};
    impl.characters.clear();
    impl.dialogueVisible = false;
    impl.waitingWait = impl.waitingChoice = false;
    SetState(Kotonoha::GameState::Gameplay);
    init(*impl.engine);
    return;
  }
  // An otherwise compatible script title can still contain an unsupported
  // image. If every visible picture failed to decode, bring back the
  // Kotonoha menu instead of leaving an invisible, unclickable title.
  if (impl.nativeTitleUi && impl.engine && impl.currentScenario.empty() &&
      !impl.failedNativePictures.empty()) {
    std::size_t pictures = 0, failed = 0;
    for (const auto &layer : impl.startupExecution.layers) {
      if (!layer.visible || layer.storage.empty())
        continue;
      ++pictures;
      if (impl.failedNativePictures.count(layer.storage))
        ++failed;
    }
    if (pictures > 0 && failed == pictures) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "KiriKiri: no script-created Layer image decoded, "
                  "returning to compatibility menu");
      impl.nativeTitleUi = false;
      impl.nativeTitleDisabled = true;
      impl.titleShown = false;
      impl.choiceScene->hide();
      initGameplay(*impl.engine);
      return;
    }
  }
  // Resume pending waits on the runtime thread, never an SDL timer worker.
  if (impl.waitingWait && SDL_GetTicks() >= impl.waitingDeadline) {
    impl.waitingWait = false;
    auto done = std::move(impl.finishWait);
    impl.finishWait = {};
    if (done)
      done();
  }
  if (impl.engine && impl.running && !impl.runner->isBlocked() &&
      !impl.runner->isFinished() && !impl.engine->isStoryPaused()) {
    impl.runner->run();
  }
}

void KirikiriRuntime::renderGameplay(Kotonoha::RendererRef renderer,
                                     float scale) {
  auto &impl = *impl_;
  const auto draw = [&](Impl::Visual &visual, bool fullScreen) {
    if (visual.id.empty() || visual.decodeFailed)
      return;
    try {
      auto &texture = impl.assets->texture(renderer, visual.id);
      // The current KAG compatibility viewport is 1280x720. Backgrounds
      // cover it; foreground layers retain their native dimensions/position.
      SDL_FRect destination =
          fullScreen ? SDL_FRect{0, 0, 1280, 720}
                     : SDL_FRect{float(visual.x), float(visual.y),
                                 float(texture.w), float(texture.h)};
      if (!SDL_RenderTexture(&renderer, &texture, nullptr, &destination))
        throw std::runtime_error(SDL_GetError());
    } catch (const std::exception &error) {
      // An unsupported TLG or malformed asset must not flood the log on each
      // frame or hide the functional fallback title and other images.
      visual.decodeFailed = true;
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "KiriKiri: cannot display image %s: %s", visual.id.c_str(),
                  error.what());
    }
  };
  if (impl.currentScenario.empty()) {
    draw(impl.titlePicture, true);
    // The bounded TJS2 VM projects Window/Layer objects onto the same
    // novel-space renderer as KAG. The layer list is sorted by script order.
    if (impl.startupExecution.subsetCompleted) {
      for (auto &layer : impl.startupExecution.layers) {
        if (!layer.visible || layer.opacity == 0 || layer.storage.empty() ||
            impl.failedNativePictures.count(layer.storage))
          continue;
        try {
          const auto id = resolveVisual(*impl.assets, layer.storage, true);
          if (id.empty())
            throw std::runtime_error("asset not found");
          auto &texture = impl.assets->texture(renderer, id);
          // KiriKiri loadImages uses intrinsic dimensions unless the script
          // explicitly specifies a destination size.
          if (layer.width <= 0)
            layer.width = texture.w;
          if (layer.height <= 0)
            layer.height = texture.h;
          const SDL_FRect destination{float(layer.left), float(layer.top),
                                      float(layer.width), float(layer.height)};
          Uint8 previousAlpha = 255;
          if (!SDL_GetTextureAlphaMod(&texture, &previousAlpha) ||
              !SDL_SetTextureAlphaMod(&texture,
                                      static_cast<Uint8>(layer.opacity)))
            throw std::runtime_error(SDL_GetError());
          const bool success =
              SDL_RenderTexture(&renderer, &texture, nullptr, &destination);
          SDL_SetTextureAlphaMod(&texture, previousAlpha);
          if (!success)
            throw std::runtime_error(SDL_GetError());
        } catch (const std::exception &error) {
          if (impl.failedNativePictures.insert(layer.storage).second)
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "KiriKiri: TJS2 Layer image %s unavailable: %s",
                        layer.storage.c_str(), error.what());
        }
      }
    }
  } else {
    draw(impl.background, true);
    for (auto &[layer, visual] : impl.characters)
      draw(visual, false);
  }
  // Textbox and modal controls must render *above* the scene images.
  Runtime::renderGameplay(renderer, scale);
}

void KirikiriRuntime::advanceDialogue() {
  auto &impl = *impl_;
  SDL_Log("Kirikiri: advanceDialogue start");
  if (impl.dialogueVisible && impl.runner && impl.runner->isBlocked()) {
    impl.dialogueVisible = false;
    impl.textbox->hide();
    impl.runner->setBlocked(false);
    SDL_Log("Kirikiri: advanceDialogue calling runner->run()");
    impl.runner->run();
    SDL_Log("Kirikiri: advanceDialogue after run, blocked=%d finished=%d",
            impl.runner->isBlocked() ? 1 : 0,
            impl.runner->isFinished() ? 1 : 0);
  } else {
    SDL_Log("Kirikiri: advanceDialogue conditions not met");
  }
}

bool KirikiriRuntime::handleAdvanceAction() {
  auto &impl = *impl_;
  SDL_Log("Kirikiri: handleAdvanceAction dialogueVisible=%d runner=%d",
          impl.dialogueVisible ? 1 : 0, impl.runner ? 1 : 0);
  if (impl.dialogueVisible && impl.runner && impl.runner->isBlocked()) {
    SDL_Log("Kirikiri: handleAdvanceAction advancing dialogue");
    advanceDialogue();
    SDL_Log(
        "Kirikiri: handleAdvanceAction after advance, blocked=%d finished=%d",
        impl.runner->isBlocked() ? 1 : 0, impl.runner->isFinished() ? 1 : 0);
    return true;
  }
  SDL_Log("Kirikiri: handleAdvanceAction returning false");
  return false;
}

bool KirikiriRuntime::onEvent(Kotonoha::EventRef event) {
  auto mapped = Kotonoha::MapControllerEvent(event);
  auto &impl = *impl_;
  // The engine already converted pointer coordinates to novel space.
  // Failed scripted UI must not strand the user on an empty title.
  const auto nativeFallback = [&] {
    impl.nativeTitleUi = false;
    impl.nativeTitleDisabled = true;
    impl.titleShown = false;
    impl.choiceScene->hide();
    if (impl.engine)
      initGameplay(*impl.engine);
  };
  if (impl.currentScenario.empty() && impl.nativeTitleUi &&
      mapped.type == SDL_EVENT_MOUSE_MOTION) {
    if (Kotonoha::Providers::dispatchTjsUiHover(
            impl.startupExecution, mapped.motion.x, mapped.motion.y)) {
      impl.failedNativePictures.clear();
      if (!impl.startupExecution.error.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "KiriKiri: TJS2 hover event failed: %s",
                    impl.startupExecution.error.c_str());
        nativeFallback();
      }
      return true;
    }
  }
  if (impl.currentScenario.empty() && impl.nativeTitleUi &&
      mapped.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      mapped.button.button == SDL_BUTTON_LEFT) {
    if (Kotonoha::Providers::dispatchTjsUiClick(
            impl.startupExecution, mapped.button.x, mapped.button.y)) {
      impl.failedNativePictures.clear();
      const auto &run = impl.startupExecution;
      if (!run.error.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "KiriKiri: TJS2 click handler failed: %s",
                    run.error.c_str());
        nativeFallback();
      } else if (!run.kagEntry.empty()) {
        const auto target = run.kagEntry;
        if (startScenario(target)) {
          impl.nativeTitleUi = false;
          impl.choiceScene->hide();
        } else {
          SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                      "KiriKiri: UI requested unavailable KAG scenario: %s",
                      target.c_str());
          nativeFallback();
        }
      }
      return true;
    }
  }
  if (impl.dialogueVisible && !impl.waitingChoice &&
      !impl.choiceScene->isActive() &&
      (mapped.type == SDL_EVENT_KEY_DOWN ||
       mapped.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
       mapped.type == SDL_EVENT_FINGER_DOWN)) {
    if (mapped.type != SDL_EVENT_KEY_DOWN || !mapped.key.repeat) {
      advanceDialogue();
      return true;
    }
  }
  if (mapped.type == SDL_EVENT_KEY_DOWN && !mapped.key.repeat &&
      mapped.key.key == SDLK_F5) {
    auto path =
        Kotonoha::preferredDocumentsDirectory("kirikiri") / "quicksave.kns";
    saveGame(path.string());
    return true;
  }
  if (mapped.type == SDL_EVENT_KEY_DOWN && !mapped.key.repeat &&
      mapped.key.key == SDLK_F6) {
    auto path =
        Kotonoha::preferredDocumentsDirectory("kirikiri") / "quicksave.kns";
    loadGame(path.string());
    return true;
  }
  return Runtime::onEvent(mapped);
}

void KirikiriRuntime::saveGame(const std::string &path) {
  auto &impl = *impl_;
  const auto state =
      impl.runner ? impl.runner->saveState() : KagScript::Runner::State{};
  // V4 additionally stores active KAG conditional state. V1-V3 remain readable.
  std::ostringstream data;
  data << "KIRIKIRI_SAVE_4\n" << std::quoted(impl.currentScenario) << '\n';
  data << state.position << ' ' << (state.finished ? 1 : 0) << ' '
       << state.returnStack.size() << '\n';
  for (std::size_t i = 0; i < state.returnStack.size(); ++i)
    data << state.returnStack[i] << ' ' << state.callIfDepth.at(i) << '\n';
  data << state.variables.size() << '\n';
  for (const auto &variable : state.variables)
    data << std::quoted(variable.first) << ' ' << std::quoted(variable.second)
         << '\n';
  data << state.ifBranches.size() << '\n';
  for (auto branch : state.ifBranches)
    data << unsigned(branch) << '\n';
  Kotonoha::GetVNManagerRuntime(*impl.engine).save(path, data.str());
}

void KirikiriRuntime::loadGame(const std::string &path) {
  const auto saved = Kotonoha::VNSave::read(path);
  std::istringstream data(saved.gameData);
  std::string header, scenario;
  KagScript::Runner::State state;
  bool fullState = false;
  std::getline(data, header);
  if (header == "KIRIKIRI_SAVE_4" || header == "KIRIKIRI_SAVE_3") {
    const bool v4 = header == "KIRIKIRI_SAVE_4";
    fullState = true;
    unsigned finished = 0;
    std::size_t frames = 0, variables = 0;
    if (!(data >> std::quoted(scenario) >> state.position >> finished >>
          frames) ||
        finished > 1 || frames > 65536)
      throw std::runtime_error("Invalid Kirikiri v3 save header");
    state.finished = finished != 0;
    for (std::size_t i = 0; i < frames; ++i) {
      std::size_t pc = 0;
      std::size_t depth = 0;
      if (!(data >> pc) || (v4 && !(data >> depth)))
        throw std::runtime_error("Truncated KAG call frame");
      state.returnStack.push_back(pc);
      state.callIfDepth.push_back(depth);
    }
    if (!(data >> variables) || variables > 65536)
      throw std::runtime_error("Invalid KAG save variable count");
    for (std::size_t i = 0; i < variables; ++i) {
      std::string key, value;
      if (!(data >> std::quoted(key) >> std::quoted(value)) ||
          !state.variables.emplace(key, value).second)
        throw std::runtime_error("Malformed KAG save variables");
    }
    if (v4) {
      std::size_t active = 0;
      if (!(data >> active) || active > 1024)
        throw std::runtime_error("Invalid KAG conditional stack depth");
      for (std::size_t i = 0; i < active; ++i) {
        unsigned matched = 0;
        if (!(data >> matched) || matched > 1)
          throw std::runtime_error("Invalid KAG conditional save state");
        state.ifBranches.push_back(static_cast<std::uint8_t>(matched));
      }
    }
  } else if (header == "KIRIKIRI_SAVE_2") {
    std::getline(data, scenario);
    if (!(data >> state.position))
      throw std::runtime_error("Invalid Kirikiri v2 save");
  } else {
    scenario = std::move(header);
  }
  if (!scenario.empty() &&
      !startScenario(scenario, state.position, fullState ? &state : nullptr))
    throw std::runtime_error("Kirikiri save references missing scenario");
}

bool KirikiriRuntime::startScenario(
    const std::string &name, std::size_t position,
    const KagScript::Runner::State *savedState) {
  SDL_Log("Kirikiri: startScenario ENTRY name='%s'", name.c_str());
  auto &impl = *impl_;
  auto files = impl.assets->scenarioFiles();
  std::string target = name;
  bool found = false;
  for (const auto &f : files) {
    if (f == name || std::filesystem::path(f).filename().string() == name ||
        scenarioTitle(f) == scenarioTitle(name)) {
      target = f;
      found = true;
      break;
    }
  }
  if (!found)
    return false;

  auto text = impl.assets->loadTextUtf8(target);
  SDL_Log("Kirikiri: startScenario '%s' read %zu bytes", target.c_str(),
          text.size());
  auto script = KagScript::parse(text, target);
  SDL_Log("Kirikiri: parsed %zu tags", script.tags.size());

  impl.currentScenario = target;
  impl.waitingWait = false;
  impl.finishWait = {};
  impl.waitingChoice = false;
  scenarioTexts_.clear();
  for (const auto &tag : script.tags)
    if (tag.type == KagScript::TagType::Text && !tag.text.empty() &&
        std::find(scenarioTexts_.begin(), scenarioTexts_.end(), tag.text) ==
            scenarioTexts_.end())
      scenarioTexts_.push_back(tag.text);

  const auto playAudio = [this](Kotonoha::PcmPlayer &player,
                                const std::string &file,
                                const std::string &channel, bool loop) {
    auto decoded = impl_->assets->loadAudio(file);
    SDL_AudioSpec targetSpec{SDL_AUDIO_S16, decoded.spec.channels,
                             decoded.spec.freq};
    Uint8 *converted = nullptr;
    int size = 0;
    if (!SDL_ConvertAudioSamples(&decoded.spec, decoded.data.data(),
                                 int(decoded.data.size()), &targetSpec,
                                 &converted, &size))
      throw std::runtime_error(SDL_GetError());
    Kotonoha::SDLMemoryPtr<Uint8> owner(converted);
    Kotonoha::PcmPlayer::Clip clip;
    clip.channels = targetSpec.channels;
    clip.rate = targetSpec.freq;
    clip.loop = loop;
    clip.samples.resize(size / sizeof(int16_t));
    std::memcpy(clip.samples.data(), converted, size);
    if (!player.Play(impl_->engine->GetAudio().channel(channel),
                     std::move(clip)))
      throw std::runtime_error(SDL_GetError());
  };

  KagScript::Callbacks callbacks;

  callbacks.showText = [this](std::string speaker, std::string text) {
    originalDialogueSpeaker_ = speaker;
    originalDialogueText_ = text;
    originalActiveKeys_.clear();
    if (!speaker.empty())
      originalActiveKeys_.push_back(speaker);
    if (!text.empty())
      originalActiveKeys_.push_back(text);
    dialogueSpeaker_ =
        impl_->engine->GetTranslation().text(debugNode(), speaker);
    dialogueText_ = impl_->engine->GetTranslation().text(debugNode(), text);
    auto &impl = *impl_;
    impl.dialogueVisible = true;
    impl.textbox->setDialogue(dialogueSpeaker_, dialogueText_);
    impl.textbox->show();
  };

  callbacks.showChoice = [this](std::string question,
                                std::vector<std::string> options,
                                std::function<void(int)> onChoose) {
    auto &impl = *impl_;
    impl.waitingChoice = true;
    originalActiveKeys_.clear();
    if (!question.empty())
      originalActiveKeys_.push_back(question);
    originalActiveKeys_.insert(originalActiveKeys_.end(), options.begin(),
                               options.end());
    const auto translate = [&](const std::string &value) {
      if (!value.empty() &&
          std::find(scenarioTexts_.begin(), scenarioTexts_.end(), value) ==
              scenarioTexts_.end())
        scenarioTexts_.push_back(value);
      return impl.engine->GetTranslation().text(debugNode(), value);
    };
    std::vector<Kotonoha::ChoiceOption> choiceOptions;
    int id = 0;
    for (auto &opt : options)
      choiceOptions.push_back({translate(opt), id++});
    impl.choiceScene->setQuestion(translate(question), choiceOptions,
                                  [this, onChoose = std::move(onChoose)](
                                      int optionId, const std::string &) {
                                    auto &impl = *impl_;
                                    impl.waitingChoice = false;
                                    impl.runner->setBlocked(false);
                                    onChoose(optionId);
                                    impl.runner->run();
                                  });
  };

  callbacks.jump = [this](std::string label) {
    auto &impl = *impl_;
    if (!label.empty())
      debugJumpToLabel(label);
  };

  callbacks.wait = [this](int ms, std::function<void()> onDone) {
    auto &impl = *impl_;
    impl.waitingWait = true;
    impl.waitingDeadline = SDL_GetTicks() + static_cast<std::uint64_t>(ms);
    impl.finishWait = std::move(onDone);
  };

  callbacks.showBg = [this](std::string file, int transitionMs) {
    auto &state = *impl_;
    if (file.empty()) {
      state.background = {};
      return;
    }
    const auto id = resolveVisual(*state.assets, file, false);
    if (id.empty()) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "KiriKiri: background asset not found: %s", file.c_str());
      return;
    }
    state.background = {id, 0, 0, false};
    if (transitionMs > 0)
      SDL_LogDebug(
          SDL_LOG_CATEGORY_APPLICATION,
          "KiriKiri: background transition %dms is currently immediate",
          transitionMs);
  };

  callbacks.showChar = [this](std::string layer, std::string file, int x, int y,
                              int transitionMs) {
    auto &state = *impl_;
    const auto id = resolveVisual(*state.assets, file, true);
    if (id.empty()) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "KiriKiri: foreground image not found: %s", file.c_str());
      return;
    }
    state.characters[layer] = {id, x, y, false};
    if (transitionMs > 0)
      SDL_LogDebug(
          SDL_LOG_CATEGORY_APPLICATION,
          "KiriKiri: foreground transition %dms is currently immediate",
          transitionMs);
  };

  callbacks.hideChar = [this](std::string layer, int) {
    impl_->characters.erase(layer);
  };

  callbacks.playBgm = [this, playAudio](std::string file, bool loop) {
    playAudio(impl_->bgm, file, "BGM", loop);
  };

  callbacks.playSe = [this, playAudio](std::string file) {
    playAudio(impl_->sound, file, "SE", false);
  };

  callbacks.stopBgm = [this]() { impl_->bgm.Stop(); };

  callbacks.setVar = [this](std::string name, std::string value) {
    SDL_Log("Kirikiri: set %s=%s", name.c_str(), value.c_str());
  };

  callbacks.getVar = [this](std::string name) -> std::string { return {}; };

  impl.runner =
      std::make_unique<KagScript::Runner>(script, std::move(callbacks));
  if (savedState)
    impl.runner->restoreState(*savedState);
  else
    impl.runner->restorePosition(position);
  impl.running = true;
  impl.runner->run();
  return true;
}

bool KirikiriRuntime::debugJumpToLabel(const std::string &label) {
  auto &impl = *impl_;
  if (!impl.runner)
    return false;
  if (!impl.runner->jumpToLabel(label))
    return false;
  impl.runner->run();
  return true;
}

std::string KirikiriRuntime::debugNode() const {
  auto &impl = *impl_;
  return impl.currentScenario.empty()
             ? std::string()
             : ("kirikiri | scenario " + impl.currentScenario);
}

void KirikiriRuntime::debugRestartScene() {
  if (impl_->currentScenario.empty() || !startScenario(impl_->currentScenario))
    throw std::runtime_error("There is no active Kirikiri scene to restart");
}

std::vector<std::string> KirikiriRuntime::activeTranslationKeys() const {
  return originalActiveKeys_;
}

Kotonoha::Translation::SourceTexts KirikiriRuntime::translationSources() const {
  return scenarioTexts_.empty()
             ? Kotonoha::Translation::SourceTexts{}
             : Kotonoha::Translation::SourceTexts{{0, scenarioTexts_}};
}

} // namespace Kirikiri
