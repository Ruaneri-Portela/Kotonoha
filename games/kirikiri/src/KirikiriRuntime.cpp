#include <Kirikiri/EmoteRenderer.hpp>
#include <Kirikiri/KirikiriRuntime.hpp>
#include <Kirikiri/PsbFile.hpp>
#include <Kirikiri/ScnRunner.hpp>
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
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace Kirikiri {

namespace {

std::string scenarioTitle(const std::string &fileName) {
  std::string name = std::filesystem::path(fileName).filename().string();
  std::string lower = name;
  for (char &c : lower)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".scn") {
    name.resize(name.size() - 4);
    lower.resize(lower.size() - 4);
  }
  if (lower.size() >= 3 && lower.substr(lower.size() - 3) == ".ks") {
    name.resize(name.size() - 3);
  }
  return name;
}

// KAG storage names often omit the extension and directory. Resolve only
// formats the provider can supply, rather than constructing filesystem paths.
std::string resolveVisual(const Kotonoha::Providers::KirikiriAssets &assets,
                          std::string storage, bool foreground) {
  return EmoteRenderer::resolveVisualAsset(assets, std::move(storage),
                                           foreground);
}

std::string resolveAudio(const Kotonoha::Providers::KirikiriAssets &assets,
                         std::string storage) {
  std::replace(storage.begin(), storage.end(), '\\', '/');
  if (storage.empty() || storage.front() == '/' ||
      storage.find("://") != std::string::npos)
    return {};
  const auto slash = storage.find_last_of('/');
  const auto dot = storage.find_last_of('.');
  const bool hasExtension =
      dot != std::string::npos && (slash == std::string::npos || dot > slash);
  for (const char *folder : {"", "bgm/", "sound/", "se/", "voice/", "VNX+/"}) {
    const std::string base = std::string(folder) + storage;
    for (const char *ext : {"", ".ogg", ".opus", ".wav"}) {
      if (hasExtension && *ext)
        continue;
      const std::string candidate = base + ext;
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
       {"vnx+/langselect_cn@base%base", "vnx+/title_cn@bg.layer", "image/title",
        "image/title_bg", "image/titlebase", "system/title", "system/title_bg",
        "bgimage/title", "title/titlebase", "title/title", "title",
        "menu/title", "image/menu"}) {
    for (const char *ext : {".png", ".jpg", ".jpeg", ".webp", ".bmp", ".tlg"}) {
      if (auto it = images.find(std::string(base) + ext); it != images.end())
        return it->second;
    }
  }
  return {};
}

bool pointInRect(float px, float py, const SDL_FRect &r) {
  return px >= r.x && px <= (r.x + r.w) && py >= r.y && py <= (r.y + r.h);
}

void drawRoundedPanel(SDL_Renderer &renderer, const SDL_FRect &rect,
                      SDL_Color fill, SDL_Color border) {
  SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(&renderer, fill.r, fill.g, fill.b, fill.a);
  SDL_RenderFillRect(&renderer, &rect);
  SDL_SetRenderDrawColor(&renderer, border.r, border.g, border.b, border.a);
  SDL_RenderRect(&renderer, &rect);
}

void drawTextLine(Kotonoha::Engine *engine, SDL_Renderer &renderer,
                  const std::string &text, int ptSize, float x, float y,
                  SDL_Color color, bool centered = false,
                  bool dropShadow = true) {
  if (!engine || text.empty())
    return;
  try {
    auto &ui = engine->GetVNUI();
    const float w = ui.textWidth(text, static_cast<float>(ptSize));
    const float drawX = centered ? (x - w * 0.5f) : x;
    if (dropShadow) {
      ui.textSized(renderer, text, drawX + 1.5f, y + 1.5f,
                   std::max(w + 32.0f, 800.0f), static_cast<float>(ptSize),
                   SDL_Color{25, 15, 22, static_cast<Uint8>(color.a * 3 / 4)});
    }
    ui.textSized(renderer, text, drawX, y, std::max(w + 32.0f, 800.0f),
                 static_cast<float>(ptSize), color);
  } catch (...) {
  }
}

void drawTextWrapped(Kotonoha::Engine *engine, SDL_Renderer &renderer,
                     const std::string &text, int ptSize, float x, float y,
                     Uint32 wrapWidth, SDL_Color color,
                     bool dropShadow = true) {
  if (!engine || text.empty())
    return;
  try {
    auto &ui = engine->GetVNUI();
    if (dropShadow) {
      ui.textSized(renderer, text, x + 1.5f, y + 1.5f,
                   static_cast<float>(wrapWidth), static_cast<float>(ptSize),
                   SDL_Color{25, 14, 20, static_cast<Uint8>(color.a * 4 / 5)});
    }
    ui.textSized(renderer, text, x, y, static_cast<float>(wrapWidth),
                 static_cast<float>(ptSize), color);
  } catch (...) {
  }
}

} // namespace

struct KirikiriRuntime::Impl {
  KirikiriRuntime &runtime;
  std::shared_ptr<Kotonoha::Providers::KirikiriAssets> assets;
  Kotonoha::Engine *engine = nullptr;

  enum class ScreenMode { None, Title, Save, Load, Config, Backlog, Extra };

  struct BacklogEntry {
    std::string speaker;
    std::string text;
    std::string nameplate;
  };

  struct SaveSlotSummary {
    bool exists = false;
    std::string scenario;
    std::string chapterTitle;
    std::string previewText;
  };

  // Presentation state
  std::string currentScenario;
  std::unique_ptr<KagScript::Runner> runner;
  std::unique_ptr<ScnRunner> scnRunner;
  EmoteRenderer emoteRenderer;
  std::string dialogueSpeaker;
  std::string dialogueText;
  std::string dialogueNameplate;
  bool dialogueVisible = false;
  bool messageHiddenByUser = false;

  // Native VNX+ UI state
  bool hasVnxUi = false;
  ScreenMode screenMode = ScreenMode::None;
  ScreenMode returnScreenMode = ScreenMode::None;
  float mouseX = -1.0f;
  float mouseY = -1.0f;
  bool mouseDown = false;
  int configTab = 0;             // 0 = game, 1 = effsnd, 2 = voice
  std::size_t languageIndex = 2; // 2 = PT-BR (VNX+ slot), 1 = EN, 0 = JP
  bool autoMode = false;
  bool skipMode = false;
  std::uint64_t nextAutoAdvanceMs = 0;
  std::string quickPopupAsset;
  std::uint64_t quickPopupUntilMs = 0;
  std::vector<BacklogEntry> history;
  int backlogScroll = 0;
  std::vector<std::string> chapterList;
  std::array<SaveSlotSummary, 6> saveSlots{};

  struct Visual {
    std::string id;
    int x = 0, y = 0;
    bool decodeFailed = false;
    bool visible = true;
    int opacity = 255;
    int order = 0;
  };
  Visual titlePicture, background, nameplateVisual;
  std::map<std::string, Visual> characters;
  bool titleRestartRequested = false;
  std::string pendingJumpStorage;
  std::string pendingJumpTarget;
  std::uint64_t lastTickMs = 0;
  std::set<std::string> unlockedFlags;

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
  std::set<std::string> warnedMissingAssets;
  Kotonoha::Providers::TjsBootstrapReport bootstrap;
  Kotonoha::Providers::TjsStartupResult startupExecution;
  bool waitingChoice = false;
  bool waitingWait = false;
  std::uint64_t waitingDeadline = 0;
  std::function<void()> finishWait;
  Kotonoha::PcmPlayer bgm;
  Kotonoha::PcmPlayer sound;
  std::array<Kotonoha::PcmPlayer, 4> voices;
  // Provider IDs played by the native audio callbacks; no decoded samples or
  // provider-specific archive pointers are retained by the Debug UI.
  std::vector<Kotonoha::DebugAudioAsset> sceneAudio;
  std::string lastVoiceAssetId;

  Impl(KirikiriRuntime &r,
       std::shared_ptr<Kotonoha::Providers::KirikiriAssets> a)
      : runtime(r), assets(std::move(a)) {}

  bool isRunnerBlocked() const {
    if (scnRunner)
      return scnRunner->isBlocked();
    return runner ? runner->isBlocked() : false;
  }
  bool isRunnerFinished() const {
    if (scnRunner)
      return scnRunner->isFinished();
    return runner ? runner->isFinished() : true;
  }
  void runActive() {
    if (scnRunner)
      scnRunner->run();
    else if (runner)
      runner->run();
  }
  bool processPendingJump() {
    if (pendingJumpStorage.empty())
      return false;
    const std::string storage = std::move(pendingJumpStorage);
    const std::string target = std::move(pendingJumpTarget);
    pendingJumpStorage.clear();
    pendingJumpTarget.clear();
    if (!runtime.startScenario(storage))
      return false;
    if (!target.empty())
      runtime.debugJumpToLabel(target);
    return true;
  }

  std::filesystem::path slotPath(int slotIndex) const {
    return Kotonoha::preferredDocumentsDirectory("kirikiri") /
           ("slot_" + std::to_string(slotIndex + 1) + ".kns");
  }

  std::filesystem::path quickSavePath() const {
    return Kotonoha::preferredDocumentsDirectory("kirikiri") / "quicksave.kns";
  }

  void refreshSaveSlots() {
    for (int i = 0; i < 6; ++i) {
      auto &slot = saveSlots[static_cast<std::size_t>(i)];
      slot = {};
      const auto path = slotPath(i);
      std::error_code ec;
      if (!std::filesystem::exists(path, ec))
        continue;
      try {
        const auto saved = Kotonoha::VNSave::read(path.string());
        std::istringstream data(saved.gameData);
        std::string header, scenario;
        std::getline(data, header);
        if (header == "KIRIKIRI_SAVE_5" || header == "KIRIKIRI_SAVE_4" ||
            header == "KIRIKIRI_SAVE_3") {
          data >> std::quoted(scenario);
        } else if (header == "KIRIKIRI_SAVE_2") {
          std::getline(data, scenario);
        } else {
          scenario = header;
        }
        slot.exists = !scenario.empty();
        slot.scenario = scenario;
        slot.chapterTitle = "Capítulo " + scenarioTitle(scenario);
        slot.previewText = "Cena: " + scenarioTitle(scenario);
      } catch (...) {
      }
    }
  }

  void loadChapterList() {
    chapterList.clear();
    for (const char *candidate :
         {"VNX+/scn/scenelist.scn", "scn/scenelist.scn", "scenelist.scn"}) {
      if (!assets->contains(candidate))
        continue;
      try {
        const auto raw = assets->read(candidate);
        if (!Kotonoha::Providers::PsbFile::isMdfBytes(raw.data(), raw.size()) &&
            !Kotonoha::Providers::PsbFile::isPsbBytes(raw.data(), raw.size()))
          continue;
        const auto psb = Kotonoha::Providers::PsbFile::parse(raw);
        const auto &scnList = psb.root()["scenelist"];
        if (!scnList.isList())
          continue;
        for (const auto &entry : scnList.asList()) {
          if (!entry.isString())
            continue;
          const std::string id = entry.asString();
          if (!id.empty())
            chapterList.push_back(id);
        }
        if (!chapterList.empty())
          return;
      } catch (...) {
      }
    }
    for (const auto &s : assets->scenarioFiles()) {
      const std::string title = scenarioTitle(s);
      if (title != "sysmenu" && title != "scenelist")
        chapterList.push_back(title);
    }
  }

  bool drawVnxAsset(SDL_Renderer &renderer, const std::string &storage,
                    const SDL_FRect *dst = nullptr, float x = 0.0f,
                    float y = 0.0f, Uint8 alpha = 255) {
    const std::string id = resolveVisual(*assets, storage, true);
    if (id.empty() || failedNativePictures.count(id))
      return false;
    try {
      auto &tex = assets->texture(renderer, id);
      SDL_FRect target =
          dst ? *dst : SDL_FRect{x, y, float(tex.w), float(tex.h)};
      Uint8 prevAlpha = 255;
      SDL_GetTextureAlphaMod(&tex, &prevAlpha);
      SDL_SetTextureAlphaMod(&tex, alpha);
      const bool ok =
          Kotonoha::RenderUpscaledTexture(&renderer, &tex, nullptr, &target);
      SDL_SetTextureAlphaMod(&tex, prevAlpha);
      return ok;
    } catch (...) {
      failedNativePictures.insert(id);
      return false;
    }
  }

  bool drawVnxButton(SDL_Renderer &renderer, const std::string &normalAsset,
                     const std::string &overAsset, const std::string &onAsset,
                     const SDL_FRect &rect, bool forceActive = false) {
    const bool hovered = pointInRect(mouseX, mouseY, rect);
    const std::string &chosen = (forceActive || (hovered && mouseDown))
                                    ? onAsset
                                    : (hovered ? overAsset : normalAsset);
    return drawVnxAsset(renderer, chosen, &rect);
  }

  void openScreen(ScreenMode target) {
    if (screenMode == ScreenMode::None || screenMode == ScreenMode::Title)
      returnScreenMode = screenMode;
    if (target == ScreenMode::Save || target == ScreenMode::Load)
      refreshSaveSlots();
    if (target == ScreenMode::Extra && chapterList.empty())
      loadChapterList();
    screenMode = target;
  }

  void closeOverlayScreen() {
    if (!currentScenario.empty() && returnScreenMode != ScreenMode::Title)
      screenMode = ScreenMode::None;
    else
      screenMode = ScreenMode::Title;
  }
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
  if (impl.hasVnxUi) {
    SetState(Kotonoha::GameState::Gameplay);
    impl.openScreen(Impl::ScreenMode::Save);
    return;
  }
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

  impl.hasVnxUi = !resolveVisual(*impl.assets,
                                 "VNX+/title_cn@start%button;normal.png", true)
                       .empty();

  const auto attachOnce = [&](Kotonoha::Scene *scene) {
    const auto &scenes = GetScenes(Kotonoha::GameState::Gameplay);
    if (std::find(scenes.begin(), scenes.end(), scene) == scenes.end())
      AddScene(scene);
  };
  attachOnce(impl.choiceScene);
  if (!impl.hasVnxUi) {
    attachOnce(impl.textbox);
    attachOnce(impl.backlog);
    attachOnce(impl.menu);
  } else {
    if (impl.textbox)
      impl.textbox->hide();
    if (impl.backlog)
      impl.backlog->close();
    if (impl.menu)
      impl.menu->close();
  }

  if ((impl.runner || impl.scnRunner) && !impl.isRunnerFinished()) {
    // Resume a running scenario.
    impl.running = true;
    impl.runActive();
    impl.processPendingJump();
  } else if (impl.currentScenario.empty()) {
    auto scenarios = impl.assets->scenarioFiles();
    SDL_Log("Kirikiri: found %zu scenarios", scenarios.size());
    for (const auto &s : scenarios)
      SDL_Log("Kirikiri: scenario: %s", s.c_str());
    if (!impl.titleShown) {
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

      const auto &ui = impl.startupExecution;
      const bool imagePresent = std::any_of(
          ui.layers.begin(), ui.layers.end(),
          [&](const Kotonoha::Providers::TjsUiLayer &layer) {
            return layer.visible &&
                   (!layer.texts.empty() || !layer.rectangles.empty() ||
                    (!layer.storage.empty() &&
                     !resolveVisual(*impl.assets, layer.storage, true).empty()));
          });
      const bool hasClickTarget =
          std::any_of(ui.layers.begin(), ui.layers.end(),
                      [](const Kotonoha::Providers::TjsUiLayer &layer) {
                        return layer.visible && layer.enabled &&
                               (!layer.onClick.empty() ||
                                !layer.onMouseUp.empty()) &&
                               ((layer.width > 0 && layer.height > 0) ||
                                !layer.storage.empty());
                      });
      impl.nativeTitleUi = !impl.nativeTitleDisabled && ui.subsetCompleted &&
                           ui.error.empty() && imagePresent &&
                           (hasClickTarget || !ui.onKeyDown.empty());
      SDL_Log("KiriKiri: TJS2 title projection: %zu layers, interactive=%d",
              ui.layers.size(), impl.nativeTitleUi ? 1 : 0);
      impl.titlePicture = {findTitlePicture(*impl.assets), 0, 0, false};
      if (impl.nativeTitleUi) {
        // A coherent source-created title takes priority over asset-specific
        // UI fallbacks, even when VNX+ overlay images are installed.
        impl.screenMode = Impl::ScreenMode::None;
        impl.choiceScene->hide();
        SDL_Log("KiriKiri: presenting script-created original title menu");
        return;
      }
      if (impl.hasVnxUi) {
        // The translation overlay's menu remains the safe fallback if the
        // source title could not be evaluated or displayed.
        impl.loadChapterList();
        impl.refreshSaveSlots();
        impl.screenMode = Impl::ScreenMode::Title;
        impl.returnScreenMode = Impl::ScreenMode::Title;
        impl.choiceScene->hide();
        SDL_Log("KiriKiri: presenting VNX+ compatibility title menu");
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
    impl.bgm.Stop();
    impl.sound.Stop();
    for (auto &v : impl.voices)
      v.Stop();
    impl.runner.reset();
    impl.scnRunner.reset();
    impl.pendingJumpStorage.clear();
    impl.pendingJumpTarget.clear();
    impl.running = false;
    impl.titleShown = false;
    impl.currentScenario.clear();
    impl.nativeTitleUi = false;
    impl.nativeTitleDisabled = false;
    impl.startupExecution = {};
    impl.failedNativePictures.clear();
    impl.background = {};
    impl.nameplateVisual = {};
    impl.characters.clear();
    impl.dialogueVisible = false;
    impl.messageHiddenByUser = false;
    impl.autoMode = false;
    impl.skipMode = false;
    impl.waitingWait = impl.waitingChoice = false;
    impl.screenMode =
        impl.hasVnxUi ? Impl::ScreenMode::Title : Impl::ScreenMode::None;
    SetState(Kotonoha::GameState::Gameplay);
    init(*impl.engine);
    return;
  }
  if (impl.processPendingJump())
    return;
  const std::uint64_t nowMs = SDL_GetTicks();
  const int elapsedMs = impl.lastTickMs > 0
                            ? static_cast<int>(std::min<std::uint64_t>(
                                  nowMs - impl.lastTickMs, 250))
                            : 16;
  impl.lastTickMs = nowMs;
  if (impl.scnRunner && impl.engine && !impl.engine->isStoryPaused() &&
      impl.screenMode == Impl::ScreenMode::None) {
    impl.scnRunner->tickAnimations(elapsedMs);
  }
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
  if (impl.waitingWait && (nowMs >= impl.waitingDeadline || impl.skipMode)) {
    impl.waitingWait = false;
    auto done = std::move(impl.finishWait);
    impl.finishWait = {};
    if (done)
      done();
  }
  if (impl.screenMode == Impl::ScreenMode::None && impl.dialogueVisible &&
      impl.isRunnerBlocked() && !impl.waitingChoice) {
    if (impl.skipMode) {
      advanceDialogue();
      return;
    }
    if (impl.autoMode && nowMs >= impl.nextAutoAdvanceMs) {
      advanceDialogue();
      return;
    }
  }
  if (impl.engine && impl.running && !impl.isRunnerBlocked() &&
      !impl.isRunnerFinished() && !impl.engine->isStoryPaused() &&
      impl.screenMode == Impl::ScreenMode::None) {
    impl.runActive();
    impl.processPendingJump();
  }
}

void KirikiriRuntime::renderGameplay(Kotonoha::RendererRef renderer,
                                     float scale) {
  auto &impl = *impl_;
  const auto draw = [&](Impl::Visual &visual, bool fullScreen) {
    if (visual.id.empty() || visual.decodeFailed ||
        !visual.visible || visual.opacity == 0)
      return;
    try {
      auto &texture = impl.assets->texture(renderer, visual.id);
      SDL_FRect destination =
          fullScreen ? SDL_FRect{0, 0, 1280, 720}
                     : SDL_FRect{float(visual.x), float(visual.y),
                                 float(texture.w), float(texture.h)};
      Uint8 previousAlpha = 255;
      if (!SDL_GetTextureAlphaMod(&texture, &previousAlpha) ||
          !SDL_SetTextureAlphaMod(
              &texture, static_cast<Uint8>(visual.opacity)))
        throw std::runtime_error(SDL_GetError());
      const bool rendered = Kotonoha::RenderUpscaledTexture(
          &renderer, &texture, nullptr, &destination);
      SDL_SetTextureAlphaMod(&texture, previousAlpha);
      if (!rendered)
        throw std::runtime_error(SDL_GetError());
    } catch (const std::exception &error) {
      visual.decodeFailed = true;
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "KiriKiri: cannot display image %s: %s", visual.id.c_str(),
                  error.what());
    }
  };

  // 1. Render base stage / gameplay scene if active
  if (!impl.currentScenario.empty()) {
    if (impl.scnRunner) {
      impl.emoteRenderer.renderLayer(renderer, impl.engine, *impl.assets,
                                     impl.scnRunner->stage(), true);
      for (const auto &[_, ev] : impl.scnRunner->events()) {
        const bool full = (ev.name == "ev" || ev.name == "eventf");
        impl.emoteRenderer.renderLayer(renderer, impl.engine, *impl.assets, ev,
                                       full);
      }
      for (const auto &[_, ch] : impl.scnRunner->characters()) {
        impl.emoteRenderer.renderCharacter(renderer, impl.engine, *impl.assets,
                                           ch);
      }
    } else {
      draw(impl.background, true);
      // Foreground KAG layer indices are numeric. Iterating the map
      // lexically used to draw "10" before "2", breaking foreground order.
      std::vector<Impl::Visual *> ordered;
      ordered.reserve(impl.characters.size());
      for (auto &[layer, visual] : impl.characters)
        ordered.push_back(&visual);
      std::stable_sort(ordered.begin(), ordered.end(),
                       [](const Impl::Visual *a, const Impl::Visual *b) {
                         return a->order < b->order;
                       });
      for (auto *visual : ordered)
        draw(*visual, false);
    }

    // Native VNX+ In-Game Message Box & Toolbar
    if (impl.hasVnxUi && impl.screenMode == Impl::ScreenMode::None &&
        impl.dialogueVisible && !impl.messageHiddenByUser) {
      const SDL_FRect msgOuter{64.0f, 496.0f, 1152.0f, 198.0f};
      const SDL_FRect msgInner{70.0f, 502.0f, 1140.0f, 186.0f};
      drawRoundedPanel(renderer, msgOuter, SDL_Color{55, 32, 42, 215},
                       SDL_Color{245, 185, 205, 235});
      drawRoundedPanel(renderer, msgInner, SDL_Color{38, 22, 30, 210},
                       SDL_Color{255, 220, 232, 180});

      // Decorative header bar inside message window
      const SDL_FRect topStripe{70.0f, 502.0f, 1140.0f, 6.0f};
      SDL_SetRenderDrawColor(&renderer, 238, 138, 168, 220);
      SDL_RenderFillRect(&renderer, &topStripe);

      // Speaker nameplate (VNX+/cn_name_*.png 380x41)
      if (!impl.nameplateVisual.id.empty()) {
        impl.nameplateVisual.x = 88;
        impl.nameplateVisual.y = 466;
        draw(impl.nameplateVisual, false);
      } else if (!dialogueSpeaker_.empty()) {
        const SDL_FRect badge{88.0f, 466.0f, 260.0f, 38.0f};
        drawRoundedPanel(renderer, badge, SDL_Color{230, 112, 148, 240},
                         SDL_Color{255, 240, 245, 255});
        drawTextLine(impl.engine, renderer, dialogueSpeaker_, 20, 218.0f,
                     473.0f, SDL_Color{255, 255, 255, 255}, true);
      }

      // Main PT-BR dialogue text
      drawTextWrapped(impl.engine, renderer, dialogueText_, 24, 102.0f, 524.0f,
                      1076u, SDL_Color{255, 248, 252, 255}, true);

      // Animated paw/triangle advance indicator when waiting for click
      if (impl.isRunnerBlocked()) {
        const float bob =
            static_cast<float>((SDL_GetTicks() / 180) % 2 == 0 ? 0 : 4);
        const SDL_FRect indicator{1176.0f, 636.0f + bob, 16.0f, 16.0f};
        SDL_SetRenderDrawColor(&renderer, 255, 175, 200, 245);
        SDL_RenderFillRect(&renderer, &indicator);
      }

      // Bottom VNX+ toolbar buttons (window@help__*)
      struct ToolbarItem {
        const char *asset;
        SDL_FRect rect;
        bool active;
      };
      const std::array<ToolbarItem, 7> toolbar{{
          {"VNX+/window@help__qsave%layer.png",
           {580.0f, 668.0f, 86.0f, 16.0f},
           false},
          {"VNX+/window@help__qload%layer.png",
           {680.0f, 668.0f, 90.0f, 16.0f},
           false},
          {"VNX+/window@help__save%layer.png",
           {784.0f, 668.0f, 44.0f, 16.0f},
           false},
          {"VNX+/window@help__load%layer.png",
           {842.0f, 668.0f, 46.0f, 16.0f},
           false},
          {"VNX+/window@help__auto%layer.png",
           {902.0f, 668.0f, 88.0f, 16.0f},
           impl.autoMode},
          {"VNX+/window@help__skip%layer.png",
           {1004.0f, 668.0f, 40.0f, 16.0f},
           impl.skipMode},
          {"VNX+/window@help__config%layer.png",
           {1058.0f, 668.0f, 60.0f, 16.0f},
           false},
      }};
      for (const auto &item : toolbar) {
        const bool hovered = pointInRect(impl.mouseX, impl.mouseY, item.rect);
        if (hovered || item.active) {
          SDL_FRect hi{item.rect.x - 4.0f, item.rect.y - 3.0f,
                       item.rect.w + 8.0f, item.rect.h + 6.0f};
          drawRoundedPanel(renderer, hi,
                           item.active ? SDL_Color{235, 115, 152, 200}
                                       : SDL_Color{255, 255, 255, 70},
                           SDL_Color{255, 220, 232, 180});
        }
        impl.drawVnxAsset(renderer, item.asset, &item.rect);
      }
      // Extra Backlog pill button on toolbar
      const SDL_FRect logRect{1130.0f, 665.0f, 64.0f, 20.0f};
      const bool logHover = pointInRect(impl.mouseX, impl.mouseY, logRect);
      drawRoundedPanel(renderer, logRect,
                       logHover ? SDL_Color{235, 115, 152, 200}
                                : SDL_Color{75, 45, 58, 180},
                       SDL_Color{255, 210, 225, 190});
      drawTextLine(impl.engine, renderer, "LOG", 13, 1162.0f, 667.0f,
                   SDL_Color{255, 245, 250, 255}, true, false);
    } else if (!impl.hasVnxUi && impl.dialogueVisible &&
               !impl.nameplateVisual.id.empty()) {
      draw(impl.nameplateVisual, false);
    }

    // Quick Save / Quick Load popup notification (VNX+/qlpopup@*)
    if (impl.hasVnxUi && !impl.quickPopupAsset.empty() &&
        SDL_GetTicks() < impl.quickPopupUntilMs) {
      const SDL_FRect baseRect{423.0f, 260.0f, 434.0f, 120.0f};
      impl.drawVnxAsset(renderer, "VNX+/qlpopup@base%base.png", &baseRect);
      const SDL_FRect capRect{445.0f, 274.0f, 117.0f, 26.0f};
      impl.drawVnxAsset(renderer, impl.quickPopupAsset, &capRect);
      drawTextLine(impl.engine, renderer,
                   impl.quickPopupAsset.find("qsave") != std::string::npos
                       ? "Salvamento rápido concluído!"
                       : "Carregamento rápido concluído!",
                   20, 640.0f, 326.0f, SDL_Color{85, 42, 58, 255}, true, false);
    }
  } else if (!impl.hasVnxUi || impl.nativeTitleUi) {
    // Do not inject the compatibility title backdrop behind an authentic
    // source-created menu: the TJS Layer tree owns the entire title scene.
    if (!impl.nativeTitleUi)
      draw(impl.titlePicture, true);
    if (impl.startupExecution.subsetCompleted) {
      for (auto &layer : impl.startupExecution.layers) {
        if (!layer.visible || layer.opacity == 0)
          continue;
        if (!layer.storage.empty() &&
            !impl.failedNativePictures.count(layer.storage)) {
          try {
            const auto id = resolveVisual(*impl.assets, layer.storage, true);
            if (id.empty())
              throw std::runtime_error("asset not found");
            auto &texture = impl.assets->texture(renderer, id);
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
            const bool success = Kotonoha::RenderUpscaledTexture(
                &renderer, &texture, nullptr, &destination);
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
        // A script's fillRect commands are rendered as GPU UI rectangles,
        // not pre-rasterized software textures.
        if (!layer.rectangles.empty()) {
          SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
          for (const auto &rect : layer.rectangles) {
            const auto rgb = rect.rgb;
            const auto alpha = static_cast<Uint8>(
                std::clamp(layer.opacity * rect.opacity / 255, 0, 255));
            SDL_SetRenderDrawColor(&renderer,
                                   static_cast<Uint8>(rgb >> 16),
                                   static_cast<Uint8>(rgb >> 8),
                                   static_cast<Uint8>(rgb), alpha);
            const SDL_FRect bounds{float(layer.left + rect.left),
                                   float(layer.top + rect.top),
                                   float(rect.width), float(rect.height)};
            SDL_RenderFillRect(&renderer, &bounds);
          }
        }
        // Native drawText commands project onto the accelerated VN UI.
        for (const auto &run : layer.texts) {
          const auto alpha = static_cast<Uint8>(
              std::clamp(layer.opacity * run.opacity / 255, 0, 255));
          const auto rgb = run.rgb;
          drawTextLine(impl.engine, renderer, run.text, run.size,
                       float(layer.left + run.left),
                       float(layer.top + run.top),
                       SDL_Color{static_cast<Uint8>(rgb >> 16),
                                 static_cast<Uint8>(rgb >> 8),
                                 static_cast<Uint8>(rgb), alpha},
                       false, false);
        }
      }
    }
  }

  // 2. Render Native VNX+ Modal / Title Screens
  if (impl.hasVnxUi && impl.screenMode != Impl::ScreenMode::None) {
    const SDL_FRect fullScreen{0.0f, 0.0f, 1280.0f, 720.0f};
    impl.drawVnxAsset(renderer, "VNX+/langselect_cn@base%base.png",
                      &fullScreen);

    if (impl.screenMode == Impl::ScreenMode::Title) {
      // Soft decorative right showcase panel with Chocola & Vanilla standees
      ScnCharacterState chocolaPreview;
      chocolaPreview.name = "ショコラ";
      chocolaPreview.file = "e-mote3.0ショコラ私服a.psb";
      chocolaPreview.xpos = -60.0;
      chocolaPreview.ypos = 30.0;
      impl.emoteRenderer.renderCharacter(renderer, impl.engine, *impl.assets,
                                         chocolaPreview);

      ScnCharacterState vanillaPreview;
      vanillaPreview.name = "バニラ";
      vanillaPreview.file = "e-mote3.0バニラ私服a.psb";
      vanillaPreview.xpos = 310.0;
      vanillaPreview.ypos = 30.0;
      impl.emoteRenderer.renderCharacter(renderer, impl.engine, *impl.assets,
                                         vanillaPreview);

      // Left menu column backdrop
      const SDL_FRect menuCard{52.0f, 36.0f, 264.0f, 648.0f};
      drawRoundedPanel(renderer, menuCard, SDL_Color{255, 245, 250, 215},
                       SDL_Color{238, 152, 182, 240});

      // Title logo (`VNX+/title_cn@title__logo%layer.png` 226x146)
      const SDL_FRect logoRect{71.0f, 52.0f, 226.0f, 146.0f};
      impl.drawVnxAsset(renderer, "VNX+/title_cn@title__logo%layer.png",
                        &logoRect);

      // 5 Native VNX+ Title Buttons (`180x90`)
      const std::array<const char *, 5> btnNames{
          {"start", "load", "config", "extra", "exit"}};
      for (std::size_t i = 0; i < btnNames.size(); ++i) {
        const SDL_FRect r{94.0f, 212.0f + float(i) * 92.0f, 180.0f, 90.0f};
        const std::string prefix =
            std::string("VNX+/title_cn@") + btnNames[i] + "%button;";
        impl.drawVnxButton(renderer, prefix + "normal.png", prefix + "over.png",
                           prefix + "on.png", r);
      }

      // Subtitle banner on bottom right
      const SDL_FRect banner{470.0f, 636.0f, 750.0f, 52.0f};
      drawRoundedPanel(renderer, banner, SDL_Color{62, 34, 46, 205},
                       SDL_Color{245, 185, 205, 230});
      drawTextLine(impl.engine, renderer,
                   "NEKOPARA Vol. 0 — Edição Português (VNX+)", 22, 845.0f,
                   649.0f, SDL_Color{255, 245, 250, 255}, true);
    } else if (impl.screenMode == Impl::ScreenMode::Save ||
               impl.screenMode == Impl::ScreenMode::Load) {
      const bool isSave = (impl.screenMode == Impl::ScreenMode::Save);
      impl.drawVnxAsset(renderer,
                        isSave ? "VNX+/saveload_cn@caption__save%layer.png"
                               : "VNX+/saveload_cn@caption__load%layer.png",
                        &fullScreen);

      // 6 Save/Load Slots (2 columns x 3 rows)
      for (int i = 0; i < 6; ++i) {
        const int col = i % 2;
        const int row = i / 2;
        const float sx = 64.0f + float(col) * 462.0f;
        const float sy = 128.0f + float(row) * 165.0f;
        const SDL_FRect cardRect{sx, sy, 434.0f, 120.0f};
        impl.drawVnxAsset(renderer, "VNX+/saveload@item%button;normal.png",
                          &cardRect);

        const auto &slot = impl.saveSlots[static_cast<std::size_t>(i)];
        drawTextLine(impl.engine, renderer, "SLOT 0" + std::to_string(i + 1),
                     18, sx + 152.0f, sy + 18.0f, SDL_Color{95, 45, 65, 255},
                     false, false);
        if (slot.exists) {
          drawTextLine(impl.engine, renderer, slot.chapterTitle, 18,
                       sx + 152.0f, sy + 46.0f, SDL_Color{55, 28, 40, 255},
                       false, false);
          drawTextLine(impl.engine, renderer, slot.previewText, 14, sx + 152.0f,
                       sy + 76.0f, SDL_Color{120, 75, 92, 255}, false, false);
        } else {
          drawTextLine(impl.engine, renderer, "— Slot Vazio —", 17, sx + 152.0f,
                       sy + 52.0f, SDL_Color{150, 115, 128, 255}, false, false);
        }

        // Action badge inside card
        const SDL_FRect actionRect{sx + 18.0f, sy + 82.0f, 117.0f, 26.0f};
        if (isSave) {
          impl.drawVnxButton(
              renderer, "VNX+/saveload@item%button;save__defo.png",
              "VNX+/saveload@item%button;save__over.png",
              "VNX+/saveload@item%button;save__on.png", actionRect);
        } else if (slot.exists) {
          impl.drawVnxButton(
              renderer, "VNX+/saveload@item%button;load__defo.png",
              "VNX+/saveload@item%button;load__over.png",
              "VNX+/saveload@item%button;load__on.png", actionRect);
        } else {
          impl.drawVnxAsset(renderer,
                            "VNX+/saveload@item%button;load__disable.png",
                            &actionRect);
        }

        if (slot.exists) {
          const SDL_FRect delRect{sx + 305.0f, sy + 14.0f, 117.0f, 26.0f};
          impl.drawVnxButton(renderer, "VNX+/saveload@delete%button;normal.png",
                             "VNX+/saveload@delete%button;over.png",
                             "VNX+/saveload@delete%button;on.png", delRect);
        }
      }

      // Right-hand system navigation column (`204x74`)
      const SDL_FRect menuSaveRect{1030.0f, 150.0f, 204.0f, 74.0f};
      const SDL_FRect menuLoadRect{1030.0f, 240.0f, 204.0f, 74.0f};
      const SDL_FRect menuTitleRect{1030.0f, 330.0f, 204.0f, 74.0f};
      const SDL_FRect menuExitRect{1030.0f, 420.0f, 204.0f, 74.0f};
      impl.drawVnxButton(
          renderer, "VNX+/saveload_cn@menusave%button;normal.png",
          "VNX+/saveload_cn@menusave%button;over.png",
          "VNX+/saveload_cn@menusave%button;on.png", menuSaveRect, isSave);
      impl.drawVnxButton(
          renderer, "VNX+/saveload_cn@menuload%button;normal.png",
          "VNX+/saveload_cn@menuload%button;over.png",
          "VNX+/saveload_cn@menuload%button;on.png", menuLoadRect, !isSave);
      impl.drawVnxButton(
          renderer, "VNX+/saveload_cn@menutitle%button;normal.png",
          "VNX+/saveload_cn@menutitle%button;over.png",
          "VNX+/saveload_cn@menutitle%button;on.png", menuTitleRect);
      impl.drawVnxButton(
          renderer, "VNX+/saveload_cn@menuexit%button;normal.png",
          "VNX+/saveload_cn@menuexit%button;over.png",
          "VNX+/saveload_cn@menuexit%button;on.png", menuExitRect);

      const SDL_FRect backRect{1176.0f, 616.0f, 66.0f, 81.0f};
      impl.drawVnxButton(renderer, "VNX+/saveload@back%button;normal.png",
                         "VNX+/saveload@back%button;over.png",
                         "VNX+/saveload@back%button;on.png", backRect);
    } else if (impl.screenMode == Impl::ScreenMode::Config) {
      const SDL_FRect capRect{48.0f, 34.0f, 124.0f, 32.0f};
      impl.drawVnxAsset(renderer, "VNX+/config_cn@caption%layer.png", &capRect);

      // 3 Config Tabs: Game, Sound, Voice
      const SDL_FRect tabGame{260.0f, 22.0f, 179.0f, 92.0f};
      const SDL_FRect tabSnd{452.0f, 22.0f, 179.0f, 92.0f};
      const SDL_FRect tabVoice{644.0f, 22.0f, 179.0f, 92.0f};
      impl.drawVnxButton(renderer, "VNX+/config_cn@tab__game%button;normal.png",
                         "VNX+/config_cn@tab__game%button;over.png",
                         "VNX+/config_cn@tab__game%button;on.png", tabGame,
                         impl.configTab == 0);
      impl.drawVnxButton(renderer,
                         "VNX+/config_cn@tab__effsnd%button;normal.png",
                         "VNX+/config_cn@tab__effsnd%button;over.png",
                         "VNX+/config_cn@tab__effsnd%button;on.png", tabSnd,
                         impl.configTab == 1);
      impl.drawVnxButton(renderer,
                         "VNX+/config_cn@tab__voice%button;normal.png",
                         "VNX+/config_cn@tab__voice%button;over.png",
                         "VNX+/config_cn@tab__voice%button;on.png", tabVoice,
                         impl.configTab == 2);

      if (impl.configTab == 0) {
        const SDL_FRect frameRect{38.0f, 122.0f, 1204.0f, 510.0f};
        impl.drawVnxAsset(renderer,
                          "VNX+/config_cn@frame__game__vol01%layer.png",
                          &frameRect);
        const SDL_FRect scrModeRect{135.0f, 238.0f, 317.0f, 70.0f};
        impl.drawVnxButton(
            renderer, "VNX+/config_cn@allscrmode%cref;normal.png",
            "VNX+/config_cn@allscrmode%cref;over.png",
            "VNX+/config_cn@allscrmode%cref;on.png", scrModeRect);
        const SDL_FRect langRect{820.0f, 446.0f, 144.0f, 34.0f};
        impl.drawVnxButton(
            renderer, "VNX+/config_game@language%button;cn__defo.png",
            "VNX+/config_game@language%button;cn__over.png",
            "VNX+/config_game@language%button;cn__on.png", langRect, true);
        const char *langName =
            impl.languageIndex == 2
                ? "Idioma ativo: Português (PT-BR)"
                : (impl.languageIndex == 1 ? "Idioma ativo: English"
                                           : "Idioma ativo: 日本語");
        drawTextLine(impl.engine, renderer, langName, 18, 892.0f, 492.0f,
                     SDL_Color{95, 45, 65, 255}, true, false);
      } else if (impl.configTab == 1) {
        const SDL_FRect frameRect{243.0f, 122.0f, 794.0f, 510.0f};
        impl.drawVnxAsset(renderer,
                          "VNX+/config_cn@frame__effsnd__all%layer.png",
                          &frameRect);
      } else {
        const SDL_FRect frameRect{38.0f, 122.0f, 1204.0f, 510.0f};
        impl.drawVnxAsset(renderer, "VNX+/config_cn@frame__voice%layer.png",
                          &frameRect);
      }

      const SDL_FRect resetRect{1010.0f, 614.0f, 124.0f, 84.0f};
      impl.drawVnxButton(renderer, "VNX+/config_cn@reset%button;normal.png",
                         "VNX+/config_cn@reset%button;over.png",
                         "VNX+/config_cn@reset%button;on.png", resetRect);
      const SDL_FRect backTitleRect{1112.0f, 616.0f, 58.0f, 81.0f};
      impl.drawVnxButton(renderer, "VNX+/config@backtitle%button;normal.png",
                         "VNX+/config@backtitle%button;over.png",
                         "VNX+/config@backtitle%button;on.png", backTitleRect);
      const SDL_FRect backRect{1180.0f, 616.0f, 66.0f, 81.0f};
      impl.drawVnxButton(renderer, "VNX+/config@back%button;normal.png",
                         "VNX+/config@back%button;over.png",
                         "VNX+/config@back%button;on.png", backRect);
    } else if (impl.screenMode == Impl::ScreenMode::Backlog) {
      const SDL_FRect capRect{48.0f, 34.0f, 126.0f, 32.0f};
      impl.drawVnxAsset(renderer, "VNX+/backlog_cn@caption%layer.png",
                        &capRect);

      const SDL_FRect listBox{68.0f, 86.0f, 1080.0f, 594.0f};
      drawRoundedPanel(renderer, listBox, SDL_Color{45, 25, 35, 215},
                       SDL_Color{240, 175, 198, 225});

      const int total = static_cast<int>(impl.history.size());
      const int visibleCount = 5;
      const int maxStart = std::max(0, total - visibleCount);
      const int startIdx =
          std::clamp(total - visibleCount - impl.backlogScroll, 0, maxStart);

      if (total == 0) {
        drawTextLine(impl.engine, renderer, "Nenhuma fala registrada ainda.",
                     22, 608.0f, 360.0f, SDL_Color{235, 210, 220, 255}, true);
      } else {
        for (int i = 0; i < visibleCount && (startIdx + i) < total; ++i) {
          const auto &item =
              impl.history[static_cast<std::size_t>(startIdx + i)];
          const float rowY = 104.0f + float(i) * 112.0f;
          const SDL_FRect rowCard{88.0f, rowY, 1040.0f, 98.0f};
          drawRoundedPanel(renderer, rowCard, SDL_Color{62, 36, 48, 190},
                           SDL_Color{235, 165, 190, 140});
          if (!item.nameplate.empty()) {
            const SDL_FRect npRect{102.0f, rowY + 6.0f, 240.0f, 26.0f};
            impl.drawVnxAsset(renderer, item.nameplate, &npRect);
          } else if (!item.speaker.empty()) {
            drawTextLine(impl.engine, renderer, item.speaker, 18, 108.0f,
                         rowY + 8.0f, SDL_Color{255, 190, 212, 255});
          }
          drawTextWrapped(impl.engine, renderer, item.text, 20, 112.0f,
                          rowY + 38.0f, 996u, SDL_Color{255, 248, 252, 255});
        }
      }

      const SDL_FRect backTitleRect{1168.0f, 548.0f, 58.0f, 50.0f};
      impl.drawVnxButton(renderer, "VNX+/backlog@backtitle%button;normal.png",
                         "VNX+/backlog@backtitle%button;over.png",
                         "VNX+/backlog@backtitle%button;on.png", backTitleRect);
      const SDL_FRect backRect{1164.0f, 616.0f, 66.0f, 70.0f};
      impl.drawVnxButton(renderer, "VNX+/backlog@back%button;normal.png",
                         "VNX+/backlog@back%button;over.png",
                         "VNX+/backlog@back%button;on.png", backRect);
    } else if (impl.screenMode == Impl::ScreenMode::Extra) {
      const SDL_FRect capRect{48.0f, 34.0f, 120.0f, 32.0f};
      impl.drawVnxAsset(renderer, "VNX+/extra_cn@caption%layer.png", &capRect);
      const SDL_FRect tabsRect{243.0f, 20.0f, 794.0f, 92.0f};
      impl.drawVnxAsset(renderer, "VNX+/extra_cn@alltabs%cref;on.png",
                        &tabsRect);

      const SDL_FRect panel{64.0f, 124.0f, 1152.0f, 476.0f};
      drawRoundedPanel(renderer, panel, SDL_Color{255, 245, 250, 220},
                       SDL_Color{238, 152, 182, 235});
      drawTextLine(impl.engine, renderer,
                   "Seleção de Cenas & Galeria Extra (VNX+)", 22, 640.0f,
                   142.0f, SDL_Color{95, 42, 62, 255}, true, false);

      // 15 Chapters in a 3x5 grid
      const std::size_t count =
          std::min<std::size_t>(impl.chapterList.size(), 15);
      for (std::size_t i = 0; i < count; ++i) {
        const std::size_t col = i % 3;
        const std::size_t row = i / 3;
        const SDL_FRect cell{96.0f + float(col) * 368.0f,
                             190.0f + float(row) * 74.0f, 344.0f, 58.0f};
        const bool hov = pointInRect(impl.mouseX, impl.mouseY, cell);
        drawRoundedPanel(renderer, cell,
                         hov ? SDL_Color{235, 118, 154, 240}
                             : SDL_Color{72, 40, 54, 215},
                         SDL_Color{255, 210, 226, 230});
        drawTextLine(impl.engine, renderer,
                     "Cena " + impl.chapterList[i] + " — Iniciar", 19,
                     cell.x + cell.w * 0.5f, cell.y + 17.0f,
                     SDL_Color{255, 250, 252, 255}, true, false);
      }

      const SDL_FRect backRect{1176.0f, 616.0f, 66.0f, 71.0f};
      impl.drawVnxButton(renderer, "VNX+/extra@back%button;normal.png",
                         "VNX+/extra@back%button;over.png",
                         "VNX+/extra@back%button;on.png", backRect);
    }
  }

  // Render active Kotonoha scenes (e.g. ChoiceQuestionScene or non-VNX UI)
  Runtime::renderGameplay(renderer, scale);
}

void KirikiriRuntime::advanceDialogue() {
  auto &impl = *impl_;
  SDL_Log("Kirikiri: advanceDialogue start");
  impl.messageHiddenByUser = false;
  if (impl.dialogueVisible && impl.scnRunner && impl.scnRunner->isBlocked()) {
    impl.dialogueVisible = false;
    if (impl.textbox && !impl.hasVnxUi)
      impl.textbox->hide();
    impl.scnRunner->resumeDialogue();
    impl.processPendingJump();
    return;
  }
  if (impl.dialogueVisible && impl.runner && impl.runner->isBlocked()) {
    impl.dialogueVisible = false;
    if (impl.textbox && !impl.hasVnxUi)
      impl.textbox->hide();
    impl.runner->setBlocked(false);
    SDL_Log("Kirikiri: advanceDialogue calling runner->run()");
    impl.runner->run();
    impl.processPendingJump();
    SDL_Log("Kirikiri: advanceDialogue after run, blocked=%d finished=%d",
            impl.runner ? (impl.runner->isBlocked() ? 1 : 0) : 0,
            impl.runner ? (impl.runner->isFinished() ? 1 : 0) : 0);
  } else {
    SDL_Log("Kirikiri: advanceDialogue conditions not met");
  }
}

bool KirikiriRuntime::handleAdvanceAction() {
  auto &impl = *impl_;
  if (impl.hasVnxUi && impl.screenMode == Impl::ScreenMode::Title) {
    const auto startup = !impl.startupExecution.kagEntry.empty()
                             ? impl.startupExecution.kagEntry
                             : impl.assets->startupScenario();
    if (!startup.empty() && startScenario(startup)) {
      impl.screenMode = Impl::ScreenMode::None;
      return true;
    }
  }
  SDL_Log("Kirikiri: handleAdvanceAction dialogueVisible=%d runner=%d",
          impl.dialogueVisible ? 1 : 0,
          (impl.runner || impl.scnRunner) ? 1 : 0);
  if (impl.dialogueVisible && impl.isRunnerBlocked()) {
    SDL_Log("Kirikiri: handleAdvanceAction advancing dialogue");
    advanceDialogue();
    SDL_Log(
        "Kirikiri: handleAdvanceAction after advance, blocked=%d finished=%d",
        impl.isRunnerBlocked() ? 1 : 0, impl.isRunnerFinished() ? 1 : 0);
    return true;
  }
  SDL_Log("Kirikiri: handleAdvanceAction returning false");
  return false;
}

bool KirikiriRuntime::onEvent(Kotonoha::EventRef event) {
  auto mapped = Kotonoha::MapControllerEvent(event);
  auto &impl = *impl_;

  if (mapped.type == SDL_EVENT_MOUSE_MOTION) {
    impl.mouseX = mapped.motion.x;
    impl.mouseY = mapped.motion.y;
  } else if (mapped.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
             mapped.button.button == SDL_BUTTON_LEFT) {
    impl.mouseX = mapped.button.x;
    impl.mouseY = mapped.button.y;
    impl.mouseDown = true;
  } else if (mapped.type == SDL_EVENT_MOUSE_BUTTON_UP &&
             mapped.button.button == SDL_BUTTON_LEFT) {
    impl.mouseX = mapped.button.x;
    impl.mouseY = mapped.button.y;
    impl.mouseDown = false;
  }

  // Handle Native VNX+ UI Events
  if (impl.hasVnxUi) {
    // Modal screens (`Title`, `Save`, `Load`, `Config`, `Backlog`, `Extra`)
    if (impl.screenMode != Impl::ScreenMode::None) {
      if (mapped.type == SDL_EVENT_KEY_DOWN && !mapped.key.repeat) {
        if (mapped.key.key == SDLK_ESCAPE || mapped.key.key == SDLK_BACKSPACE ||
            mapped.key.key == SDLK_AC_BACK) {
          if (impl.screenMode != Impl::ScreenMode::Title) {
            impl.closeOverlayScreen();
            return true;
          }
        }
        if (impl.screenMode == Impl::ScreenMode::Title &&
            (mapped.key.key == SDLK_RETURN || mapped.key.key == SDLK_SPACE)) {
          const auto startup = !impl.startupExecution.kagEntry.empty()
                                   ? impl.startupExecution.kagEntry
                                   : impl.assets->startupScenario();
          if (!startup.empty() && startScenario(startup)) {
            impl.screenMode = Impl::ScreenMode::None;
            return true;
          }
        }
        if (impl.screenMode == Impl::ScreenMode::Backlog) {
          if (mapped.key.key == SDLK_UP || mapped.key.key == SDLK_PAGEUP) {
            ++impl.backlogScroll;
            return true;
          }
          if (mapped.key.key == SDLK_DOWN || mapped.key.key == SDLK_PAGEDOWN) {
            impl.backlogScroll = std::max(0, impl.backlogScroll - 1);
            return true;
          }
        }
      }
      if (mapped.type == SDL_EVENT_MOUSE_WHEEL &&
          impl.screenMode == Impl::ScreenMode::Backlog) {
        if (mapped.wheel.y > 0)
          ++impl.backlogScroll;
        else if (mapped.wheel.y < 0)
          impl.backlogScroll = std::max(0, impl.backlogScroll - 1);
        return true;
      }
      if (mapped.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
          mapped.button.button == SDL_BUTTON_RIGHT) {
        if (impl.screenMode != Impl::ScreenMode::Title) {
          impl.closeOverlayScreen();
          return true;
        }
      }
      if (mapped.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
          mapped.button.button == SDL_BUTTON_LEFT) {
        const float px = mapped.button.x;
        const float py = mapped.button.y;

        if (impl.screenMode == Impl::ScreenMode::Title) {
          for (std::size_t i = 0; i < 5; ++i) {
            const SDL_FRect r{94.0f, 212.0f + float(i) * 92.0f, 180.0f, 90.0f};
            if (!pointInRect(px, py, r))
              continue;
            if (i == 0) { // START
              const auto startup = !impl.startupExecution.kagEntry.empty()
                                       ? impl.startupExecution.kagEntry
                                       : impl.assets->startupScenario();
              if (!startup.empty() && startScenario(startup))
                impl.screenMode = Impl::ScreenMode::None;
            } else if (i == 1) { // LOAD
              impl.openScreen(Impl::ScreenMode::Load);
            } else if (i == 2) { // CONFIG
              impl.openScreen(Impl::ScreenMode::Config);
            } else if (i == 3) { // EXTRA
              impl.openScreen(Impl::ScreenMode::Extra);
            } else if (i == 4) { // EXIT
              SDL_Event quit{};
              quit.type = SDL_EVENT_QUIT;
              SDL_PushEvent(&quit);
            }
            return true;
          }
          return true;
        }

        if (impl.screenMode == Impl::ScreenMode::Save ||
            impl.screenMode == Impl::ScreenMode::Load) {
          const bool isSave = (impl.screenMode == Impl::ScreenMode::Save);
          for (int i = 0; i < 6; ++i) {
            const int col = i % 2;
            const int row = i / 2;
            const float sx = 64.0f + float(col) * 462.0f;
            const float sy = 128.0f + float(row) * 165.0f;
            const SDL_FRect cardRect{sx, sy, 434.0f, 120.0f};
            const SDL_FRect delRect{sx + 305.0f, sy + 14.0f, 117.0f, 26.0f};
            if (impl.saveSlots[static_cast<std::size_t>(i)].exists &&
                pointInRect(px, py, delRect)) {
              std::error_code ec;
              std::filesystem::remove(impl.slotPath(i), ec);
              impl.refreshSaveSlots();
              return true;
            }
            if (pointInRect(px, py, cardRect)) {
              const auto path = impl.slotPath(i).string();
              if (isSave && !impl.currentScenario.empty()) {
                saveGame(path);
                impl.refreshSaveSlots();
              } else if (!isSave &&
                         impl.saveSlots[static_cast<std::size_t>(i)].exists) {
                loadGame(path);
                impl.screenMode = Impl::ScreenMode::None;
              }
              return true;
            }
          }
          if (pointInRect(px, py, {1030.0f, 150.0f, 204.0f, 74.0f})) {
            impl.screenMode = Impl::ScreenMode::Save;
            return true;
          }
          if (pointInRect(px, py, {1030.0f, 240.0f, 204.0f, 74.0f})) {
            impl.screenMode = Impl::ScreenMode::Load;
            return true;
          }
          if (pointInRect(px, py, {1030.0f, 330.0f, 204.0f, 74.0f})) {
            impl.titleRestartRequested = true;
            return true;
          }
          if (pointInRect(px, py, {1030.0f, 420.0f, 204.0f, 74.0f})) {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
            return true;
          }
          if (pointInRect(px, py, {1176.0f, 616.0f, 66.0f, 81.0f})) {
            impl.closeOverlayScreen();
            return true;
          }
          return true;
        }

        if (impl.screenMode == Impl::ScreenMode::Config) {
          if (pointInRect(px, py, {260.0f, 22.0f, 179.0f, 92.0f})) {
            impl.configTab = 0;
            return true;
          }
          if (pointInRect(px, py, {452.0f, 22.0f, 179.0f, 92.0f})) {
            impl.configTab = 1;
            return true;
          }
          if (pointInRect(px, py, {644.0f, 22.0f, 179.0f, 92.0f})) {
            impl.configTab = 2;
            return true;
          }
          if (impl.configTab == 0 &&
              pointInRect(px, py, {820.0f, 446.0f, 144.0f, 34.0f})) {
            impl.languageIndex = (impl.languageIndex == 2)
                                     ? 1
                                     : ((impl.languageIndex == 1) ? 0 : 2);
            if (impl.scnRunner)
              impl.scnRunner->setLanguageIndex(impl.languageIndex);
            return true;
          }
          if (pointInRect(px, py, {1010.0f, 614.0f, 124.0f, 84.0f})) {
            impl.languageIndex = 2;
            if (impl.scnRunner)
              impl.scnRunner->setLanguageIndex(2);
            return true;
          }
          if (pointInRect(px, py, {1112.0f, 616.0f, 58.0f, 81.0f})) {
            impl.titleRestartRequested = true;
            return true;
          }
          if (pointInRect(px, py, {1180.0f, 616.0f, 66.0f, 81.0f})) {
            impl.closeOverlayScreen();
            return true;
          }
          return true;
        }

        if (impl.screenMode == Impl::ScreenMode::Backlog) {
          if (pointInRect(px, py, {1168.0f, 548.0f, 58.0f, 50.0f})) {
            impl.titleRestartRequested = true;
            return true;
          }
          if (pointInRect(px, py, {1164.0f, 616.0f, 66.0f, 70.0f})) {
            impl.closeOverlayScreen();
            return true;
          }
          return true;
        }

        if (impl.screenMode == Impl::ScreenMode::Extra) {
          const std::size_t count =
              std::min<std::size_t>(impl.chapterList.size(), 15);
          for (std::size_t i = 0; i < count; ++i) {
            const std::size_t col = i % 3;
            const std::size_t row = i / 3;
            const SDL_FRect cell{96.0f + float(col) * 368.0f,
                                 190.0f + float(row) * 74.0f, 344.0f, 58.0f};
            if (pointInRect(px, py, cell)) {
              if (startScenario(impl.chapterList[i]))
                impl.screenMode = Impl::ScreenMode::None;
              return true;
            }
          }
          if (pointInRect(px, py, {1176.0f, 616.0f, 66.0f, 71.0f})) {
            impl.closeOverlayScreen();
            return true;
          }
          return true;
        }
      }
      return true;
    }

    // In-game native VNX+ shortcuts & toolbar clicks
    if (!impl.currentScenario.empty() &&
        impl.screenMode == Impl::ScreenMode::None) {
      if (mapped.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
          mapped.button.button == SDL_BUTTON_RIGHT) {
        impl.openScreen(Impl::ScreenMode::Save);
        return true;
      }
      if (mapped.type == SDL_EVENT_MOUSE_WHEEL && mapped.wheel.y > 0) {
        impl.backlogScroll = 0;
        impl.openScreen(Impl::ScreenMode::Backlog);
        return true;
      }
      if (mapped.type == SDL_EVENT_KEY_DOWN && !mapped.key.repeat) {
        if (mapped.key.key == SDLK_ESCAPE || mapped.key.key == SDLK_AC_BACK) {
          impl.openScreen(Impl::ScreenMode::Save);
          return true;
        }
        if (mapped.key.key == SDLK_B || mapped.key.key == SDLK_PAGEUP) {
          impl.backlogScroll = 0;
          impl.openScreen(Impl::ScreenMode::Backlog);
          return true;
        }
        if (mapped.key.key == SDLK_S) {
          impl.skipMode = !impl.skipMode;
          return true;
        }
        if (mapped.key.key == SDLK_A) {
          impl.autoMode = !impl.autoMode;
          return true;
        }
      }
      if (mapped.type == SDL_EVENT_KEY_DOWN &&
          (mapped.key.key == SDLK_LCTRL || mapped.key.key == SDLK_RCTRL)) {
        impl.skipMode = true;
        return true;
      }
      if (mapped.type == SDL_EVENT_KEY_UP &&
          (mapped.key.key == SDLK_LCTRL || mapped.key.key == SDLK_RCTRL)) {
        impl.skipMode = false;
        return true;
      }
      if (mapped.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
          mapped.button.button == SDL_BUTTON_LEFT && impl.dialogueVisible &&
          !impl.messageHiddenByUser) {
        const float px = mapped.button.x;
        const float py = mapped.button.y;
        // Check toolbar clicks
        if (pointInRect(px, py, {580.0f, 665.0f, 86.0f, 22.0f})) {
          saveGame(impl.quickSavePath().string());
          impl.quickPopupAsset = "VNX+/qlpopup@qsave%layer.png";
          impl.quickPopupUntilMs = SDL_GetTicks() + 1600;
          return true;
        }
        if (pointInRect(px, py, {680.0f, 665.0f, 90.0f, 22.0f})) {
          std::error_code ec;
          if (std::filesystem::exists(impl.quickSavePath(), ec)) {
            loadGame(impl.quickSavePath().string());
            impl.quickPopupAsset = "VNX+/qlpopup@qload%layer.png";
            impl.quickPopupUntilMs = SDL_GetTicks() + 1600;
          }
          return true;
        }
        if (pointInRect(px, py, {784.0f, 665.0f, 44.0f, 22.0f})) {
          impl.openScreen(Impl::ScreenMode::Save);
          return true;
        }
        if (pointInRect(px, py, {842.0f, 665.0f, 46.0f, 22.0f})) {
          impl.openScreen(Impl::ScreenMode::Load);
          return true;
        }
        if (pointInRect(px, py, {902.0f, 665.0f, 88.0f, 22.0f})) {
          impl.autoMode = !impl.autoMode;
          impl.nextAutoAdvanceMs = SDL_GetTicks() + 2400;
          return true;
        }
        if (pointInRect(px, py, {1004.0f, 665.0f, 40.0f, 22.0f})) {
          impl.skipMode = !impl.skipMode;
          return true;
        }
        if (pointInRect(px, py, {1058.0f, 665.0f, 60.0f, 22.0f})) {
          impl.openScreen(Impl::ScreenMode::Config);
          return true;
        }
        if (pointInRect(px, py, {1130.0f, 665.0f, 64.0f, 22.0f})) {
          impl.backlogScroll = 0;
          impl.openScreen(Impl::ScreenMode::Backlog);
          return true;
        }
      }
    }
  }

  const auto nativeFallback = [&] {
    impl.nativeTitleUi = false;
    impl.nativeTitleDisabled = true;
    impl.titleShown = false;
    impl.choiceScene->hide();
    if (impl.engine)
      initGameplay(*impl.engine);
  };
  if (impl.currentScenario.empty() && impl.nativeTitleUi &&
      mapped.type == SDL_EVENT_KEY_DOWN && !mapped.key.repeat) {
    int virtualKey = 0;
    switch (mapped.key.key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER: virtualKey = 13; break;
    case SDLK_ESCAPE: virtualKey = 27; break;
    case SDLK_SPACE: virtualKey = 32; break;
    case SDLK_LEFT: virtualKey = 37; break;
    case SDLK_UP: virtualKey = 38; break;
    case SDLK_RIGHT: virtualKey = 39; break;
    case SDLK_DOWN: virtualKey = 40; break;
    default: break;
    }
    if (virtualKey > 0 && Kotonoha::Providers::dispatchTjsUiKeyDown(
                              impl.startupExecution, virtualKey)) {
      impl.failedNativePictures.clear();
      const auto &run = impl.startupExecution;
      if (!run.error.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "KiriKiri: TJS2 keyboard handler failed: %s",
                    run.error.c_str());
        nativeFallback();
      } else if (!run.kagEntry.empty()) {
        const auto target = run.kagEntry;
        if (startScenario(target)) {
          impl.nativeTitleUi = false;
          impl.choiceScene->hide();
        } else {
          SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                      "KiriKiri: keyboard requested unavailable KAG: %s",
                      target.c_str());
          nativeFallback();
        }
      }
      return true;
    }
  }

  if (impl.currentScenario.empty() && impl.nativeTitleUi &&
      mapped.type == SDL_EVENT_MOUSE_MOTION) {
    const bool hover = Kotonoha::Providers::dispatchTjsUiHover(
        impl.startupExecution, mapped.motion.x, mapped.motion.y);
    const bool motion = Kotonoha::Providers::dispatchTjsUiMouseMove(
        impl.startupExecution, mapped.motion.x, mapped.motion.y);
    if (hover || motion) {
      impl.failedNativePictures.clear();
      const auto &run = impl.startupExecution;
      if (!run.error.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "KiriKiri: TJS2 pointer movement failed: %s",
                    run.error.c_str());
        nativeFallback();
      } else if (!run.kagEntry.empty()) {
        const auto target = run.kagEntry;
        if (startScenario(target)) {
          impl.nativeTitleUi = false;
          impl.choiceScene->hide();
        } else {
          SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                      "KiriKiri: pointer motion requested missing KAG: %s",
                      target.c_str());
          nativeFallback();
        }
      }
      return true;
    }
  }
  if (impl.currentScenario.empty() && impl.nativeTitleUi &&
      mapped.type == SDL_EVENT_MOUSE_BUTTON_UP &&
      mapped.button.button == SDL_BUTTON_LEFT) {
    if (Kotonoha::Providers::dispatchTjsUiMouseUp(
            impl.startupExecution, mapped.button.x, mapped.button.y)) {
      impl.failedNativePictures.clear();
      const auto &run = impl.startupExecution;
      if (!run.error.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "KiriKiri: TJS2 release handler failed: %s",
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
      impl.skipMode = false;
      advanceDialogue();
      return true;
    }
  }
  if (mapped.type == SDL_EVENT_KEY_DOWN && !mapped.key.repeat &&
      mapped.key.key == SDLK_F5) {
    auto path =
        Kotonoha::preferredDocumentsDirectory("kirikiri") / "quicksave.kns";
    saveGame(path.string());
    impl.quickPopupAsset = "VNX+/qlpopup@qsave%layer.png";
    impl.quickPopupUntilMs = SDL_GetTicks() + 1600;
    return true;
  }
  if (mapped.type == SDL_EVENT_KEY_DOWN && !mapped.key.repeat &&
      mapped.key.key == SDLK_F6) {
    auto path =
        Kotonoha::preferredDocumentsDirectory("kirikiri") / "quicksave.kns";
    loadGame(path.string());
    impl.quickPopupAsset = "VNX+/qlpopup@qload%layer.png";
    impl.quickPopupUntilMs = SDL_GetTicks() + 1600;
    return true;
  }
  return Runtime::onEvent(mapped);
}

void KirikiriRuntime::saveGame(const std::string &path) {
  auto &impl = *impl_;
  std::ostringstream data;
  if (impl.scnRunner) {
    const auto scnState = impl.scnRunner->saveState();
    data << "KIRIKIRI_SAVE_5\n" << std::quoted(impl.currentScenario) << '\n';
    data << scnState.sceneIndex << ' ' << scnState.lineIndex << ' '
         << scnState.savepointId << ' ' << (scnState.finished ? 1 : 0) << '\n';
    data << std::quoted(scnState.snapshotJson) << '\n';
    data << scnState.unlockedFlags.size() << '\n';
    for (const auto &flag : scnState.unlockedFlags)
      data << std::quoted(flag) << '\n';
    Kotonoha::GetVNManagerRuntime(*impl.engine).save(path, data.str());
    return;
  }
  const auto state =
      impl.runner ? impl.runner->saveState() : KagScript::Runner::State{};
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
  if (header == "KIRIKIRI_SAVE_5") {
    ScnRunner::State scnState;
    unsigned finished = 0;
    std::size_t flagCount = 0;
    if (!(data >> std::quoted(scenario) >> scnState.sceneIndex >>
          scnState.lineIndex >> scnState.savepointId >> finished >>
          std::quoted(scnState.snapshotJson) >> flagCount) ||
        finished > 1 || flagCount > 65536)
      throw std::runtime_error("Invalid Kirikiri v5 save header");
    scnState.finished = finished != 0;
    for (std::size_t i = 0; i < flagCount; ++i) {
      std::string flag;
      if (!(data >> std::quoted(flag)))
        throw std::runtime_error("Truncated Kirikiri v5 flag table");
      scnState.unlockedFlags.insert(std::move(flag));
    }
    if (scenario.empty() || !startScenario(scenario))
      throw std::runtime_error("Kirikiri save references missing scenario");
    if (impl_->scnRunner) {
      impl_->scnRunner->restoreState(scnState);
      impl_->scnRunner->run();
      impl_->processPendingJump();
    }
    return;
  }
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

  impl.currentScenario = target;
  impl.sceneAudio.clear();
  impl.lastVoiceAssetId.clear();
  impl.screenMode = Impl::ScreenMode::None;
  impl.messageHiddenByUser = false;
  impl.waitingWait = false;
  impl.finishWait = {};
  impl.waitingChoice = false;
  impl.pendingJumpStorage.clear();
  impl.pendingJumpTarget.clear();
  impl.runner.reset();
  impl.scnRunner.reset();
  impl.emoteRenderer.clearFailedCache();
  impl.nameplateVisual = {};
  scenarioTexts_.clear();

  const auto playAudio = [this](Kotonoha::PcmPlayer &player,
                                const std::string &file,
                                const std::string &channel, bool loop) {
    if (file.empty() || !impl_->engine)
      return;
    const std::string resolved = resolveAudio(*impl_->assets, file);
    if (resolved.empty()) {
      if (impl_->warnedMissingAssets.insert("audio:" + file).second)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "KiriKiri: audio asset unavailable: %s", file.c_str());
      return;
    }
    try {
      auto decoded = impl_->assets->loadAudio(resolved);
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
      if (channel == "VOICE")
        impl_->lastVoiceAssetId = resolved;
      const auto found =
          std::find_if(impl_->sceneAudio.begin(), impl_->sceneAudio.end(),
                       [&](const auto &item) { return item.id == resolved; });
      if (found == impl_->sceneAudio.end()) {
        impl_->sceneAudio.push_back({resolved, {}, {}});
        if (impl_->sceneAudio.size() > 256)
          impl_->sceneAudio.erase(impl_->sceneAudio.begin());
      }
    } catch (const std::exception &error) {
      if (impl_->warnedMissingAssets.insert("audio:" + resolved).second)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "KiriKiri: cannot play audio %s: %s", resolved.c_str(),
                    error.what());
    }
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
    dialogueSpeaker_ = impl_->engine
                           ? impl_->engine->GetTranslation().text(0, speaker)
                           : speaker;
    dialogueText_ =
        impl_->engine ? impl_->engine->GetTranslation().text(0, text) : text;
    auto &impl = *impl_;
    impl.dialogueVisible = true;
    impl.messageHiddenByUser = false;
    impl.nextAutoAdvanceMs =
        SDL_GetTicks() + static_cast<std::uint64_t>(std::clamp<std::size_t>(
                             dialogueText_.size() * 42 + 1200, 1800, 5200));
    impl.history.push_back(
        {dialogueSpeaker_, dialogueText_, impl.dialogueNameplate});
    if (impl.history.size() > 200)
      impl.history.erase(impl.history.begin());
    if (impl.textbox) {
      impl.textbox->setDialogue(dialogueSpeaker_, dialogueText_);
      if (!impl.hasVnxUi)
        impl.textbox->show();
      else
        impl.textbox->hide();
    }
  };

  callbacks.executeInlineTjs = [this, target](std::string source) {
    auto &state = *impl_;
    const bool success = Kotonoha::Providers::executeTjsInlineSubset(
        state.startupExecution, *state.assets, source, target);
    if (!success) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "KiriKiri: inline TJS2 in %s: %s", target.c_str(),
                  state.startupExecution.error.c_str());
      return; // Keep unsupported scripts visible as diagnostics.
    }
    const auto requested = state.startupExecution.kagEntry;
    if (!requested.empty() && requested != target) {
      const auto separator = requested.find('#');
      state.pendingJumpStorage = requested.substr(0, separator);
      state.pendingJumpTarget = separator == std::string::npos
                                    ? std::string()
                                    : requested.substr(separator + 1);
    }
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
      return impl.engine ? impl.engine->GetTranslation().text(0, value) : value;
    };
    std::vector<Kotonoha::ChoiceOption> choiceOptions;
    int id = 0;
    for (auto &opt : options)
      choiceOptions.push_back({translate(opt), id++});
    if (impl.choiceScene) {
      impl.choiceScene->setQuestion(translate(question), choiceOptions,
                                    [this, onChoose = std::move(onChoose)](
                                        int optionId, const std::string &) {
                                      auto &impl = *impl_;
                                      impl.waitingChoice = false;
                                      if (impl.runner)
                                        impl.runner->setBlocked(false);
                                      onChoose(optionId);
                                      impl.runActive();
                                      impl.processPendingJump();
                                    });
    }
  };

  callbacks.jump = [this](std::string label) {
    auto &impl = *impl_;
    if (label.empty())
      return;
    const auto hash = label.find('#');
    if (hash != std::string::npos) {
      const std::string storage = label.substr(0, hash);
      const std::string targetLabel = label.substr(hash + 1);
      if (storage.empty() ||
          scenarioTitle(storage) == scenarioTitle(impl.currentScenario)) {
        if (!targetLabel.empty())
          debugJumpToLabel(targetLabel);
      } else {
        impl.pendingJumpStorage = storage;
        impl.pendingJumpTarget = targetLabel;
      }
      return;
    }
    if (!label.empty() && label.front() != '*' && !debugJumpToLabel(label)) {
      impl.pendingJumpStorage = label;
      impl.pendingJumpTarget.clear();
    } else {
      debugJumpToLabel(label);
    }
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
      if (state.warnedMissingAssets.insert("bg:" + file).second)
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
      if (state.warnedMissingAssets.insert("fg:" + file).second)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "KiriKiri: foreground image not found: %s", file.c_str());
      return;
    }
    auto &visual = state.characters[layer];
    visual.id = id;
    visual.x = x;
    visual.y = y;
    visual.decodeFailed = false;
    visual.visible = true;
    // Layers 0,1,2,... are ordered numerically, not lexically.
    char *end = nullptr;
    const long numeric = std::strtol(layer.c_str(), &end, 10);
    if (end != layer.c_str() && end && *end == '\0' &&
        numeric >= -10000 && numeric <= 10000)
      visual.order = static_cast<int>(numeric);
    if (transitionMs > 0)
      SDL_LogDebug(
          SDL_LOG_CATEGORY_APPLICATION,
          "KiriKiri: foreground transition %dms is currently immediate",
          transitionMs);
  };

  callbacks.hideChar = [this](std::string layer, int) {
    impl_->characters.erase(layer);
  };

  callbacks.updateLayer = [this](KagScript::LayerOptions options) {
    auto &state = *impl_;
    Impl::Visual *visual = nullptr;
    if (options.layer == "base") {
      visual = &state.background;
    } else {
      const auto it = state.characters.find(options.layer);
      if (it != state.characters.end())
        visual = &it->second;
    }
    // [layopt] before [image] should not create an empty drawable.
    if (!visual)
      return;
    if (options.left)
      visual->x = *options.left;
    if (options.top)
      visual->y = *options.top;
    if (options.visible)
      visual->visible = *options.visible;
    if (options.opacity)
      visual->opacity = std::clamp(*options.opacity, 0, 255);
    if (options.order)
      visual->order = *options.order;
  };

  callbacks.playBgm = [this, playAudio](std::string file, bool loop) {
    playAudio(impl_->bgm, file, "BGM", loop);
  };

  callbacks.playSe = [this, playAudio](std::string file) {
    playAudio(impl_->sound, file, "SE", false);
  };

  callbacks.stopBgm = [this]() { impl_->bgm.Stop(); };

  callbacks.setVar = [this](std::string name, std::string value) {
    if (value == "1")
      impl_->unlockedFlags.insert(name);
    SDL_Log("Kirikiri: set %s=%s", name.c_str(), value.c_str());
  };

  callbacks.getVar = [this](std::string name) -> std::string {
    return impl_->unlockedFlags.count(name) ? "1" : std::string{};
  };

  std::string lowerTarget = target;
  for (char &c : lowerTarget)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (!savedState && lowerTarget.size() >= 4 &&
      lowerTarget.substr(lowerTarget.size() - 4) == ".scn") {
    try {
      const auto rawBytes = impl.assets->read(target);
      if (Kotonoha::Providers::PsbFile::isMdfBytes(rawBytes.data(),
                                                   rawBytes.size()) ||
          Kotonoha::Providers::PsbFile::isPsbBytes(rawBytes.data(),
                                                   rawBytes.size())) {
        auto psb = Kotonoha::Providers::PsbFile::parse(rawBytes);
        if (psb.root()["scenes"].isList() && psb.root()["scenes"].size() > 0) {
          for (const auto &scn : psb.root()["scenes"].asList()) {
            const auto &txList = scn["texts"];
            if (!txList.isList())
              continue;
            for (const auto &tx : txList.asList()) {
              if (!tx.isList() || tx.size() < 3 || !tx[2].isList())
                continue;
              const auto &langs = tx[2];
              const std::size_t li =
                  langs.size() > 2 && langs[2].isList() &&
                          langs[2].size() >= 2 &&
                          !langs[2][1].asString().empty()
                      ? 2
                      : (langs.size() > 1 && langs[1].isList() &&
                                 langs[1].size() >= 2 &&
                                 !langs[1][1].asString().empty()
                             ? 1
                             : 0);
              if (langs[li].isList() && langs[li].size() >= 2) {
                const std::string lineText =
                    ScnRunner::stripFormatting(langs[li][1].asString());
                if (!lineText.empty() &&
                    std::find(scenarioTexts_.begin(), scenarioTexts_.end(),
                              lineText) == scenarioTexts_.end())
                  scenarioTexts_.push_back(lineText);
              }
            }
          }

          ScnRunner::Callbacks scnCb;
          scnCb.kag = callbacks;
          scnCb.showDialogue = [this, playAudio, showText = callbacks.showText](
                                   std::string speaker, std::string text,
                                   std::string nameplate,
                                   std::vector<std::string> voices) {
            auto &state = *impl_;
            state.dialogueNameplate = nameplate;
            const std::string resolvedPlate =
                resolveVisual(*state.assets, nameplate, true);
            if (!resolvedPlate.empty())
              state.nameplateVisual = {resolvedPlate, 88, 466, false};
            else
              state.nameplateVisual = {};
            for (std::size_t i = 0;
                 i < voices.size() && i < state.voices.size(); ++i) {
              playAudio(state.voices[i], voices[i], "VOICE", false);
            }
            if (showText)
              showText(std::move(speaker), std::move(text));
          };
          scnCb.unlockFlag = [this](std::string flag) {
            impl_->unlockedFlags.insert(std::move(flag));
          };
          scnCb.setMessageWindowVisible = [this](bool visible) {
            auto &state = *impl_;
            if (!visible) {
              state.dialogueVisible = false;
              if (state.textbox)
                state.textbox->hide();
            }
          };
          scnCb.returnToTitle = [this]() {
            impl_->titleRestartRequested = true;
          };

          impl.scnRunner = std::make_unique<ScnRunner>(
              std::move(psb), std::move(scnCb), impl.languageIndex);
          if (position > 0) {
            ScnRunner::State posState;
            posState.lineIndex = position;
            impl.scnRunner->restoreState(posState);
          }
          impl.running = true;
          impl.scnRunner->run();
          impl.processPendingJump();
          return true;
        }
      }
    } catch (const std::exception &e) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "KiriKiri: ScnRunner fallback for %s: %s", target.c_str(),
                  e.what());
    }
  }

  auto text = impl.assets->loadTextUtf8(target);
  SDL_Log("Kirikiri: startScenario '%s' read %zu bytes", target.c_str(),
          text.size());
  auto script = KagScript::parse(text, target);
  SDL_Log("Kirikiri: parsed %zu tags", script.tags.size());

  for (const auto &tag : script.tags)
    if (tag.type == KagScript::TagType::Text && !tag.text.empty() &&
        std::find(scenarioTexts_.begin(), scenarioTexts_.end(), tag.text) ==
            scenarioTexts_.end())
      scenarioTexts_.push_back(tag.text);

  impl.runner =
      std::make_unique<KagScript::Runner>(script, std::move(callbacks));
  if (savedState)
    impl.runner->restoreState(*savedState);
  else
    impl.runner->restorePosition(position);
  impl.running = true;
  impl.runner->run();
  impl.processPendingJump();
  return true;
}

bool KirikiriRuntime::debugJumpToLabel(const std::string &label) {
  auto &impl = *impl_;
  const auto hash = label.find('#');
  if (hash != std::string::npos) {
    const std::string storage = label.substr(0, hash);
    const std::string target = label.substr(hash + 1);
    if (!storage.empty() &&
        scenarioTitle(storage) != scenarioTitle(impl.currentScenario)) {
      if (!startScenario(storage))
        return false;
      return target.empty() ? true : debugJumpToLabel(target);
    }
    return debugJumpToLabel(target);
  }
  if (impl.scnRunner) {
    if (!impl.scnRunner->jumpToLabel(label))
      return false;
    impl.scnRunner->run();
    impl.processPendingJump();
    return true;
  }
  if (!impl.runner)
    return false;
  if (!impl.runner->jumpToLabel(label))
    return false;
  impl.runner->run();
  impl.processPendingJump();
  return true;
}

std::string KirikiriRuntime::debugNode() const {
  auto &impl = *impl_;
  return impl.currentScenario.empty()
             ? std::string()
             : ("kirikiri | scenario " + impl.currentScenario);
}

std::string KirikiriRuntime::debugNodeCatalogKey() const {
  return impl_->currentScenario;
}

std::vector<Kotonoha::DebugNode> KirikiriRuntime::debugNodes() const {
  std::vector<Kotonoha::DebugNode> nodes;
  const auto &impl = *impl_;
  if (!impl.assets)
    return nodes;
  for (const auto &script : impl.assets->scenarioFiles()) {
    Kotonoha::DebugNode node{
        "kirikiri | scenario " + script, scenarioTitle(script), {script}};
    if (script == impl.currentScenario) {
      const auto add = [&](const std::string &id) {
        if (!id.empty() && impl.assets->contains(id) &&
            std::find(node.assets.begin(), node.assets.end(), id) ==
                node.assets.end())
          node.assets.push_back(id);
      };
      add(impl.background.id);
      add(impl.nameplateVisual.id);
      for (const auto &[slot, visual] : impl.characters)
        add(visual.id);
      for (const auto &voice : impl.sceneAudio)
        add(voice.id);
    }
    nodes.push_back(std::move(node));
  }
  return nodes;
}

void KirikiriRuntime::debugJumpToNode(const std::string &id) {
  constexpr std::string_view prefix = "kirikiri | scenario ";
  if (id.compare(0, prefix.size(), prefix) != 0 ||
      !startScenario(id.substr(prefix.size())))
    throw std::invalid_argument("Unknown Kirikiri debug scenario: " + id);
  SetState(Kotonoha::GameState::Gameplay);
}

std::vector<Kotonoha::DebugAudioAsset> KirikiriRuntime::debugSceneAudio() {
  auto rows = impl_->sceneAudio;
  // Annotate only the most recently played native VOICE clip with its active
  // dialogue; BGM and sound effects must never be attributed to a character.
  if (!originalDialogueText_.empty() && !impl_->lastVoiceAssetId.empty())
    for (auto it = rows.rbegin(); it != rows.rend(); ++it)
      if (it->id == impl_->lastVoiceAssetId) {
        it->character = originalDialogueSpeaker_;
        it->text = originalDialogueText_;
        break;
      }
  return rows;
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
