// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/DebugCatalog.hpp>
#include <N2System/ImageEffects.hpp>
#include <N2System/N2Runtime.hpp>
#include <N2System/ScriptIndex.hpp>

#include <Kotonoha/Filesystem/AtomicFile.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/components/ChoiceQuestionScene.hpp>
#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/components/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/InputMapper.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <Kotonoha/events/VideoSkipGesture.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace N2System {

struct N2Runtime::Impl {
  std::shared_ptr<Kotonoha::VNTextBox> textBox;
  std::shared_ptr<Kotonoha::VNBacklog> backlog;
  std::shared_ptr<Kotonoha::InGameMenu> pauseMenu;
  std::unique_ptr<Kotonoha::UI> sgUi;
  bool dialogueHidden = false;
  std::string activeScrollBarDrag;
  std::unique_ptr<Kotonoha::MediaPlayer> moviePlayer;
  Kotonoha::VideoSkipGesture videoSkip;
  std::unique_ptr<Kotonoha::MediaPlayer> bgmPlayer;
  std::unique_ptr<Kotonoha::MediaPlayer> voicePlayer;
  std::unique_ptr<Kotonoha::MediaPlayer> sePlayer;
  Kotonoha::Engine *engine = nullptr;
  std::unique_ptr<Kotonoha::ChoiceQuestionScene> choiceScene;
  bool choiceActive = false;
  bool restartRequested = false;
  bool quitRequested = false;
  int focusedChoiceIndex = 0;
  std::vector<std::string> lastChoices;
  std::unordered_map<std::string, SDL_Point> imageSizeCache;
  std::unordered_map<std::string, Kotonoha::TexturePtr> effectTextures;
  SDL_Renderer *effectRenderer = nullptr;
  std::string currentBgm;
  std::string currentVoice;
  std::string currentSe;
  std::string currentMovie;
  std::string lastBacklogText;
  std::string nativeScript;
  std::string nativeScriptId;
  ScriptIndex nativeIndex;
  bool nsbMode = false;
  SDL_Point viewportSize{1280, 720};
  std::unique_ptr<NsbVm> nsbVm;
  std::unique_ptr<Ponscripter::PonscripterRuntime> ponscripter;

  static bool startsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
           text.compare(0, prefix.size(), prefix) == 0;
  }

  static bool isChoiceOverlayState(std::string_view name) {
    std::string lower(name);
    for (char &ch : lower)
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return lower.find("/mouseover/") != std::string::npos ||
           lower.find("/mouseclick/") != std::string::npos ||
           lower.find("/mouseleave/") != std::string::npos;
  }

  void drawSgText(Kotonoha::RendererRef renderer, const std::string &text,
                  float x, float y, float wrapW, float fontSize,
                  SDL_Color color) {
    if (text.empty())
      return;
    Kotonoha::UI *ui = sgUi ? sgUi.get()
                           : (engine ? &engine->GetVNUI() : nullptr);
    if (!ui)
      return;
    // Both shadow and foreground share the renderer's glyph atlas. This
    // path does not allocate SDL surfaces/textures on every frame.
    ui->textSized(renderer, text, x + 1.5f, y + 1.5f, wrapW, fontSize,
                  {0, 0, 0, 210});
    ui->textSized(renderer, text, x, y, wrapW, fontSize, color);
  }

  SDL_Texture *effectTexture(Kotonoha::RendererRef renderer,
                             N2ArchiveAssets &assets,
                             const std::string &path,
                             bool monochrome, unsigned blur) {
    if (!monochrome && blur == 0)
      return &assets.texture(renderer, path);
    if (effectRenderer != &renderer) {
      effectTextures.clear();
      effectRenderer = &renderer;
    }
    const std::string key = path + "|" + (monochrome ? "mono" : "color") +
                            "|" + std::to_string(blur);
    if (auto it = effectTextures.find(key); it != effectTextures.end())
      return it->second.get();
    auto source = assets.loadSurface(path);
    if (!source)
      return &assets.texture(renderer, path);
    Kotonoha::SurfacePtr rgba(
        SDL_ConvertSurface(source.get(), SDL_PIXELFORMAT_RGBA32));
    // Avoid blocking the render thread with many 3x3 blur passes over a
    // giant background; retaining the original image is preferable to a
    // blank frame if a portable effect exceeds the preparation budget.
    if (!rgba || rgba->w <= 0 || rgba->h <= 0 ||
        std::uint64_t(rgba->w) * std::uint64_t(rgba->h) >
            (blur ? 1024u * 1024u : 2048u * 2048u))
      return &assets.texture(renderer, path);
    const std::size_t stride = std::size_t(rgba->w) * 4;
    std::vector<uint8_t> pixels(stride * std::size_t(rgba->h));
    if (!SDL_LockSurface(rgba.get()))
      return &assets.texture(renderer, path);
    for (int row = 0; row < rgba->h; ++row)
      std::memcpy(pixels.data() + row * stride,
                  static_cast<const uint8_t *>(rgba->pixels) +
                      std::size_t(row) * rgba->pitch, stride);
    SDL_UnlockSurface(rgba.get());

    if (monochrome)
      grayscaleRgba(pixels);
    if (blur && !boxBlurRgba(pixels, rgba->w, rgba->h, blur))
      return &assets.texture(renderer, path);

    if (!SDL_LockSurface(rgba.get()))
      return &assets.texture(renderer, path);
    for (int row = 0; row < rgba->h; ++row)
      std::memcpy(static_cast<uint8_t *>(rgba->pixels) +
                      std::size_t(row) * rgba->pitch,
                  pixels.data() + row * stride, stride);
    SDL_UnlockSurface(rgba.get());

    Kotonoha::TexturePtr texture(
        SDL_CreateTextureFromSurface(&renderer, rgba.get()));
    if (!texture)
      return &assets.texture(renderer, path);
    SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);
    if (effectTextures.size() >= 256)
      effectTextures.clear(); // Drop old renderer textures, never the provider.
    auto *result = texture.get();
    effectTextures.emplace(key, std::move(texture));
    return result;
  }

  std::string resolveImagePath(const SceneGraph &graph,
                               const std::string &rawPath) const {
    if (rawPath.empty())
      return {};
    if (rawPath.front() == '@') {
      if (const auto *ref = graph.findEntity(rawPath)) {
        if (!ref->imagePath.empty() && ref->imagePath != rawPath)
          return resolveImagePath(graph, ref->imagePath);
      }
      return {};
    }
    return rawPath;
  }

  bool queryImageSize(N2ArchiveAssets *assets, const SceneGraph &graph,
                      const std::string &rawPath, int &outW, int &outH) {
    const std::string resolved = resolveImagePath(graph, rawPath);
    if (resolved.empty() || !assets)
      return false;
    auto it = imageSizeCache.find(resolved);
    if (it != imageSizeCache.end()) {
      outW = it->second.x;
      outH = it->second.y;
      return outW > 0 && outH > 0;
    }
    if (!assets->contains(resolved)) {
      imageSizeCache[resolved] = SDL_Point{0, 0};
      return false;
    }
    try {
      auto surf = assets->loadSurface(resolved);
      if (surf && surf->w > 0 && surf->h > 0) {
        imageSizeCache[resolved] = SDL_Point{surf->w, surf->h};
        outW = surf->w;
        outH = surf->h;
        return true;
      }
    } catch (...) {
    }
    imageSizeCache[resolved] = SDL_Point{0, 0};
    return false;
  }

  const Entity *findChoiceHitEntity(const SceneGraph &graph,
                                    const std::string &choiceName) const {
    if (const auto *e = graph.findEntity(choiceName + "/MouseUsual/hit"))
      return e;
    if (const auto *e = graph.findEntity(choiceName + "/MouseUsual/img"))
      return e;
    const std::string usualPrefix = choiceName + "/MouseUsual/";
    for (const auto &[name, entity] : graph.entities()) {
      if (startsWith(name, usualPrefix))
        return &entity;
    }
    if (const auto *e = graph.findEntity(choiceName + "/MouseOver/img"))
      return e;
    const std::string overPrefix = choiceName + "/MouseOver/";
    for (const auto &[name, entity] : graph.entities()) {
      if (startsWith(name, overPrefix))
        return &entity;
    }
    return graph.findEntity(choiceName + "/img");
  }

  std::optional<SDL_FRect> choiceBounds(N2ArchiveAssets *assets,
                                        const SceneGraph &graph,
                                        const std::string &choiceName) {
    const Entity *e = findChoiceHitEntity(graph, choiceName);
    if (!e)
      return std::nullopt;
    const float x = e->x + e->shakeOffsetX;
    const float y = e->y + e->shakeOffsetY;
    float w = 0.0f;
    float h = 0.0f;
    if (e->hasClipRect && e->clipW > 0.0f && e->clipH > 0.0f) {
      w = e->clipW * e->scaleX;
      h = e->clipH * e->scaleY;
    } else if (e->width > 0.0f && e->height > 0.0f) {
      w = e->width * e->scaleX;
      h = e->height * e->scaleY;
    } else if (!e->imagePath.empty()) {
      int iw = 0, ih = 0;
      if (queryImageSize(assets, graph, e->imagePath, iw, ih)) {
        w = static_cast<float>(iw) * e->scaleX;
        h = static_cast<float>(ih) * e->scaleY;
      }
    }
    if (w <= 0.0f || h <= 0.0f)
      return std::nullopt;
    return SDL_FRect{x, y, w, h};
  }

  bool hasNativeChoices(N2ArchiveAssets *assets) {
    if (!nsbMode || !nsbVm || !nsbVm->waitingForChoice())
      return false;
    const auto &graph = nsbVm->sceneGraph();
    for (const auto &choice : nsbVm->activeChoices()) {
      if (choiceBounds(assets, graph, choice).has_value())
        return true;
    }
    return false;
  }

  int hitTestNativeChoice(N2ArchiveAssets *assets, float x, float y) {
    if (!nsbMode || !nsbVm || !nsbVm->waitingForChoice())
      return -1;
    const auto &graph = nsbVm->sceneGraph();
    const auto &choices = nsbVm->activeChoices();
    for (std::size_t i = 0; i < choices.size(); ++i) {
      const auto rect = choiceBounds(assets, graph, choices[i]);
      if (rect && x >= rect->x && x < rect->x + rect->w && y >= rect->y &&
          y < rect->y + rect->h) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  bool hasOverSprite(const SceneGraph &graph,
                     const std::string &choiceName) const {
    const std::string overPrefix = choiceName + "/MouseOver/";
    for (const auto &[name, entity] : graph.entities()) {
      if (startsWith(name, overPrefix) && entity.type == EntityType::Sprite &&
          !resolveImagePath(graph, entity.imagePath).empty()) {
        return true;
      }
    }
    return false;
  }

  bool moveNativeChoiceFocus(int dir) {
    if (!nsbMode || !nsbVm || !nsbVm->waitingForChoice())
      return false;
    const auto &choices = nsbVm->activeChoices();
    if (choices.empty())
      return false;
    const int count = static_cast<int>(choices.size());
    if (focusedChoiceIndex < 0 || focusedChoiceIndex >= count)
      focusedChoiceIndex = 0;

    const auto &graph = nsbVm->sceneGraph();
    const std::string &cur =
        choices[static_cast<std::size_t>(focusedChoiceIndex)];
    const Entity *focusEnt = findChoiceHitEntity(graph, cur);
    if (!focusEnt)
      focusEnt = graph.findEntity(cur);
    if (focusEnt) {
      std::string target;
      if (dir == 0)
        target = focusEnt->focusUp;
      else if (dir == 1)
        target = focusEnt->focusDown;
      else if (dir == 2)
        target = focusEnt->focusLeft;
      else if (dir == 3)
        target = focusEnt->focusRight;

      if (!target.empty()) {
        const auto slash = target.find('/');
        const std::string targetChoice =
            slash == std::string::npos ? target : target.substr(0, slash);
        for (int i = 0; i < count; ++i) {
          if (choices[static_cast<std::size_t>(i)] == targetChoice) {
            focusedChoiceIndex = i;
            return true;
          }
        }
      }
    }

    if (dir == 0 || dir == 2)
      focusedChoiceIndex = (focusedChoiceIndex + count - 1) % count;
    else
      focusedChoiceIndex = (focusedChoiceIndex + 1) % count;
    return true;
  }

  int findCancelChoiceIndex() const {
    if (!nsbMode || !nsbVm || !nsbVm->waitingForChoice())
      return -1;
    const auto &choices = nsbVm->activeChoices();
    for (std::size_t i = 0; i < choices.size(); ++i) {
      const std::string &c = choices[i];
      if (c.find("戻る") != std::string::npos ||
          c.find("キャンセル") != std::string::npos ||
          c.find("Cancel") != std::string::npos ||
          c.find("cancel") != std::string::npos ||
          c.find("Back") != std::string::npos ||
          c.find("back") != std::string::npos) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }
};

N2Runtime::N2Runtime(std::shared_ptr<N2ArchiveAssets> assets)
    : Kotonoha::Runtime("N2System", "0.1.0", "me.hirameki.kotonoha.n2system"),
      impl_(std::make_unique<Impl>()), assets_(std::move(assets)) {
  if (!assets_) {
    assets_ = std::make_shared<N2ArchiveAssets>();
  }
  // Pre-detect Steins;Gate / compiled .nsb layout so viewport() reports
  // 1024x576 even before init() creates the render target.
  if (assets_ && (assets_->contains("system/system.ini") ||
                  assets_->contains("nss/0_boot.nsb"))) {
    impl_->viewportSize = SDL_Point{1024, 576};
  }
}

N2Runtime::~N2Runtime() { shutdown(); }

void N2Runtime::shutdown() {
  if (!impl_)
    return;
  if (impl_->ponscripter) {
    impl_->ponscripter->shutdown();
    impl_->ponscripter.reset();
  }
  if (impl_->moviePlayer) {
    impl_->moviePlayer->stop();
    impl_->moviePlayer.reset();
  }
  if (impl_->bgmPlayer) {
    impl_->bgmPlayer->stop();
    impl_->bgmPlayer.reset();
  }
  if (impl_->voicePlayer) {
    impl_->voicePlayer->stop();
    impl_->voicePlayer.reset();
  }
  if (impl_->sePlayer) {
    impl_->sePlayer->stop();
    impl_->sePlayer.reset();
  }
  impl_->nsbVm.reset();
  impl_->nsbMode = false;
  impl_->choiceScene.reset();
  impl_->choiceActive = false;
  impl_->quitRequested = false;
  impl_->focusedChoiceIndex = 0;
  impl_->lastChoices.clear();
  impl_->imageSizeCache.clear();
  impl_->effectTextures.clear();
  impl_->effectRenderer = nullptr;
  impl_->sgUi.reset();
  impl_->backlog.reset();
  impl_->textBox.reset();
  impl_->engine = nullptr;
}

SDL_Point N2Runtime::viewport() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->viewport();
  return impl_->viewportSize;
}

bool N2Runtime::isNsbActive() const {
  return impl_->nsbMode && impl_->nsbVm != nullptr;
}

NsbVm *N2Runtime::nsbVm() { return impl_->nsbVm.get(); }

const NsbVm *N2Runtime::nsbVm() const { return impl_->nsbVm.get(); }

bool N2Runtime::isPonscripterActive() const {
  return impl_->ponscripter != nullptr;
}

Ponscripter::PonscripterVm *N2Runtime::ponscripterVm() {
  return impl_->ponscripter ? &impl_->ponscripter->vm() : nullptr;
}

const Ponscripter::PonscripterVm *N2Runtime::ponscripterVm() const {
  return impl_->ponscripter ? &impl_->ponscripter->vm() : nullptr;
}

void N2Runtime::init(Kotonoha::Engine &engine) {
  impl_->engine = &engine;

  // Check if the mounted directory is a PONscripter title (e.g. 0.utf, 00.utf,
  // 01.utf, arc.nsa) such as SayaNoUta_PT-BR.
  if (assets_ && !assets_->rootDirectory().empty()) {
    auto ponAssets = std::make_shared<Ponscripter::PonscripterAssets>(
        assets_->rootDirectory().string(), assets_->overlayDirectory());
    if (!ponAssets->discoverScripts().empty()) {
      impl_->ponscripter =
          std::make_unique<Ponscripter::PonscripterRuntime>(ponAssets);
      engine.SetAssetProvider(ponAssets);
      impl_->ponscripter->init(engine);
      SetState(Kotonoha::GameState::Gameplay);
      return;
    }
  }

  impl_->textBox = std::make_shared<Kotonoha::VNTextBox>(engine, "", "", 30.0);
  impl_->backlog = std::make_shared<Kotonoha::VNBacklog>(engine);
  impl_->pauseMenu = std::make_shared<Kotonoha::InGameMenu>(engine);
  impl_->pauseMenu->SetOnResume([this] {
    if (impl_->pauseMenu)
      impl_->pauseMenu->close();
  });
  impl_->pauseMenu->SetOnSave([this] {
    saveGame((Kotonoha::saveDirectory() / "n2system_quick.n2sv").string());
    if (impl_->pauseMenu)
      impl_->pauseMenu->close();
  });
  impl_->pauseMenu->SetOnLoad([this] {
    loadGame((Kotonoha::saveDirectory() / "n2system_quick.n2sv").string());
    if (impl_->pauseMenu)
      impl_->pauseMenu->close();
  });
  impl_->pauseMenu->SetOnSettings([this] {
    if (impl_->pauseMenu)
      impl_->pauseMenu->close();
    if (impl_->engine)
      impl_->engine->openSettings();
  });
  impl_->pauseMenu->SetOnReturnToMenu([this] {
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit);
  });
  impl_->choiceScene = std::make_unique<Kotonoha::ChoiceQuestionScene>(
      engine, "", std::vector<Kotonoha::ChoiceOption>{{"", 0}}, nullptr);
  impl_->choiceActive = false;
  impl_->dialogueHidden = false;
  impl_->activeScrollBarDrag.clear();
  impl_->quitRequested = false;
  impl_->focusedChoiceIndex = 0;
  impl_->lastChoices.clear();
  impl_->moviePlayer =
      std::make_unique<Kotonoha::MediaPlayer>(engine.GetAudio());
  impl_->bgmPlayer = std::make_unique<Kotonoha::MediaPlayer>(engine.GetAudio());
  impl_->voicePlayer =
      std::make_unique<Kotonoha::MediaPlayer>(engine.GetAudio());
  impl_->sePlayer = std::make_unique<Kotonoha::MediaPlayer>(engine.GetAudio());

  impl_->sgUi.reset();
  if (assets_) {
    for (const char *name : {"Droid Sans Fallback.ttf",
                             "font/Droid Sans Fallback.ttf",
                             "system/Droid Sans Fallback.ttf"}) {
      if (!assets_->contains(name))
        continue;
      try {
        auto fontBytes = assets_->read(name);
        if (!fontBytes.empty())
          impl_->sgUi = std::make_unique<Kotonoha::UI>(std::move(fontBytes));
      } catch (const std::exception &e) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "N2System: font %s cannot load: %s", name, e.what());
      }
      if (impl_->sgUi)
        break;
    }
  }

  AddScene(impl_->backlog.get(), Kotonoha::GameState::Gameplay);
  AddScene(impl_->pauseMenu.get(), Kotonoha::GameState::Gameplay);

  // Check for Steins;Gate / compiled .nsb bytecode layout first.
  impl_->nsbMode = false;
  impl_->nsbVm.reset();
  if (assets_ && (assets_->contains("system/system.ini") ||
                  assets_->contains("nss/0_boot.nsb"))) {
    SystemIniConfig cfg;
    if (assets_->contains("system/system.ini")) {
      try {
        cfg = SystemIniConfig::parseUtf16Le(assets_->read("system/system.ini"));
      } catch (...) {
      }
    }
    impl_->viewportSize = SDL_Point{cfg.width, cfg.height};
    impl_->nsbVm = std::make_unique<NsbVm>();
    impl_->nsbVm->setConfig(cfg);
    impl_->nsbVm->setImageSizeQuery([this](std::string_view path, int &w,
                                           int &h) -> bool {
      if (!impl_->nsbVm)
        return false;
      return impl_->queryImageSize(assets_.get(), impl_->nsbVm->sceneGraph(),
                                   std::string(path), w, h);
    });
    impl_->nsbVm->setSaveExistsQuery([](std::uint32_t slot) {
      const auto name = slot == 0
          ? std::string("n2system_quick.n2sv")
          : "n2system_slot_" + std::to_string(slot) + ".n2sv";
      std::error_code error;
      return std::filesystem::is_regular_file(Kotonoha::saveDirectory() / name,
                                              error) && !error;
    });
    impl_->nsbVm->setMediaTimeQuery(
        [this](std::string_view query, MediaTimeKind kind) -> std::int64_t {
          const Kotonoha::MediaPlayer *player = nullptr;
          if (impl_->moviePlayer && impl_->moviePlayer->isOpen() &&
              (query.empty() || query == impl_->currentMovie ||
               (impl_->nsbVm &&
                query == impl_->nsbVm->activeMovie().entityName)))
            player = impl_->moviePlayer.get();
          else if (impl_->bgmPlayer && impl_->bgmPlayer->isOpen() &&
                   (query == impl_->currentBgm || query == "bgm"))
            player = impl_->bgmPlayer.get();
          else if (impl_->voicePlayer && impl_->voicePlayer->isOpen() &&
                   (query == impl_->currentVoice || query == "voice"))
            player = impl_->voicePlayer.get();
          if (!player)
            return 0;
          const double duration = player->duration();
          const double elapsed = player->elapsed();
          double seconds = 0.0;
          if (kind == MediaTimeKind::Elapsed)
            seconds = elapsed;
          else if (duration >= 0.0)
            seconds = kind == MediaTimeKind::Duration
                          ? duration : std::max(0.0, duration - elapsed);
          if (!std::isfinite(seconds) || seconds < 0)
            return 0;
          return static_cast<std::int64_t>(
              std::min(seconds * 1000.0,
                       double(std::numeric_limits<std::int64_t>::max())));
        });
    impl_->nsbVm->setLoader(
        [assets = assets_](std::string_view nsbPath,
                           std::vector<std::uint8_t> &nsbOut,
                           std::vector<std::uint8_t> &mapOut) -> bool {
          const std::string nsb = NsbScript::nssToNsbPath(nsbPath);
          const std::string map = NsbScript::nssToMapPath(nsbPath);
          if (!assets || !assets->contains(nsb))
            return false;
          try {
            nsbOut = assets->read(nsb);
            if (assets->contains(map))
              mapOut = assets->read(map);
            else
              mapOut.clear();
            return true;
          } catch (...) {
            return false;
          }
        });
    if (impl_->nsbVm->boot(cfg.startScript, "chapter.main")) {
      impl_->nsbMode = true;
      SetState(Kotonoha::GameState::Gameplay);
      return;
    }
    impl_->nsbVm.reset();
  }

  impl_->viewportSize = SDL_Point{1280, 720};

  std::string scriptSource;
  std::string initialChapter = "main";
  impl_->nativeScriptId.clear();
  impl_->nativeScript.clear();

  for (const char *path : {"scenario.nss", "n2system.nss", "start.nss",
                           "nss/0_boot.nss", "nss/main.nss"}) {
    if (assets_ && assets_->contains(path)) {
      scriptSource = assets_->loadTextUtf8(path, "CP932");
      impl_->nativeScriptId = path;
      break;
    }
  }

  if (!scriptSource.empty()) {
    try {
      auto index = ScriptIndex::Parse(scriptSource);
      if (index.findChapter("main"))
        initialChapter = "main";
      else if (!index.chapters().empty())
        initialChapter = index.chapters().front().name;
      impl_->nativeIndex = std::move(index);
    } catch (...) {
      scriptSource.clear();
      impl_->nativeScriptId.clear();
    }
  }

  if (scriptSource.empty()) {
    scriptSource =
        "chapter main {\n"
        "SetVariable(\"$progress\", \"1\");\n"
        "CreateSprite(\"bg\", \"bg_lab.png\");\n"
        "PlayBgm(\"bgm_gate.ogg\", 1);\n"
        "SetDialogue(\"System\", \"N2System Engine Initialized.\");\n"
        "WaitKey();\n"
        "SetDialogue(\"Okabe\", \"El Psy Kongroo.\");\n"
        "WaitKey();\n"
        "Finish();\n"
        "}\n";
    initialChapter = "main";
  }

  impl_->nativeScript = scriptSource;
  impl_->nativeIndex = ScriptIndex::Parse(impl_->nativeScript);
  vm_.loadScript(scriptSource, initialChapter);
  SetState(Kotonoha::GameState::Gameplay);
}

void N2Runtime::initGlobal(Kotonoha::Engine &engine) { (void)engine; }

void N2Runtime::initSplash(Kotonoha::Engine &engine) { (void)engine; }

void N2Runtime::initMenu(Kotonoha::Engine &engine) { (void)engine; }

void N2Runtime::initGameplay(Kotonoha::Engine &engine) {
  impl_->engine = &engine;
}

void N2Runtime::update() {
  if (impl_->ponscripter) {
    impl_->ponscripter->update();
    return;
  }

  Runtime::update();
  if (impl_->restartRequested && impl_->engine) {
    impl_->restartRequested = false;
    auto *engine = impl_->engine;
    SetState(Kotonoha::GameState::Gameplay);
    // init() replaces every registered scene; drop stale pointers first.
    ClearScenes();
    init(*engine);
    return;
  }
  if (impl_->engine && impl_->engine->isStoryPaused())
    return;

  constexpr float dt = 1.0f / 60.0f;

  if (isNsbActive()) {
    auto &nvm = *impl_->nsbVm;
    nvm.update(dt);

    // Sync choices from Select/Case: prefer native on-screen SceneGraph
    // choices (e.g. Steins;Gate sys_title.nsb buttons) and only fall back to
    // ChoiceQuestionScene when no visual choice hitboxes exist.
    if (nvm.waitingForChoice()) {
      const auto &choices = nvm.activeChoices();
      if (choices != impl_->lastChoices) {
        impl_->lastChoices = choices;
        impl_->focusedChoiceIndex = 0;
        impl_->choiceActive = false;
      }
      const bool nativeChoices = impl_->hasNativeChoices(assets_.get());
      if (nativeChoices) {
        impl_->choiceActive = false;
        if (impl_->focusedChoiceIndex < 0 ||
            impl_->focusedChoiceIndex >= static_cast<int>(choices.size())) {
          impl_->focusedChoiceIndex = 0;
        }
      } else if (!impl_->choiceActive && impl_->choiceScene) {
        std::vector<Kotonoha::ChoiceOption> items;
        for (std::size_t i = 0; i < choices.size(); ++i) {
          items.push_back({choices[i], static_cast<int>(i)});
        }
        impl_->choiceActive = true;
        impl_->choiceScene->setQuestion(
            "", items, [this](int optionId, const std::string &label) {
              (void)label;
              impl_->choiceActive = false;
              if (impl_->nsbVm)
                impl_->nsbVm->triggerChoiceIndex(optionId);
            });
      }
    } else {
      impl_->lastChoices.clear();
      impl_->focusedChoiceIndex = 0;
    }

    if (!nvm.finished() && !impl_->choiceActive) {
      nvm.step(4096);
    }
    // Native persistence opcodes are deferred until AFTER stepThread
    // releases references to the current call stack and process. Loading a
    // checkpoint from within an instruction would invalidate them.
    if (const auto pending = nvm.takePendingSaveAction()) {
      const auto filename = pending->slot == 0
          ? std::string("n2system_quick.n2sv")
          : "n2system_slot_" + std::to_string(pending->slot) + ".n2sv";
      const auto path = Kotonoha::saveDirectory() / filename;
      switch (pending->kind) {
      case SaveActionKind::Save:
        saveGame(path.string());
        break;
      case SaveActionKind::Delete: {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (ec)
          SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                      "N2System: cannot delete save slot %u: %s",
                      pending->slot, ec.message().c_str());
        break;
      }
      case SaveActionKind::Load:
        loadGame(path.string());
        impl_->lastChoices.clear();
        impl_->focusedChoiceIndex = 0;
        impl_->choiceActive = false;
        // Resynchronize players with restored audio and movie channels.
        if (impl_->moviePlayer) impl_->moviePlayer->stop();
        if (impl_->bgmPlayer) impl_->bgmPlayer->stop();
        if (impl_->voicePlayer) impl_->voicePlayer->stop();
        if (impl_->sePlayer) impl_->sePlayer->stop();
        impl_->currentMovie.clear();
        impl_->currentBgm.clear();
        impl_->currentVoice.clear();
        impl_->currentSe.clear();
        return; // Rebuild UI and media from restored state next frame.
      }
    }

    if (nvm.finished() && impl_->engine && !impl_->quitRequested) {
      impl_->quitRequested = true;
      if (impl_->moviePlayer)
        impl_->moviePlayer->stop();
      if (impl_->bgmPlayer)
        impl_->bgmPlayer->stop();
      if (impl_->voicePlayer)
        impl_->voicePlayer->stop();
      if (impl_->sePlayer)
        impl_->sePlayer->stop();
      SDL_Event quit{};
      quit.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&quit);
      return;
    }

    // Sync BGM / Voice / SE via streaming MediaPlayer
    const auto &audio = nvm.audioSystem();
    if (impl_->bgmPlayer && assets_) {
      if (audio.bgmChannel().isPlaying &&
          !audio.bgmChannel().assetPath.empty()) {
        if (impl_->currentBgm != audio.bgmChannel().assetPath &&
            assets_->contains(audio.bgmChannel().assetPath)) {
          impl_->currentBgm = audio.bgmChannel().assetPath;
          try {
            impl_->bgmPlayer->open(*assets_, impl_->currentBgm, true, "bgm");
          } catch (...) {
          }
        }
      } else if (!impl_->currentBgm.empty()) {
        impl_->bgmPlayer->stop();
        impl_->currentBgm.clear();
      }
    }
    if (impl_->voicePlayer && assets_) {
      if (audio.voiceChannel().isPlaying &&
          !audio.voiceChannel().assetPath.empty()) {
        if (impl_->currentVoice != audio.voiceChannel().assetPath &&
            assets_->contains(audio.voiceChannel().assetPath)) {
          impl_->currentVoice = audio.voiceChannel().assetPath;
          try {
            impl_->voicePlayer->open(*assets_, impl_->currentVoice, true,
                                     "voice");
          } catch (...) {
          }
        }
      }
    }
    if (impl_->sePlayer && assets_) {
      std::string latestSe;
      for (const auto &[seId, ch] : audio.seChannels()) {
        (void)seId;
        if (ch.isPlaying && !ch.assetPath.empty())
          latestSe = ch.assetPath;
      }
      if (!latestSe.empty() && latestSe != impl_->currentSe &&
          assets_->contains(latestSe)) {
        impl_->currentSe = latestSe;
        try {
          impl_->sePlayer->open(*assets_, impl_->currentSe, true, "se");
        } catch (...) {
        }
      } else if (latestSe.empty() && !impl_->currentSe.empty()) {
        impl_->sePlayer->stop();
        impl_->currentSe.clear();
      }
    }

    // Sync active .ngs movie streaming
    const auto &mov = nvm.activeMovie();
    if (impl_->moviePlayer && assets_) {
      if (mov.active && !mov.path.empty()) {
        if (impl_->currentMovie != mov.path) {
          impl_->currentMovie = mov.path;
          if (assets_->contains(mov.path)) {
            try {
              impl_->moviePlayer->open(*assets_, mov.path, false, "movie");
              impl_->videoSkip.reset();
            } catch (...) {
              nvm.notifyMovieFinished(mov.entityName);
            }
          } else {
            nvm.notifyMovieFinished(mov.entityName);
          }
        } else if (impl_->moviePlayer->isOpen() &&
                   impl_->moviePlayer->finished()) {
          if (mov.loop) {
            try {
              impl_->moviePlayer->open(*assets_, mov.path, false, "movie");
              impl_->videoSkip.reset();
            } catch (...) {
              nvm.notifyMovieFinished(mov.entityName);
            }
          } else {
            impl_->moviePlayer->stop();
            impl_->currentMovie.clear();
            nvm.notifyMovieFinished(mov.entityName);
          }
        }
      } else if (!impl_->currentMovie.empty()) {
        impl_->moviePlayer->stop();
        impl_->currentMovie.clear();
      }
    }

    // Sync dialogue & backlog (NSB mode renders Steins;Gate's native dialogue
    // HUD directly rather than Kotonoha's generic VNTextBox theme).
    const auto &dialogue = nvm.dialogueSystem();
    const auto rawSpeaker = std::string(dialogue.speaker());
    const auto rawText = std::string(dialogue.fullText());
    if (!rawText.empty() && rawText != impl_->lastBacklogText &&
        impl_->backlog) {
      impl_->lastBacklogText = rawText;
      impl_->backlog->addEntry(rawSpeaker, rawText);
    }
    if (impl_->choiceActive && impl_->choiceScene) {
      impl_->choiceScene->update();
    }
    return;
  }

  vm_.update(dt);

  if (vm_.hasPendingChoice() && !impl_->choiceActive && impl_->choiceScene) {
    auto options = vm_.pendingChoiceOptions();
    std::vector<Kotonoha::ChoiceOption> choiceItems;
    for (size_t i = 0; i < options.size(); ++i) {
      choiceItems.push_back({impl_->engine
                                 ? impl_->engine->GetTranslation().text(
                                       translationRoute(), options[i])
                                 : options[i],
                             static_cast<int>(i)});
    }
    impl_->choiceActive = true;
    impl_->choiceScene->setQuestion(
        "", choiceItems, [this](int optionId, const std::string &label) {
          (void)label;
          impl_->choiceActive = false;
          vm_.selectChoice(optionId);
          vm_.variables().set("$route", static_cast<int64_t>(optionId));
        });
  }

  if (!vm_.finished() && !impl_->choiceActive) {
    vm_.step();
  }

  if (impl_->textBox) {
    const auto &dialogue = vm_.dialogueSystem();
    const auto rawSpeaker = std::string(dialogue.speaker());
    const auto rawText = std::string(dialogue.fullText());
    const auto route = translationRoute();
    const auto speaker =
        impl_->engine ? impl_->engine->GetTranslation().text(route, rawSpeaker)
                      : rawSpeaker;
    const auto line = impl_->engine
                          ? impl_->engine->GetTranslation().text(route, rawText)
                          : rawText;
    // Preserve the native VM's reveal cadence for untranslated text.
    // Translations have a different glyph count and are displayed as a
    // complete line instead of slicing a multibyte UTF-8 character.
    impl_->textBox->setDialogue(
        speaker, line == rawText ? dialogue.visibleText() : line);
    impl_->textBox->update();
  }

  if (impl_->choiceActive && impl_->choiceScene) {
    impl_->choiceScene->update();
  }
}

void N2Runtime::renderGameplay(Kotonoha::RendererRef renderer, float scale) {
  if (impl_->ponscripter) {
    impl_->ponscripter->renderGameplay(renderer, scale);
    return;
  }

  if (impl_->bgmPlayer && impl_->bgmPlayer->isOpen())
    impl_->bgmPlayer->update(renderer);
  if (impl_->voicePlayer && impl_->voicePlayer->isOpen())
    impl_->voicePlayer->update(renderer);
  if (impl_->sePlayer && impl_->sePlayer->isOpen())
    impl_->sePlayer->update(renderer);
  if (impl_->moviePlayer && impl_->moviePlayer->isOpen())
    impl_->moviePlayer->update(renderer);

  SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 0);
  SDL_RenderClear(&renderer);

  const SceneGraph &graph =
      isNsbActive() ? impl_->nsbVm->sceneGraph() : vm_.sceneGraph();
  const float vpW = static_cast<float>(impl_->viewportSize.x);
  const float vpH = static_cast<float>(impl_->viewportSize.y);

  std::string focusedChoice;
  bool focusedHasOver = false;
  if (impl_->hasNativeChoices(assets_.get())) {
    const auto &choices = impl_->nsbVm->activeChoices();
    if (impl_->focusedChoiceIndex >= 0 &&
        impl_->focusedChoiceIndex < static_cast<int>(choices.size())) {
      focusedChoice =
          choices[static_cast<std::size_t>(impl_->focusedChoiceIndex)];
      focusedHasOver = impl_->hasOverSprite(graph, focusedChoice);
    }
  }
  const std::string focusedOverPrefix =
      focusedChoice.empty() ? std::string{} : focusedChoice + "/MouseOver/";
  const std::string focusedUsualPrefix =
      focusedChoice.empty() ? std::string{} : focusedChoice + "/MouseUsual/";

  for (const auto &name : graph.renderOrder()) {
    const auto *entity = graph.findEntity(name);
    if (!entity)
      continue;

    const bool isFocusedOver =
        !focusedOverPrefix.empty() &&
        Impl::startsWith(entity->name, focusedOverPrefix);
    const bool isFocusedUsual =
        focusedHasOver && !focusedUsualPrefix.empty() &&
        Impl::startsWith(entity->name, focusedUsualPrefix);
    if (isFocusedUsual)
      continue;
    // Hide all hover/click/leave choice overlays unless they belong to the
    // currently focused choice so config/menu buttons never stack states.
    if (!isFocusedOver && Impl::isChoiceOverlayState(entity->name))
      continue;

    const float effectiveOpacity = isFocusedOver ? 1.0f : entity->opacity;
    const bool effectiveVisible = isFocusedOver ? true : entity->visible;
    if (!effectiveVisible || effectiveOpacity <= 0.01f)
      continue;

    const Uint8 alpha =
        static_cast<Uint8>(std::clamp(effectiveOpacity, 0.0f, 1.0f) * 255.0f);
    float drawX = entity->x + entity->shakeOffsetX;
    float drawY = entity->y + entity->shakeOffsetY;
    auto applyAlign = [&](float w, float h) {
      if (entity->alignCenterX)
        drawX -= w * 0.5f;
      else if (entity->alignRightX)
        drawX -= w;
      if (entity->alignCenterY)
        drawY -= h * 0.5f;
      else if (entity->alignBottomY)
        drawY -= h;
    };

    if (entity->type == EntityType::Color) {
      const float w = entity->width > 0.0f ? entity->width : vpW;
      const float h = entity->height > 0.0f ? entity->height : vpH;
      applyAlign(w, h);
      const Uint8 r = static_cast<Uint8>((entity->solidColor >> 16) & 0xFFu);
      const Uint8 g = static_cast<Uint8>((entity->solidColor >> 8) & 0xFFu);
      const Uint8 b = static_cast<Uint8>(entity->solidColor & 0xFFu);
      SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(&renderer, r, g, b, alpha);
      SDL_FRect rect{drawX, drawY, w, h};
      SDL_RenderFillRect(&renderer, &rect);
    } else if (entity->type == EntityType::Movie) {
      if (impl_->moviePlayer && impl_->moviePlayer->isOpen() &&
          impl_->moviePlayer->hasVideoFrame()) {
        SDL_FRect area{
            drawX, drawY,
            (entity->hasClipRect && entity->clipW > 0.0f) ? entity->clipW : vpW,
            (entity->hasClipRect && entity->clipH > 0.0f) ? entity->clipH
                                                          : vpH};
        impl_->moviePlayer->render(renderer, area);
      }
    } else if (entity->type == EntityType::ScrollBar) {
      const float w = entity->width > 0.0f ? entity->width : 180.0f;
      const float h = entity->height > 0.0f ? entity->height : 18.0f;
      const float frac = std::clamp(entity->scrollValue / 100.0f, 0.0f, 1.0f);
      SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
      // Track
      SDL_SetRenderDrawColor(&renderer, 28, 34, 44, alpha);
      SDL_FRect track{drawX, drawY, w, h};
      SDL_RenderFillRect(&renderer, &track);
      // Filled bar (Future Gadget Lab amber/gold accent)
      SDL_SetRenderDrawColor(&renderer, 212, 168, 75, alpha);
      SDL_FRect fill =
          entity->scrollHorizontal
              ? SDL_FRect{drawX, drawY, w * frac, h}
              : SDL_FRect{drawX, drawY + h * (1.0f - frac), w, h * frac};
      SDL_RenderFillRect(&renderer, &fill);
      // Border & knob
      SDL_SetRenderDrawColor(&renderer, 180, 196, 214, alpha);
      SDL_RenderRect(&renderer, &track);
      SDL_FRect knob =
          entity->scrollHorizontal
              ? SDL_FRect{drawX + w * frac - 5.0f, drawY - 2.0f, 10.0f,
                          h + 4.0f}
              : SDL_FRect{drawX - 2.0f, drawY + h * (1.0f - frac) - 5.0f,
                          w + 4.0f, 10.0f};
      SDL_SetRenderDrawColor(&renderer, 244, 240, 224, alpha);
      SDL_RenderFillRect(&renderer, &knob);
    } else if (entity->type == EntityType::Text) {
      if (!entity->text.empty()) {
        const float wrapW =
            entity->width > 0.0f ? entity->width : (vpW - drawX);
        const float fontSize =
            entity->fontSize > 0 ? static_cast<float>(entity->fontSize) : 20.0f;
        const SDL_Color col{
            static_cast<Uint8>(std::clamp(entity->colorR, 0.0f, 1.0f) * 255.0f),
            static_cast<Uint8>(std::clamp(entity->colorG, 0.0f, 1.0f) * 255.0f),
            static_cast<Uint8>(std::clamp(entity->colorB, 0.0f, 1.0f) * 255.0f),
            alpha};
        impl_->drawSgText(renderer, entity->text, drawX, drawY, wrapW, fontSize,
                          col);
      }
    } else if (entity->type == EntityType::Sprite) {
      SDL_Texture *tex = nullptr;
      const std::string resolvedImg =
          impl_->resolveImagePath(graph, entity->imagePath);
      if (assets_ && !resolvedImg.empty() && assets_->contains(resolvedImg)) {
        try {
          tex = impl_->effectTexture(renderer, *assets_, resolvedImg,
                                       entity->monochrome,
                                       entity->boxBlurPasses);
        } catch (...) {
          tex = nullptr;
        }
      }
      if (tex) {
        float tw = vpW, th = vpH;
        SDL_GetTextureSize(tex, &tw, &th);
        Uint8 oldR = 255, oldG = 255, oldB = 255, oldAlpha = 255;
        SDL_GetTextureColorMod(tex, &oldR, &oldG, &oldB);
        SDL_GetTextureAlphaMod(tex, &oldAlpha);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(tex, alpha);
        const Uint8 cr =
            static_cast<Uint8>(std::clamp(entity->colorR, 0.0f, 1.0f) * 255.0f);
        const Uint8 cg =
            static_cast<Uint8>(std::clamp(entity->colorG, 0.0f, 1.0f) * 255.0f);
        const Uint8 cb =
            static_cast<Uint8>(std::clamp(entity->colorB, 0.0f, 1.0f) * 255.0f);
        SDL_SetTextureColorMod(tex, cr, cg, cb);
        const SDL_FRect *srcPtr = nullptr;
        SDL_FRect src{};
        if (entity->hasClipRect && entity->clipW > 0.0f &&
            entity->clipH > 0.0f) {
          src = SDL_FRect{entity->clipX, entity->clipY, entity->clipW,
                          entity->clipH};
          srcPtr = &src;
          tw = entity->clipW;
          th = entity->clipH;
        }
        const float dstW = tw * entity->scaleX;
        const float dstH = th * entity->scaleY;
        applyAlign(dstW, dstH);
        SDL_FRect dst{drawX, drawY, dstW, dstH};
        if (std::abs(entity->angle) > 0.01f) {
          Kotonoha::RenderUpscaledTextureRotated(&renderer, tex, srcPtr, &dst,
                                                 entity->angle, nullptr,
                                                 SDL_FLIP_NONE);
        } else {
          Kotonoha::RenderUpscaledTexture(&renderer, tex, srcPtr, &dst);
        }
        SDL_SetTextureColorMod(tex, oldR, oldG, oldB);
        SDL_SetTextureAlphaMod(tex, oldAlpha);
      } else if (!isNsbActive()) {
        SDL_FRect rect{drawX, drawY, vpW, vpH};
        SDL_SetRenderDrawColor(&renderer, 30, 36, 48, alpha);
        SDL_RenderFillRect(&renderer, &rect);
      }
    }
  }

  // Render fullscreen .ngs movie if active
  if (isNsbActive() && impl_->nsbVm->activeMovie().active &&
      impl_->nsbVm->activeMovie().fullscreen && impl_->moviePlayer &&
      impl_->moviePlayer->isOpen() && impl_->moviePlayer->hasVideoFrame()) {
    SDL_FRect full{0.0f, 0.0f, vpW, vpH};
    impl_->moviePlayer->render(renderer, full);
  }

  // Render attached phone CG if active
  if (isNsbActive() && !impl_->nsbVm->phoneState().attachedCg.empty() &&
      assets_ && assets_->contains(impl_->nsbVm->phoneState().attachedCg)) {
    try {
      auto &cg =
          assets_->texture(renderer, impl_->nsbVm->phoneState().attachedCg);
      float cw = 0, ch = 0;
      SDL_GetTextureSize(&cg, &cw, &ch);
      SDL_FRect dst{(vpW - cw) * 0.5f, (vpH - ch) * 0.5f, cw, ch};
      Kotonoha::RenderUpscaledTexture(&renderer, &cg, nullptr, &dst);
    } catch (...) {
    }
  }

  const bool hasDialogue =
      isNsbActive() ? !impl_->nsbVm->dialogueSystem().fullText().empty() : true;
  if (isNsbActive()) {
    if (hasDialogue && !impl_->choiceActive && !impl_->dialogueHidden) {
      const auto &dialogue = impl_->nsbVm->dialogueSystem();
      const std::string speaker(dialogue.speaker());
      const std::string visibleText(dialogue.visibleText());
      // Authentic Steins;Gate dark-translucent dialogue bar in native 1024x576
      // viewport (replacing Kotonoha's generic VNTextBox theme).
      const float marginX = vpW * 0.045f;
      const float boxW = vpW - marginX * 2.0f;
      const float boxH = vpH * 0.23f;
      const float boxY = vpH - boxH - vpH * 0.025f;
      SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
      SDL_FRect boxRect{marginX, boxY, boxW, boxH};
      SDL_SetRenderDrawColor(&renderer, 10, 14, 22, 204);
      SDL_RenderFillRect(&renderer, &boxRect);
      // Subtle Steins;Gate warm-amber accent line along top of dialogue bar
      SDL_FRect topLine{marginX, boxY, boxW, 2.0f};
      SDL_SetRenderDrawColor(&renderer, 212, 168, 75, 220);
      SDL_RenderFillRect(&renderer, &topLine);
      SDL_SetRenderDrawColor(&renderer, 80, 96, 118, 160);
      SDL_RenderRect(&renderer, &boxRect);

      const float textX = marginX + 40.0f;
      const float textWrap = boxW - 80.0f;
      if (!speaker.empty()) {
        impl_->drawSgText(renderer, speaker, textX, boxY + 10.0f, textWrap,
                          20.0f, {242, 210, 139, 255});
      }
      const float bodyY = boxY + (speaker.empty() ? 18.0f : 40.0f);
      impl_->drawSgText(renderer, visibleText, textX, bodyY, textWrap, 21.0f,
                        {244, 246, 250, 255});
      // Subtle animated wait indicator when line reveal completes
      if (visibleText.size() >= dialogue.fullText().size()) {
        const float pulse =
            0.5f + 0.5f * std::sin(static_cast<float>(SDL_GetTicks()) * 0.006f);
        SDL_FRect indicator{marginX + boxW - 28.0f, boxY + boxH - 22.0f, 12.0f,
                            8.0f};
        SDL_SetRenderDrawColor(
            &renderer, 212, 168, 75,
            static_cast<Uint8>(std::lround((0.35f + 0.65f * pulse) * 255.0f)));
        SDL_RenderFillRect(&renderer, &indicator);
      }
    }
  } else if (impl_->textBox && !impl_->choiceActive && hasDialogue) {
    renderScene(*impl_->textBox, renderer, scale);
  }

  if (impl_->choiceActive && impl_->choiceScene) {
    renderScene(*impl_->choiceScene, renderer, scale);
  }
}

bool N2Runtime::onEvent(Kotonoha::EventRef rawEvent) {
  if (impl_->ponscripter)
    return impl_->ponscripter->onEvent(rawEvent);

  for (auto *scene : GetActiveScenes()) {
    if (scene && scene->capturesInput())
      return Runtime::onEvent(rawEvent);
  }
  if (impl_->backlog && impl_->backlog->isVisible())
    return Runtime::onEvent(rawEvent);

  // Decorative looping title movies must not capture menu selections or
  // accept generic video-skip gestures.
  if (isNsbActive() && impl_->nsbVm->activeMovie().active &&
      !impl_->nsbVm->activeMovie().loop) {
    // Guard the movie wait even before the decoder opens. Otherwise an early
    // single click can advance the NSB VM while the first frame is loading.
    if (impl_->videoSkip.accept(rawEvent)) {
      const auto entity = impl_->nsbVm->activeMovie().entityName;
      if (impl_->moviePlayer)
        impl_->moviePlayer->stop();
      impl_->currentMovie.clear();
      impl_->nsbVm->notifyMovieFinished(entity);
    }
    return true;
  }

  if (impl_->choiceActive && impl_->choiceScene) {
    // ChoiceQuestionScene draws in design space; map pointer coordinates the
    // same way renderScene() maps its pixels.
    if (dispatchEvent(*impl_->choiceScene, rawEvent))
      return true;
  }

  const bool nativeChoices = impl_->hasNativeChoices(assets_.get());
  const SDL_Event event = Kotonoha::InputMapper::toKeyEvent(
      rawEvent, nativeChoices ? Kotonoha::InputContext::Menu
                              : Kotonoha::InputContext::Dialogue);

  if (isNsbActive()) {
    auto updateScrollBarAt = [&](const std::string &sbName, float mx,
                                 float my) -> bool {
      auto *e = impl_->nsbVm->sceneGraph().findEntity(sbName);
      if (!e || e->type != EntityType::ScrollBar || !e->visible)
        return false;
      const float w = e->width > 0.0f ? e->width : 180.0f;
      const float h = e->height > 0.0f ? e->height : 18.0f;
      const float frac =
          e->scrollHorizontal
              ? std::clamp((mx - e->x) / std::max(1.0f, w), 0.0f, 1.0f)
              : std::clamp(1.0f - (my - e->y) / std::max(1.0f, h), 0.0f, 1.0f);
      e->scrollValue = frac * 100.0f;
      return true;
    };
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
        event.button.button == SDL_BUTTON_LEFT &&
        !impl_->activeScrollBarDrag.empty()) {
      impl_->activeScrollBarDrag.clear();
      return true;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION &&
        !impl_->activeScrollBarDrag.empty()) {
      updateScrollBarAt(impl_->activeScrollBarDrag, event.motion.x,
                        event.motion.y);
      return true;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event.button.button == SDL_BUTTON_LEFT) {
      const float mx = event.button.x;
      const float my = event.button.y;
      for (const auto &name : impl_->nsbVm->sceneGraph().renderOrder()) {
        const auto *e = impl_->nsbVm->sceneGraph().findEntity(name);
        if (!e || e->type != EntityType::ScrollBar || !e->visible ||
            e->opacity <= 0.01f)
          continue;
        const float w = e->width > 0.0f ? e->width : 180.0f;
        const float h = e->height > 0.0f ? e->height : 18.0f;
        if (mx >= e->x - 6.0f && mx <= e->x + w + 6.0f && my >= e->y - 6.0f &&
            my <= e->y + h + 6.0f) {
          impl_->activeScrollBarDrag = e->name;
          updateScrollBarAt(e->name, mx, my);
          return true;
        }
      }
    }
  }

  if (event.type == SDL_EVENT_MOUSE_MOTION && isNsbActive()) {
    impl_->nsbVm->setMousePosition(
        static_cast<int>(std::lround(event.motion.x)),
        static_cast<int>(std::lround(event.motion.y)));
    if (nativeChoices) {
      const int hit = impl_->hitTestNativeChoice(assets_.get(), event.motion.x,
                                                 event.motion.y);
      if (hit >= 0) {
        impl_->focusedChoiceIndex = hit;
        return true;
      }
    }
    return false;
  }

  if (nativeChoices) {
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
      impl_->nsbVm->setMousePosition(
          static_cast<int>(std::lround(event.button.x)),
          static_cast<int>(std::lround(event.button.y)));
      if (event.button.button == SDL_BUTTON_LEFT) {
        const int hit = impl_->hitTestNativeChoice(
            assets_.get(), event.button.x, event.button.y);
        if (hit >= 0) {
          impl_->focusedChoiceIndex = hit;
          impl_->nsbVm->triggerChoiceIndex(hit);
          return true;
        }
        return false;
      }
      if (event.button.button == SDL_BUTTON_RIGHT) {
        const int cancelIdx = impl_->findCancelChoiceIndex();
        if (cancelIdx >= 0) {
          impl_->nsbVm->triggerChoiceIndex(cancelIdx);
          return true;
        }
        return handleBackAction();
      }
      return false;
    }

    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
      if (event.key.key == SDLK_UP)
        return impl_->moveNativeChoiceFocus(0);
      if (event.key.key == SDLK_DOWN)
        return impl_->moveNativeChoiceFocus(1);
      if (event.key.key == SDLK_LEFT)
        return impl_->moveNativeChoiceFocus(2);
      if (event.key.key == SDLK_RIGHT)
        return impl_->moveNativeChoiceFocus(3);
      if (Kotonoha::InputMapper::isConfirmKey(event.key.key)) {
        const int count =
            static_cast<int>(impl_->nsbVm->activeChoices().size());
        if (count > 0) {
          const int idx = std::clamp(impl_->focusedChoiceIndex, 0, count - 1);
          impl_->nsbVm->triggerChoiceIndex(idx);
          return true;
        }
        return false;
      }
      if (Kotonoha::InputMapper::isBackKey(event.key.key)) {
        const int cancelIdx = impl_->findCancelChoiceIndex();
        if (cancelIdx >= 0) {
          impl_->nsbVm->triggerChoiceIndex(cancelIdx);
          return true;
        }
        return handleBackAction();
      }
    }
    return false;
  }

  if (event.type == SDL_EVENT_MOUSE_WHEEL && impl_->backlog) {
    const float dir =
        event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
    if (event.wheel.y * dir > 0.0f) {
      impl_->backlog->open();
      return true;
    }
  }

  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      event.button.button == SDL_BUTTON_RIGHT) {
    if (impl_->pauseMenu) {
      impl_->pauseMenu->open();
      return true;
    }
    return handleBackAction();
  }

  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (event.key.key == SDLK_H && isNsbActive()) {
      impl_->dialogueHidden = !impl_->dialogueHidden;
      return true;
    }
    if (Kotonoha::InputMapper::isBacklogKey(event.key.key) && impl_->backlog) {
      impl_->backlog->toggle();
      return true;
    }
    if (Kotonoha::InputMapper::isBackKey(event.key.key)) {
      if (impl_->pauseMenu) {
        impl_->pauseMenu->open();
        return true;
      }
      return handleBackAction();
    }
    if (event.key.key == SDLK_F5) {
      saveGame((Kotonoha::saveDirectory() / "n2system_quick.n2sv").string());
      return true;
    }
    if (event.key.key == SDLK_F6) {
      loadGame((Kotonoha::saveDirectory() / "n2system_quick.n2sv").string());
      return true;
    }
    if (Kotonoha::InputMapper::isConfirmKey(event.key.key)) {
      return handleAdvanceAction();
    }
  }

  if (event.type == SDL_EVENT_KEY_DOWN &&
      Kotonoha::InputMapper::isSkipHoldKey(event.key.key)) {
    return handleAdvanceAction();
  }

  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      event.button.button == SDL_BUTTON_LEFT) {
    return handleAdvanceAction();
  }
  return Runtime::onEvent(event);
}

bool N2Runtime::handleAdvanceAction() {
  if (impl_->ponscripter)
    return impl_->ponscripter->handleAdvanceAction();
  if (isNsbActive())
    return impl_->nsbVm->advance();
  return vm_.advance();
}

bool N2Runtime::advanceStoryTime(double seconds) {
  if (impl_->ponscripter)
    return impl_->ponscripter->advanceStoryTime(seconds);
  if (isNsbActive()) {
    impl_->nsbVm->update(static_cast<float>(seconds));
    return true;
  }
  vm_.update(static_cast<float>(seconds));
  return true;
}

void N2Runtime::saveGame(const std::string &path) {
  if (impl_->ponscripter) {
    impl_->ponscripter->saveGame(path);
    return;
  }
  try {
    const auto saveData =
        isNsbActive() ? impl_->nsbVm->saveState() : vm_.saveState();
    const auto bytes = SaveStateCodec::serializeBinary(saveData);
    std::string error;
    if (!Kotonoha::Filesystem::writeBinaryFileAtomically(
            std::filesystem::path(path), bytes, &error))
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "N2System: save %s failed: %s",
                  path.c_str(), error.c_str());
  } catch (const std::exception &error) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "N2System: cannot serialize save %s: %s",
                path.c_str(), error.what());
  }
}

void N2Runtime::loadGame(const std::string &path) {
  if (impl_->ponscripter) {
    impl_->ponscripter->loadGame(path);
    return;
  }
  try {
    auto bytes = Kotonoha::Filesystem::readLocalFile(path);
    SaveData loaded;
    if (!SaveStateCodec::deserializeBinary(bytes, loaded)) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "N2System: rejected truncated/corrupted save %s",
                  path.c_str());
      return;
    }
    const bool restored = isNsbActive()
                              ? impl_->nsbVm->loadState(loaded)
                              : vm_.loadState(loaded);
    if (!restored)
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "N2System: save %s references missing or invalid script state",
                  path.c_str());
  } catch (const std::exception &error) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "N2System: cannot load save %s: %s",
                path.c_str(), error.what());
  }
}

std::string N2Runtime::debugNode() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->debugNode();
  if (isNsbActive()) {
    const auto p = impl_->nsbVm->currentScriptPath();
    return p.empty() ? std::string{} : "n2 | nsb " + p;
  }
  const auto chapter = vm_.currentChapter();
  return chapter.empty() ? std::string{}
                         : "n2 | chapter " + std::string(chapter);
}

std::string N2Runtime::dialogueText() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->dialogueText();
  if (isNsbActive())
    return std::string(impl_->nsbVm->dialogueSystem().fullText());
  return std::string(vm_.dialogueSystem().fullText());
}

std::string N2Runtime::dialogueSpeaker() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->dialogueSpeaker();
  if (isNsbActive())
    return std::string(impl_->nsbVm->dialogueSystem().speaker());
  return std::string(vm_.dialogueSystem().speaker());
}

std::vector<Kotonoha::DebugNode> N2Runtime::debugNodes() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->debugNodes();
  if (isNsbActive() && assets_) {
    std::vector<Kotonoha::DebugNode> nodes;
    for (const auto &asset : assets_->listAsset("nss/")) {
      if (asset.size() > 4 && asset.compare(asset.size() - 4, 4, ".nsb") == 0) {
        nodes.push_back({"n2 | nsb " + asset, asset, {asset}});
      }
    }
    return nodes;
  }
  std::vector<Kotonoha::DebugNode> nodes;
  for (const auto &chapter : impl_->nativeIndex.chapters()) {
    Kotonoha::DebugNode node{
        "n2 | chapter " + chapter.name, "Chapter " + chapter.name, {}};
    if (!impl_->nativeScriptId.empty())
      node.assets.push_back(impl_->nativeScriptId);
    if (vm_.currentChapter() == chapter.name) {
      for (const auto &name : vm_.sceneGraph().renderOrder()) {
        const auto *entity = vm_.sceneGraph().findEntity(name);
        if (entity && !entity->imagePath.empty() && assets_ &&
            assets_->contains(entity->imagePath))
          node.assets.push_back(entity->imagePath);
      }
      const auto voice = std::string(vm_.dialogueSystem().voicePath());
      if (!voice.empty() && assets_ && assets_->contains(voice))
        node.assets.push_back(voice);
    }
    nodes.push_back(std::move(node));
    for (const auto &label : chapter.labels) {
      Kotonoha::DebugNode entry{
          "n2 | chapter " + chapter.name + " | label " + label.name,
          "Label " + label.name + " (line " + std::to_string(label.line) + ")",
          {}};
      if (!impl_->nativeScriptId.empty())
        entry.assets.push_back(impl_->nativeScriptId);
      nodes.push_back(std::move(entry));
    }
  }
  return nodes;
}

void N2Runtime::debugJumpToNode(const std::string &id) {
  if (impl_->ponscripter) {
    impl_->ponscripter->debugJumpToNode(id);
    return;
  }
  if (isNsbActive()) {
    constexpr std::string_view nsbPrefix = "n2 | nsb ";
    if (id.compare(0, nsbPrefix.size(), nsbPrefix) == 0) {
      const auto scriptPath = id.substr(nsbPrefix.size());
      if (impl_->nsbVm->jumpToScript(scriptPath, "chapter.main")) {
        impl_->choiceActive = false;
        SetState(Kotonoha::GameState::Gameplay);
        return;
      }
    }
    throw std::invalid_argument("Unknown N2System NSB debug node: " + id);
  }
  constexpr std::string_view prefix = "n2 | chapter ";
  constexpr std::string_view labelSep = " | label ";
  if (id.compare(0, prefix.size(), prefix) != 0)
    throw std::invalid_argument("Unknown N2System debug node: " + id);
  const std::string_view body(id.data() + prefix.size(),
                              id.size() - prefix.size());
  const auto labelAt = body.find(labelSep);
  const auto chapterName = body.substr(0, labelAt);
  const auto *chapter = impl_->nativeIndex.findChapter(chapterName);
  if (!chapter)
    throw std::invalid_argument("Unknown N2System chapter: " + id);
  const auto label = labelAt == std::string_view::npos
                         ? std::string_view{}
                         : body.substr(labelAt + labelSep.size());
  // Reject all unknown targets *before* resetting the VM state.
  if (labelAt != std::string_view::npos &&
      (label.empty() || !chapter->findLabel(label)))
    throw std::invalid_argument("Unknown N2System label: " + id);
  if (vm_.currentChapter() != chapterName || label.empty())
    vm_.loadScript(impl_->nativeScript, chapterName);
  if (!label.empty() && !vm_.debugJumpToLabel(label))
    throw std::logic_error("N2System validated label jump failed");
  impl_->choiceActive = false;
  SetState(Kotonoha::GameState::Gameplay);
}

void N2Runtime::debugRestartScene() {
  if (impl_->ponscripter) {
    impl_->ponscripter->debugRestartScene();
    return;
  }
  debugJumpToNode(debugNode());
}

int N2Runtime::translationRoute() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->translationRoute();
  const auto current = vm_.currentChapter();
  const auto &chapters = impl_->nativeIndex.chapters();
  for (std::size_t i = 0; i < chapters.size(); ++i)
    if (chapters[i].name == current)
      return static_cast<int>(i);
  return -1;
}

std::vector<int> N2Runtime::translationRoutes() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->translationRoutes();
  std::vector<int> result;
  for (std::size_t i = 0; i < impl_->nativeIndex.chapters().size(); ++i)
    result.push_back(static_cast<int>(i));
  return result;
}

Kotonoha::Translation::SourceTexts N2Runtime::translationSources() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->translationSources();
  // The standalone NsScript catalog uses real argument positions and ignores
  // audio filenames, control IDs and comments without executing the VM.
  return ExtractTranslationTexts(impl_->nativeIndex, impl_->nativeScript);
}

std::vector<std::string> N2Runtime::activeTranslationKeys() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->activeTranslationKeys();
  std::vector<std::string> keys;
  const auto append = [&](const std::string &value) {
    if (!value.empty() &&
        std::find(keys.begin(), keys.end(), value) == keys.end())
      keys.push_back(value);
  };
  const auto &dlg =
      isNsbActive() ? impl_->nsbVm->dialogueSystem() : vm_.dialogueSystem();
  append(std::string(dlg.speaker()));
  append(std::string(dlg.fullText()));
  if (!isNsbActive() && vm_.hasPendingChoice())
    for (const auto &value : vm_.pendingChoiceOptions())
      append(value);
  return keys;
}

std::vector<Kotonoha::DebugAudioAsset> N2Runtime::debugSceneAudio() {
  if (impl_->ponscripter)
    return impl_->ponscripter->debugSceneAudio();
  const auto &dialogue =
      isNsbActive() ? impl_->nsbVm->dialogueSystem() : vm_.dialogueSystem();
  if (dialogue.voicePath().empty())
    return {};
  return {{std::string(dialogue.voicePath()), std::string(dialogue.speaker()),
           std::string(dialogue.fullText())}};
}

std::optional<Kotonoha::AudioClip>
N2Runtime::debugOriginalAudio(const std::string &id) {
  if (impl_->ponscripter)
    return impl_->ponscripter->debugOriginalAudio(id);
  return std::nullopt;
}

void N2Runtime::debugOverlayAssetChanged(const std::string &id) {
  if (impl_->ponscripter)
    impl_->ponscripter->debugOverlayAssetChanged(id);
  else if (assets_)
    assets_->clearCache();
}

} // namespace N2System
