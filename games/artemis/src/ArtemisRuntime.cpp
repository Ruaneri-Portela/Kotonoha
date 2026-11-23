// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Artemis/ArtemisRuntime.hpp>

#include <Kotonoha/Filesystem/AtomicFile.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/components/VNSaveMenu.hpp>
#include <Kotonoha/components/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/VideoSkipGesture.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Artemis {
namespace {

Kotonoha::PcmPlayer::Clip toPcmClip(const Kotonoha::AudioClip &decoded,
                                    bool loop, float gain) {
  Kotonoha::PcmPlayer::Clip clip;
  clip.channels = std::max(1, decoded.spec.channels);
  clip.rate = decoded.spec.freq > 0 ? decoded.spec.freq : 44100;
  clip.loop = loop;
  clip.gain = std::clamp(gain, 0.0f, 1.0f);

  if (decoded.data.empty())
    return clip;

  if (decoded.spec.format == SDL_AUDIO_S16) {
    const std::size_t count = decoded.data.size() / sizeof(int16_t);
    clip.samples.resize(count);
    std::memcpy(clip.samples.data(), decoded.data.data(),
                count * sizeof(int16_t));
    return clip;
  }

  SDL_AudioSpec dstSpec = decoded.spec;
  dstSpec.format = SDL_AUDIO_S16;
  Uint8 *converted = nullptr;
  int convertedLen = 0;
  if (SDL_ConvertAudioSamples(&decoded.spec, decoded.data.data(),
                              static_cast<int>(decoded.data.size()), &dstSpec,
                              &converted, &convertedLen) &&
      converted && convertedLen > 0) {
    Kotonoha::SDLMemoryPtr<Uint8> guard(converted);
    const std::size_t count =
        static_cast<std::size_t>(convertedLen) / sizeof(int16_t);
    clip.samples.resize(count);
    std::memcpy(clip.samples.data(), converted, count * sizeof(int16_t));
  }
  return clip;
}

class ArtemisMenu final : public Kotonoha::InGameMenu {
public:
  ArtemisMenu(Kotonoha::Engine &engineRef,
              std::function<void(const std::string &)> onAction)
      : InGameMenu(engineRef), onAction_(std::move(onAction)) {}

protected:
  bool designSpace() const override { return false; }
  int itemCount() const override { return 5; }

  std::string itemLabel(int index) const override {
    switch (index) {
    case 0:
      return "Save";
    case 1:
      return "Load";
    case 2:
      return "Backlog";
    case 3:
      return "Switch Language";
    case 4:
      return "Restart";
    default:
      return {};
    }
  }

  void activate(int index) override {
    static const char *actions[]{"save", "load", "backlog", "lang", "restart"};
    if (index < 0 || index >= 5)
      return;
    close();
    onAction_(actions[index]);
  }

private:
  std::function<void(const std::string &)> onAction_;
};

} // namespace

struct ArtemisRuntime::Impl {
  Kotonoha::Engine *engine = nullptr;
  Kotonoha::VNTextBox *textBox = nullptr;
  Kotonoha::VNBacklog *backlog = nullptr;
  std::unique_ptr<ArtemisMenu> menu;
  std::unique_ptr<Kotonoha::VNSaveMenu> saveMenu;

  Kotonoha::PcmPlayer bgmPlayer;
  Kotonoha::PcmPlayer voicePlayer;
  std::unordered_map<int, std::unique_ptr<Kotonoha::PcmPlayer>> sePlayers;
  std::unique_ptr<Kotonoha::MediaPlayer> moviePlayer;
  Kotonoha::VideoSkipGesture videoSkip;

  std::uint64_t lastBgmGeneration = 0;
  std::string lastDialogueKey;
  std::string lastVoiceAsset;

  // Transition & timer state.
  double waitElapsedMs = 0.0;
  double transitionElapsedMs = 0.0;
  double transitionDurationMs = 0.0;
  std::string lastBgFile;
  std::string prevBgAsset;
  double quakeRemainingMs = 0.0;
  int quakePowerX = 0;
  int quakePowerY = 0;
  Uint64 lastTickNs = 0;

  // Choice navigation state.
  int selectedChoice = 0;
  bool started = false;
  bool fastSkip = false;
};

ArtemisRuntime::ArtemisRuntime(std::shared_ptr<ArtemisAssets> assets)
    : Runtime((assets && !assets->systemConfig().title.empty())
                  ? assets->systemConfig().title
                  : "Artemis Engine",
              "0.1.0", "me.hirameki.kotonoha.artemis"),
      impl_(std::make_unique<Impl>()), assets_(std::move(assets)) {
  if (assets_) {
    vm_.setScriptLoader(
        [a = assets_](const std::string &scriptId) -> std::string {
          const std::string resolved = a->resolveScriptPath(scriptId);
          if (resolved.empty())
            return {};
          try {
            return a->loadTextUtf8(resolved, "UTF-8");
          } catch (...) {
            return {};
          }
        });
  }
}

ArtemisRuntime::~ArtemisRuntime() { shutdown(); }

void ArtemisRuntime::shutdown() {
  if (!impl_)
    return;
  impl_->bgmPlayer.Stop();
  impl_->voicePlayer.Stop();
  for (auto &[id, player] : impl_->sePlayers) {
    if (player)
      player->Stop();
  }
  impl_->sePlayers.clear();
  if (impl_->moviePlayer)
    impl_->moviePlayer->stop();
}

SDL_Point ArtemisRuntime::viewport() const {
  if (assets_) {
    const auto &cfg = assets_->systemConfig();
    if (cfg.width >= 320 && cfg.height >= 240)
      return {cfg.width, cfg.height};
  }
  return {1280, 720};
}

void ArtemisRuntime::init(Kotonoha::Engine &engine) {
  impl_->engine = &engine;
  Runtime::init(engine);
}

void ArtemisRuntime::initGlobal(Kotonoha::Engine &engine) {
  impl_->engine = &engine;
  impl_->moviePlayer =
      std::make_unique<Kotonoha::MediaPlayer>(engine.GetAudio());
  impl_->textBox = &Kotonoha::GetVNTextBoxRuntime(engine, "", "", 45.0);
  impl_->backlog = &Kotonoha::GetVNBacklogRuntime(engine);
  impl_->backlog->setVoiceReplay([this](const std::string &voiceId) {
    if (!assets_ || !impl_->engine || voiceId.empty())
      return;
    try {
      const auto clip = assets_->loadAudio(voiceId);
      impl_->voicePlayer.Play(impl_->engine->GetAudio().channel("voice"),
                              toPcmClip(clip, false, 1.0f));
    } catch (...) {
    }
  });
  impl_->menu =
      std::make_unique<ArtemisMenu>(engine, [this](const std::string &action) {
        if (action == "save" && impl_->saveMenu) {
          impl_->saveMenu->open(
              Kotonoha::applicationDirectories("artemis").saves.string(), false,
              vm_.saveTitle().empty() ? "Artemis" : vm_.saveTitle());
        } else if (action == "load" && impl_->saveMenu) {
          impl_->saveMenu->open(
              Kotonoha::applicationDirectories("artemis").saves.string(), true);
        } else if (action == "backlog" && impl_->backlog) {
          impl_->backlog->open();
        } else if (action == "lang") {
          const auto langs = vm_.availableLanguages();
          if (langs.size() > 1) {
            auto it =
                std::find(langs.begin(), langs.end(), vm_.preferredLanguage());
            const std::size_t nextIdx =
                (it == langs.end())
                    ? 0
                    : (static_cast<std::size_t>(it - langs.begin()) + 1) %
                          langs.size();
            vm_.setPreferredLanguage(langs[nextIdx]);
            vm_.jumpToBlock(vm_.currentBlock());
          }
        } else if (action == "restart") {
          debugRestartScene();
        }
      });
  impl_->saveMenu = std::make_unique<Kotonoha::VNSaveMenu>(engine);
  Runtime::initGlobal(engine);
}

void ArtemisRuntime::initSplash(Kotonoha::Engine &engine) {
  // Artemis scripts include their own brandlogo / title flow, so skip the
  // engine splash. Runtime::start() dispatches only ONE init hook based on the
  // state it saw at entry (Splash here); switching the state to Gameplay alone
  // never called initGameplay(), leaving impl_->engine null and no script
  // loaded — the result was a black screen. Call the gameplay hook explicitly.
  SetState(Kotonoha::GameState::Gameplay);
  initGameplay(engine);
}

void ArtemisRuntime::initMenu(Kotonoha::Engine &engine) {
  SetState(Kotonoha::GameState::Gameplay);
  (void)engine;
}

void ArtemisRuntime::initGameplay(Kotonoha::Engine &engine) {
  impl_->engine = &engine;
  impl_->lastTickNs = SDL_GetTicksNS();
  if (!impl_->started && assets_) {
    impl_->started = true;
    const std::string startScript = assets_->findStartScript();
    if (!startScript.empty()) {
      vm_.loadScript(startScript);
      vm_.step();
    }
  }
  Runtime::initGameplay(engine);
}

void ArtemisRuntime::update() {
  if (!impl_->engine)
    return;

  const Uint64 now = SDL_GetTicksNS();
  double dtMs = 16.6;
  if (impl_->lastTickNs != 0 && now >= impl_->lastTickNs) {
    dtMs = static_cast<double>(now - impl_->lastTickNs) / 1e6;
    dtMs = std::clamp(dtMs, 0.0, 100.0);
  }
  impl_->lastTickNs = now;

  // Update active movie playback.
  if (vm_.waitReason() == VmWaitReason::Movie && impl_->moviePlayer) {
    if (!impl_->moviePlayer->isOpen()) {
      const std::string mvPath =
          assets_ ? assets_->resolveMoviePath(vm_.movie().file) : std::string{};
      if (!mvPath.empty() && assets_) {
        try {
          impl_->moviePlayer->open(*assets_, mvPath, false, "movie");
          impl_->videoSkip.reset();
        } catch (...) {
          vm_.finishMovie();
        }
      } else {
        vm_.finishMovie();
      }
    } else {
      impl_->moviePlayer->update(impl_->engine->GetRenderer());
      if (!impl_->moviePlayer->isOpen()) {
        vm_.finishMovie();
      }
    }
  }

  // Update wait timer.
  if (vm_.waitReason() == VmWaitReason::WaitTimer) {
    impl_->waitElapsedMs += dtMs;
    if (impl_->fastSkip || impl_->waitElapsedMs >= vm_.waitTimerMs()) {
      impl_->waitElapsedMs = 0.0;
      vm_.finishWaitTimer();
    }
  }

  // Handle Ctrl fast-skip during dialogue.
  if (impl_->fastSkip && vm_.waitReason() == VmWaitReason::Dialogue) {
    vm_.advance();
  }

  // Sync BGM.
  if (assets_ && vm_.bgm().generation != impl_->lastBgmGeneration) {
    impl_->lastBgmGeneration = vm_.bgm().generation;
    if (!vm_.bgm().playing || vm_.bgm().file.empty()) {
      impl_->bgmPlayer.Stop();
    } else {
      const auto [introPath, loopPath] =
          assets_->resolveBgmPaths(vm_.bgm().file);
      const std::string chosen = !loopPath.empty() ? loopPath : introPath;
      if (!chosen.empty()) {
        try {
          const auto decoded = assets_->loadAudio(chosen);
          const float gain = static_cast<float>(vm_.bgm().volume) / 100.0f;
          impl_->bgmPlayer.Play(impl_->engine->GetAudio().channel("bgm"),
                                toPcmClip(decoded, true, gain));
        } catch (...) {
        }
      }
    }
  }

  // Consume SE events.
  for (const auto &se : vm_.consumeSeEvents()) {
    if (se.stop || se.file.empty()) {
      auto it = impl_->sePlayers.find(se.id);
      if (it != impl_->sePlayers.end() && it->second)
        it->second->Stop();
      continue;
    }
    if (!assets_)
      continue;
    const std::string sePath = assets_->resolveAudioPath(":se/", se.file);
    if (sePath.empty())
      continue;
    try {
      const auto decoded = assets_->loadAudio(sePath);
      auto &player = impl_->sePlayers[se.id];
      if (!player)
        player = std::make_unique<Kotonoha::PcmPlayer>();
      const float gain = static_cast<float>(se.volume) / 100.0f;
      player->Play(impl_->engine->GetAudio().channel("se"),
                   toPcmClip(decoded, se.loop, gain));
    } catch (...) {
    }
  }

  // Consume Voice events.
  for (const auto &vo : vm_.consumeVoiceEvents()) {
    if (!assets_ || vo.file.empty())
      continue;
    const std::string voPath =
        assets_->resolveAudioPath(vo.path.empty() ? ":vo/" : vo.path, vo.file);
    if (voPath.empty())
      continue;
    impl_->lastVoiceAsset = voPath;
    try {
      const auto decoded = assets_->loadAudio(voPath);
      impl_->voicePlayer.Play(impl_->engine->GetAudio().channel("voice"),
                              toPcmClip(decoded, false, 1.0f));
    } catch (...) {
    }
  }

  // Consume Quake events.
  for (const auto &q : vm_.consumeQuakeEvents()) {
    impl_->quakeRemainingMs =
        std::max(impl_->quakeRemainingMs, static_cast<double>(q.timeMs));
    impl_->quakePowerX = q.powerX;
    impl_->quakePowerY = q.powerY;
  }
  if (impl_->quakeRemainingMs > 0.0) {
    impl_->quakeRemainingMs = std::max(0.0, impl_->quakeRemainingMs - dtMs);
  }

  // Track background transition fade.
  if (vm_.bg().file != impl_->lastBgFile) {
    if (!impl_->lastBgFile.empty() && assets_) {
      impl_->prevBgAsset =
          assets_->resolveImagePath(vm_.bg().path, impl_->lastBgFile);
    }
    impl_->lastBgFile = vm_.bg().file;
    impl_->transitionDurationMs =
        static_cast<double>(std::max(0, vm_.bg().transitionMs));
    impl_->transitionElapsedMs = 0.0;
  }
  if (impl_->transitionDurationMs > 0.0) {
    impl_->transitionElapsedMs = std::min(impl_->transitionDurationMs,
                                          impl_->transitionElapsedMs + dtMs);
  }

  // Sync VNTextBox & Backlog.
  if (impl_->textBox) {
    const auto &dlg = vm_.dialogue();
    const std::string key =
        vm_.currentScript() + ":" + vm_.currentBlock() + ":" + dlg.text;
    if (dlg.hasText && key != impl_->lastDialogueKey) {
      impl_->lastDialogueKey = key;
      impl_->textBox->setDialogue(dlg.speaker, dlg.text);
      if (impl_->backlog && !dlg.text.empty()) {
        impl_->backlog->addEntry(dlg.speaker, dlg.text);
        if (!impl_->lastVoiceAsset.empty())
          impl_->backlog->setLastVoice(impl_->lastVoiceAsset);
      }
    }
    if (dlg.hasText && dlg.boxVisible &&
        vm_.waitReason() != VmWaitReason::Movie) {
      impl_->textBox->show();
    } else {
      impl_->textBox->hide();
    }
    impl_->textBox->update();
  }

  if (impl_->backlog && impl_->backlog->isVisible())
    impl_->backlog->update();
  if (impl_->menu && impl_->menu->IsOpen())
    impl_->menu->update();
  if (impl_->saveMenu && impl_->saveMenu->isOpen())
    impl_->saveMenu->update();

  Runtime::update();
}

void ArtemisRuntime::renderGameplay(Kotonoha::RendererRef renderer,
                                    float scale) {
  const SDL_Point vp = viewport();
  const float vw = static_cast<float>(vp.x);
  const float vh = static_cast<float>(vp.y);

  // Movie playback takes over full screen.
  if (vm_.waitReason() == VmWaitReason::Movie && impl_->moviePlayer &&
      impl_->moviePlayer->isOpen()) {
    impl_->moviePlayer->render(
        renderer, impl_->engine ? &impl_->engine->GetUpscaler() : nullptr);
    return;
  }

  // Quake offset.
  float qx = 0.0f;
  float qy = 0.0f;
  if (impl_->quakeRemainingMs > 0.0) {
    const float phase = static_cast<float>(impl_->quakeRemainingMs * 0.08);
    qx = std::sin(phase) * static_cast<float>(impl_->quakePowerX);
    qy = std::cos(phase * 1.3f) * static_cast<float>(impl_->quakePowerY);
  }

  // 1. Background.
  const auto &bg = vm_.bg();
  if (bg.isSolidColor || bg.file.empty()) {
    SDL_SetRenderDrawColor(&renderer, bg.solidR, bg.solidG, bg.solidB, 255);
    const SDL_FRect full{0.0f, 0.0f, vw, vh};
    SDL_RenderFillRect(&renderer, &full);
  } else if (assets_) {
    const std::string bgPath = assets_->resolveImagePath(bg.path, bg.file);
    if (!bgPath.empty()) {
      try {
        SDL_Texture &tex = assets_->texture(
            renderer, bgPath, scale,
            impl_->engine ? &impl_->engine->GetUpscaler() : nullptr);
        float tw = 0.0f, th = 0.0f;
        SDL_GetTextureSize(&tex, &tw, &th);
        const float zoom = static_cast<float>(bg.zoom) / 100.0f;
        const float dw = (tw > 0.0f ? tw : vw) * zoom;
        const float dh = (th > 0.0f ? th : vh) * zoom;
        // Center oversized backgrounds (e.g. 1610x910 on 1600x900) with x/y
        // offset.
        const float dx = (vw - dw) * 0.5f + static_cast<float>(bg.x) + qx;
        const float dy = (vh - dh) * 0.5f + static_cast<float>(bg.y) + qy;
        const SDL_FRect dst{dx, dy, dw, dh};
        SDL_SetTextureAlphaMod(
            &tex, static_cast<Uint8>(std::clamp(bg.alpha, 0, 255)));
        SDL_RenderTexture(&renderer, &tex, nullptr, &dst);
      } catch (...) {
      }
    }
  }

  // 2. Character / FG sprites (with multi-part file + file1 + file2
  // compositing).
  if (assets_) {
    for (const auto &spr : vm_.sprites()) {
      if (!spr.visible || spr.alpha <= 0)
        continue;
      const std::string parts[3] = {spr.file, spr.file1, spr.file2};
      for (const auto &part : parts) {
        if (part.empty())
          continue;
        const std::string imgPath = assets_->resolveImagePath(spr.path, part);
        if (imgPath.empty())
          continue;
        try {
          SDL_Texture &tex = assets_->texture(
              renderer, imgPath, scale,
              impl_->engine ? &impl_->engine->GetUpscaler() : nullptr);
          float tw = 0.0f, th = 0.0f;
          SDL_GetTextureSize(&tex, &tw, &th);
          const float zoom = static_cast<float>(spr.zoom) / 100.0f;
          const float dw = tw * zoom;
          const float dh = th * zoom;
          // Artemis FG positions are centered horizontally on stage with x/y
          // offsets.
          const float dx = (vw - dw) * 0.5f + static_cast<float>(spr.x) + qx;
          const float dy = (vh - dh) + static_cast<float>(spr.y) + qy;
          const SDL_FRect dst{dx, dy, dw, dh};
          SDL_SetTextureAlphaMod(
              &tex, static_cast<Uint8>(std::clamp(spr.alpha, 0, 255)));
          SDL_RenderTexture(&renderer, &tex, nullptr, &dst);
        } catch (...) {
        }
      }
    }
  }

  // 3. Color tone overlay (sepia / mono / dark).
  if (!vm_.colorTone().empty()) {
    SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
    if (vm_.colorTone() == "sepia") {
      SDL_SetRenderDrawColor(&renderer, 112, 66, 20, 72);
      const SDL_FRect full{0.0f, 0.0f, vw, vh};
      SDL_RenderFillRect(&renderer, &full);
    } else if (vm_.colorTone() == "night" || vm_.colorTone() == "dark") {
      SDL_SetRenderDrawColor(&renderer, 12, 20, 48, 84);
      const SDL_FRect full{0.0f, 0.0f, vw, vh};
      SDL_RenderFillRect(&renderer, &full);
    }
  }

  // 4. Dialogue box.
  if (impl_->textBox && impl_->textBox->isVisible()) {
    impl_->textBox->render(renderer, scale);
  }

  // 5. Choices overlay.
  if (vm_.waitReason() == VmWaitReason::Choice && !vm_.choices().empty() &&
      impl_->engine) {
    const auto &choices = vm_.choices();
    const float btnW = vw * 0.6f;
    const float btnH = 56.0f;
    const float gap = 14.0f;
    const float totalH =
        static_cast<float>(choices.size()) * (btnH + gap) - gap;
    const float startX = (vw - btnW) * 0.5f;
    const float startY = (vh - totalH) * 0.45f;

    for (std::size_t i = 0; i < choices.size(); ++i) {
      const SDL_FRect rect{
          startX, startY + static_cast<float>(i) * (btnH + gap), btnW, btnH};
      const bool focused = (static_cast<int>(i) == impl_->selectedChoice);
      impl_->engine->GetUI().button(renderer, rect, choices[i].text, focused);
    }
  }

  // 6. Modal overlays (backlog / in-game menu / save menu).
  if (impl_->backlog && impl_->backlog->isVisible())
    impl_->backlog->render(renderer, scale);
  if (impl_->menu && impl_->menu->IsOpen())
    impl_->menu->render(renderer, scale);
  if (impl_->saveMenu && impl_->saveMenu->isOpen())
    impl_->saveMenu->render(renderer, scale);
}

bool ArtemisRuntime::onEvent(Kotonoha::EventRef event) {
  if (vm_.waitReason() == VmWaitReason::Movie && impl_->moviePlayer &&
      impl_->moviePlayer->isOpen()) {
    if (impl_->videoSkip.accept(event) && vm_.movie().cancellable)
      return handleAdvanceAction();
    return true;
  }
  // Route to open modal overlays first.
  if (impl_->saveMenu && impl_->saveMenu->isOpen())
    return impl_->saveMenu->onEvent(event);
  if (impl_->menu && impl_->menu->IsOpen())
    return impl_->menu->onEvent(event);
  if (impl_->backlog && impl_->backlog->isVisible())
    return impl_->backlog->onEvent(event);

  if (event.type == SDL_EVENT_KEY_DOWN) {
    const SDL_Keycode key = event.key.key;
    if (key == SDLK_LCTRL || key == SDLK_RCTRL) {
      impl_->fastSkip = true;
      return true;
    }
    if (key == SDLK_ESCAPE) {
      if (impl_->menu)
        impl_->menu->open();
      return true;
    }
    if (key == SDLK_L) {
      return handleBacklogAction();
    }
    if (key == SDLK_H && impl_->textBox) {
      impl_->textBox->toggleUserHidden();
      return true;
    }
    if (vm_.waitReason() == VmWaitReason::Choice && !vm_.choices().empty()) {
      const int n = static_cast<int>(vm_.choices().size());
      if (key == SDLK_UP) {
        impl_->selectedChoice = (impl_->selectedChoice + n - 1) % n;
        return true;
      }
      if (key == SDLK_DOWN) {
        impl_->selectedChoice = (impl_->selectedChoice + 1) % n;
        return true;
      }
      if (key == SDLK_RETURN || key == SDLK_SPACE) {
        vm_.selectChoice(static_cast<std::size_t>(impl_->selectedChoice));
        impl_->selectedChoice = 0;
        return true;
      }
    }
    if (key == SDLK_RETURN || key == SDLK_SPACE) {
      return handleAdvanceAction();
    }
  } else if (event.type == SDL_EVENT_KEY_UP) {
    if (event.key.key == SDLK_LCTRL || event.key.key == SDLK_RCTRL) {
      impl_->fastSkip = false;
      return true;
    }
  } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    if (event.button.button == SDL_BUTTON_RIGHT) {
      if (impl_->menu)
        impl_->menu->open();
      return true;
    }
    if (event.button.button == SDL_BUTTON_LEFT) {
      if (vm_.waitReason() == VmWaitReason::Choice && !vm_.choices().empty()) {
        const SDL_Point vp = viewport();
        const float vw = static_cast<float>(vp.x);
        const float vh = static_cast<float>(vp.y);
        const float btnW = vw * 0.6f;
        const float btnH = 56.0f;
        const float gap = 14.0f;
        const float totalH =
            static_cast<float>(vm_.choices().size()) * (btnH + gap) - gap;
        const float startX = (vw - btnW) * 0.5f;
        const float startY = (vh - totalH) * 0.45f;
        for (std::size_t i = 0; i < vm_.choices().size(); ++i) {
          const float y = startY + static_cast<float>(i) * (btnH + gap);
          if (event.button.x >= startX && event.button.x <= startX + btnW &&
              event.button.y >= y && event.button.y <= y + btnH) {
            vm_.selectChoice(i);
            impl_->selectedChoice = 0;
            return true;
          }
        }
        return true;
      }
      return handleAdvanceAction();
    }
  } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    const auto btn = event.gbutton.button;
    if (btn == SDL_GAMEPAD_BUTTON_SOUTH)
      return handleAdvanceAction();
    if (btn == SDL_GAMEPAD_BUTTON_START && impl_->menu) {
      impl_->menu->open();
      return true;
    }
    if (btn == SDL_GAMEPAD_BUTTON_NORTH)
      return handleBacklogAction();
    if (vm_.waitReason() == VmWaitReason::Choice && !vm_.choices().empty()) {
      const int n = static_cast<int>(vm_.choices().size());
      if (btn == SDL_GAMEPAD_BUTTON_DPAD_UP) {
        impl_->selectedChoice = (impl_->selectedChoice + n - 1) % n;
        return true;
      }
      if (btn == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
        impl_->selectedChoice = (impl_->selectedChoice + 1) % n;
        return true;
      }
    }
  }

  return Runtime::onEvent(event);
}

bool ArtemisRuntime::handleAdvanceAction() {
  if (vm_.waitReason() == VmWaitReason::Movie && impl_->moviePlayer &&
      vm_.movie().cancellable) {
    impl_->moviePlayer->stop();
    vm_.finishMovie();
    return true;
  }
  if (impl_->textBox && impl_->textBox->isVisible() &&
      !impl_->textBox->isDialogueComplete()) {
    impl_->textBox->completeDialogue();
    return true;
  }
  return vm_.advance();
}

bool ArtemisRuntime::advanceStoryTime(double seconds) {
  if (vm_.waitReason() == VmWaitReason::WaitTimer) {
    impl_->waitElapsedMs += seconds * 1000.0;
    if (impl_->waitElapsedMs >= vm_.waitTimerMs()) {
      impl_->waitElapsedMs = 0.0;
      vm_.finishWaitTimer();
    }
    return true;
  }
  return false;
}

bool ArtemisRuntime::handleBacklogAction() {
  if (!impl_->backlog)
    return false;
  if (impl_->backlog->isVisible())
    impl_->backlog->close();
  else
    impl_->backlog->open();
  return true;
}

void ArtemisRuntime::saveGame(const std::string &path) {
  const std::string state = vm_.serializeState();
  std::vector<std::uint8_t> bytes(state.begin(), state.end());
  Kotonoha::Filesystem::writeBinaryFileAtomically(std::filesystem::path(path),
                                                  bytes);
}

void ArtemisRuntime::loadGame(const std::string &path) {
  auto vfs = Kotonoha::Filesystem::Filesystem::mount(
      std::filesystem::path(path).parent_path().string());
  if (!vfs)
    return;
  const std::string fname = std::filesystem::path(path).filename().string();
  if (!vfs->exists(fname))
    return;
  auto handle = vfs->open(fname);
  if (!handle)
    return;
  const auto sz = handle->size();
  if (sz <= 0)
    return;
  std::string data(static_cast<std::size_t>(sz), '\0');
  const auto got = handle->read(data.data(), data.size());
  if (got > 0) {
    data.resize(static_cast<std::size_t>(got));
    vm_.deserializeState(data);
  }
}

std::string ArtemisRuntime::debugNode() const {
  return vm_.currentScript() + ":" + vm_.currentBlock();
}

std::vector<Kotonoha::DebugNode> ArtemisRuntime::debugNodes() const {
  std::vector<Kotonoha::DebugNode> out;
  for (const auto &blk : vm_.currentScriptBlocks()) {
    Kotonoha::DebugNode node;
    node.id = blk;
    node.label = vm_.currentScript() + " / " + blk;
    out.push_back(std::move(node));
  }
  return out;
}

std::vector<std::string> ArtemisRuntime::activeTranslationKeys() const {
  const auto &dialogue = vm_.dialogue();
  std::vector<std::string> keys;
  if (!dialogue.speaker.empty())
    keys.push_back(dialogue.speaker);
  if (!dialogue.text.empty())
    keys.push_back(dialogue.text);
  return keys;
}

Kotonoha::Translation::SourceTexts ArtemisRuntime::translationSources() const {
  Kotonoha::Translation::SourceTexts result;
  const auto keys = activeTranslationKeys();
  if (!keys.empty())
    result[translationRoute()] = keys;
  return result;
}

std::vector<Kotonoha::DebugAudioAsset> ArtemisRuntime::debugSceneAudio() {
  if (!impl_ || impl_->lastVoiceAsset.empty())
    return {};
  const auto &dialogue = vm_.dialogue();
  return {{impl_->lastVoiceAsset, dialogue.speaker, dialogue.text}};
}

void ArtemisRuntime::debugJumpToNode(const std::string &id) {
  vm_.jumpToBlock(id);
}

void ArtemisRuntime::debugRestartScene() {
  if (!assets_)
    return;
  vm_.reset();
  const std::string startScript = assets_->findStartScript();
  if (!startScript.empty()) {
    vm_.loadScript(startScript);
    vm_.step();
  }
}

std::string ArtemisRuntime::dialogueText() const { return vm_.dialogue().text; }

std::string ArtemisRuntime::dialogueSpeaker() const {
  return vm_.dialogue().speaker;
}

} // namespace Artemis
