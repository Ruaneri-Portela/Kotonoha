#include "AmagamiRuntime.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace Amagami {
void AmagamiTextBox::renderSpeaker(Kotonoha::RendererRef r) {
  auto name = speakerName;
  auto n = Kotonoha::VNTextBoxUtility::CountUtf8Chars(name);
  if (n == 1)
    name = "　" + name + "　";
  else if (n == 2)
    name.insert(Kotonoha::VNTextBoxUtility::NextUtf8Char(name, 0), "　");
  engine.GetUI().textSized(r, name, 44, 288, 90, 24);
}
void AmagamiTextBox::renderDialogue(Kotonoha::RendererRef r) {
  const float x = speakerName.empty() ? 44 : 136;
  auto measure = [&](const std::string &text) {
    return engine.GetUI().measureSized(text, 2000, 24).x;
  };
  const auto lines = Kotonoha::VNTextBoxUtility::RevealLines(
      fullDialogue, visibleDialogue.size(), measure, 596 - x);
  SDL_Rect clip{40, 284, 560, 132};
  SDL_SetRenderClipRect(&r, &clip);
  for (size_t i = 0; i < lines.size(); ++i)
    engine.GetUI().textSized(r, lines[i], x, 288 + float(i) * 44, 596 - x, 24);
  SDL_SetRenderClipRect(&r, nullptr);
}
void AmagamiTextBox::renderIndicator(Kotonoha::RendererRef) {}
void AmagamiChoices::layout() {
  for (size_t i = 0; i < options.size(); ++i)
    options[i].hitBox = {40, 284 + float(i) * 44, 560, 36};
}
void AmagamiChoices::renderPanel(Kotonoha::RendererRef r) {
  auto draw = [&](const std::string &id, SDL_FRect rect) {
    auto &tex = assets_.texture(r, id);
    SDL_RenderTexture(&r, &tex, nullptr, &rect);
  };
  draw("sysgraph/win1.tm2", {24, 266, 592, 160});
  draw("sysgraph/win1_base.tm2", {40, 243, 144, 24});
  draw("sysgraph/win1_text.tm2", {40, 243, 144, 24});
}
void AmagamiChoices::renderOption(Kotonoha::RendererRef r, size_t i,
                                  bool focused) {
  auto b = options[i].hitBox;
  if (focused) {
    auto &tex = assets_.texture(r, "sysgraph/cursor_base.tm2");
    SDL_SetTextureColorMod(&tex, 255, 145, 165);
    SDL_SetTextureAlphaMod(&tex, 140);
    SDL_RenderTexture(&r, &tex, nullptr, &b);
    SDL_SetTextureColorMod(&tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(&tex, 255);
  }
  engine.GetUI().textSized(r, options[i].label, b.x + 4, b.y + 3, b.w - 8, 24);
}
void AmagamiRuntime::syncChoices() {
  if (!choices_ || !session_)
    return;
  if (session_->choices().empty()) {
    choices_->hide();
    return;
  }
  if (choices_->isActive())
    return;
  std::vector<Kotonoha::ChoiceOption> options;
  for (size_t i = 0; i < session_->choices().size(); ++i)
    options.push_back({session_->choices()[i].text, int(i)});
  choices_->setQuestion("", options, [this](int i, const std::string &) {
    voice_.Stop();
    session_->choose(size_t(i));
    syncDialogue();
  });
}
void AmagamiRuntime::syncDialogue() {
  if (!session_ || !textBox_ || displayed_ == session_->history().size())
    return;
  displayed_ = session_->history().size();
  const auto &line = session_->dialogue();
  auto route = int(assets_->source()->scenarioIndex(session_->name()));
  textBox_->setDialogue(
      line.speaker,
      DisplayText(engine_->GetTranslation().text(route, line.original)));
}

AmagamiRuntime::AmagamiRuntime(std::shared_ptr<Assets> assets,
                               std::string start)
    : Runtime("Amagami", "0.1.0", "me.hirameki.kotonoha.amagami"),
      assets_(std::move(assets)), start_(std::move(start)) {}
AmagamiRuntime::~AmagamiRuntime() { shutdown(); }
void AmagamiRuntime::shutdown() {
  voice_.Stop();
  music_.Stop();
  if (assets_)
    assets_->clearCache();
  session_.reset();
}
void AmagamiRuntime::initGlobal(Kotonoha::Engine &e) {
  engine_ = &e;
  if (!choices_)
    choices_ = std::make_unique<AmagamiChoices>(e, *assets_);
  if (!textBox_)
    textBox_ = std::make_unique<AmagamiTextBox>(e);
  char *p = SDL_GetPrefPath("Kotonoha", "amagami");
  if (p) {
    savePath_ = (std::filesystem::path(p) / "quick-save.txt").string();
    SDL_free(p);
  } else
    throw std::runtime_error("Cannot locate Amagami save folder");
}
void AmagamiRuntime::initMenu(Kotonoha::Engine &) {
  if (!start_.empty()) {
    auto name = start_;
    start_.clear();
    start(name);
  }
}
void AmagamiRuntime::initGameplay(Kotonoha::Engine &) {}
void AmagamiRuntime::start(const std::string &name) {
  error_.clear();
  notice_.clear();
  session_ = std::make_unique<Session>(assets_->source());
  session_->sound = [this](const std::string &kind, int n) {
    if (n < 0) {
      music_.Stop();
      return;
    }
    if (kind == "music")
      audio("music/" + std::to_string(n), "bgm", true);
    else if (kind == "music-gain")
      music_.SetGain(0.48f * n / 100.0f);
  };
  session_->setRealtime(true);
  frameClock_.reset();
  choices_->hide();
  session_->start(name);
  paused_ = false;
  backlog_ = false;
  focus_ = 0;
  voiced_ = 0;
  displayed_ = 0;
  syncDialogue();
  SetState(Kotonoha::GameState::Gameplay);
}
void AmagamiRuntime::audio(const std::string &id, const std::string &channel,
                           bool loop) {
  try {
    auto clip = assets_->loadAudio(id);
    if (clip.data.empty())
      return;
    SDL_AudioSpec dst{SDL_AUDIO_S16, clip.spec.channels, clip.spec.freq};
    Uint8 *converted = nullptr;
    int bytes = 0;
    if (!SDL_ConvertAudioSamples(&clip.spec, clip.data.data(),
                                 int(clip.data.size()), &dst, &converted,
                                 &bytes))
      throw std::runtime_error(SDL_GetError());
    Kotonoha::PcmPlayer::Clip pcm;
    pcm.channels = dst.channels;
    pcm.rate = dst.freq;
    pcm.loop = loop;
    pcm.gain = channel == "bgm" ? 0.48f : 0.8f;
    pcm.samples.resize(bytes / 2);
    std::memcpy(pcm.samples.data(), converted, bytes);
    SDL_free(converted);
    // ADX v4 stores loop samples after its per-channel predictor histories.
    // Trim at loop end so PcmPlayer repeats only the original loop region.
    if (loop) {
      const auto bytes = assets_->read(id);
      auto be32 = [&](size_t p) -> uint32_t {
        return uint32_t(bytes[p]) << 24 | uint32_t(bytes[p + 1]) << 16 |
               uint32_t(bytes[p + 2]) << 8 | bytes[p + 3];
      };
      const size_t loops = 0x18 + std::max(8, pcm.channels * 4);
      if (bytes.size() >= loops + 24 && bytes[4] == 3 && bytes[0x12] == 4) {
        const size_t dataStart = (size_t(bytes[2]) << 8 | bytes[3]) + 4;
        if (dataStart >= loops + 30 && be32(loops + 4)) {
          const size_t begin = be32(loops + 8), end = be32(loops + 16);
          if (begin >= end || end > pcm.samples.size() / pcm.channels)
            throw std::runtime_error("Invalid ADX loop samples");
          pcm.loopStart = begin;
          pcm.samples.resize(end * pcm.channels);
        }
      }
    }
    auto &player = channel == "bgm" ? music_ : voice_;
    player.Play(engine_->GetAudio().channel(channel), std::move(pcm));
  } catch (const std::exception &e) {
    SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "Amagami audio %s: %s", id.c_str(),
                e.what());
  }
}
void AmagamiRuntime::advance() {
  if (!session_ || session_->ended() || session_->waiting())
    return;
  syncDialogue();
  if (session_->choices().empty() && textBox_ &&
      !textBox_->isDialogueComplete()) {
    textBox_->completeDialogue();
    return;
  }
  voice_.Stop();
  if (session_->choices().empty())
    session_->advance();
  else
    session_->choose(focus_);
  focus_ = 0;
  syncDialogue();
}
void AmagamiRuntime::picture(SDL_Renderer *r, const std::string &id,
                             SDL_FRect rect) {
  if (!assets_->contains(id))
    return;
  auto &t = assets_->texture(*r, id);
  SDL_RenderTexture(r, &t, nullptr, &rect);
}
void AmagamiRuntime::actor(SDL_Renderer *r, const Visual &v) {
  const float zoom = 0.5f * v.scaleX;
  auto draw = [&](const std::string &file, bool face) {
    if (!assets_->contains(file))
      return;
    auto mesh = file.substr(0, file.size() - 5) + "z.zb";
    if (!assets_->contains(mesh))
      return;
    auto bytes = assets_->read(mesh);
    if (bytes.size() % 32 || bytes.size() > 1024 * 1024)
      throw std::runtime_error("Invalid actor mesh: " + mesh);
    auto &tex = assets_->texture(*r, file);
    SDL_SetTextureAlphaModFloat(&tex, std::clamp(v.alpha, 0.0f, 1.0f));
    for (size_t i = 0; i < bytes.size() / 32; ++i) {
      float p[6];
      std::memcpy(p, bytes.data() + i * 32, 24);
      uint32_t group = 0;
      std::memcpy(&group, bytes.data() + i * 32 + 24, 4);
      if (face) {
        size_t expression = size_t(std::clamp(v.expression, 0, 8));
        if (group < 1 || group > 3)
          continue;
        const size_t count = bytes.size() / 32 / 3;
        const size_t selected =
            (group - 1) * count + expression * 3 + (group == 3 ? 2 : 0);
        if (i != selected)
          continue;
      }
      if (!std::all_of(std::begin(p), std::end(p), [](float f) {
            return std::isfinite(f) && std::abs(f) < 16384;
          }))
        throw std::runtime_error("Invalid actor mesh coordinates");
      SDL_FRect src{p[2] + 0.5f, p[3] + 0.5f, p[4] - 1, p[5] - 1};
      SDL_FRect dst{320 + v.x + (p[0] - 400) * zoom, v.y + (p[1] - 160) * zoom,
                    p[4] * zoom, p[5] * zoom};
      SDL_RenderTexture(r, &tex, &src, &dst);
    }
    SDL_SetTextureAlphaModFloat(&tex, 1);
  };
  draw(v.file, false);
  if (!v.face.empty())
    draw(v.face, true);
}
void AmagamiRuntime::render(Kotonoha::RendererRef r, float scale) {
  if (GetState() == Kotonoha::GameState::Splash) {
    Runtime::render(r, scale);
    return;
  }
  Kotonoha::UI::FrameScope frame(r, Kotonoha::UI::fit({640, 448}, viewport()));
  auto &ui = engine_->GetUI();
  ui.panel(r, {0, 0, 640, 448}, {0, 0, 0, 255});
  if (!session_) {
    picture(&r, "bg/164_10.tm2", {0, 0, 640, 448});
    picture(&r, "bg/140_20.tm2", {0, 0, 640, 448});
    picture(&r, "sysgraph/rogo1snow.tm2", {468, 128, 64, 64});
    picture(&r, "sysgraph/copy.tm2", {16, 404, 320, 24});
    if (!titleMenu_) {
      const float alpha = 0.65f + 0.35f * std::sin(float(SDL_GetTicks()) / 500);
      auto &tex = assets_->texture(r, "sysgraph/txt_start.tm2");
      SDL_SetTextureAlphaModFloat(&tex, alpha);
      SDL_FRect dst{196, 320, 248, 32};
      SDL_RenderTexture(&r, &tex, nullptr, &dst);
      SDL_SetTextureAlphaModFloat(&tex, 1);
    } else {
      picture(&r, "sysgraph/menu_main.tm2", {232, 245, 176, 56});
      for (size_t i = 0; i < 3; ++i) {
        SDL_FRect b{264, float(305 + i * 36), 112, 32};
        if (focus_ == i) {
          auto &tex = assets_->texture(r, "sysgraph/cursor_base.tm2");
          SDL_RenderTexture(&r, &tex, nullptr, &b);
        }
        if (i < 2)
          picture(&r, "sysgraph/menu_main" + std::to_string(i) + ".tm2", b);
        else
          ui.textSized(r, "終了", b.x + 32, b.y + 3, 80, 22);
      }
    }
  } else {
    for (const auto &[name, v] : session_->visuals()) {
      if (!v.visible || v.file.empty())
        continue;
      if (v.file.rfind("stand/b", 0) == 0)
        actor(&r, v);
      else {
        auto &tex = assets_->texture(r, v.file);
        SDL_SetTextureAlphaModFloat(&tex, std::clamp(v.alpha, 0.0f, 1.0f));
        SDL_FRect dst{320 + v.x - 320 * v.scaleX, 224 + v.y - 224 * v.scaleY,
                      640 * v.scaleX, 448 * v.scaleY};
        SDL_RenderTexture(&r, &tex, nullptr, &dst);
        SDL_SetTextureAlphaModFloat(&tex, 1);
      }
    }
    if (session_->stageFade() > 0) {
      Uint8 shade = session_->whiteFade() ? 255 : 0;
      ui.panel(r, {0, 0, 640, 448},
               {shade, shade, shade, Uint8(session_->stageFade() * 255)});
    }
    syncChoices();
    if (choices_->isActive())
      choices_->render(r, scale);
    else if (session_->messageVisible()) {
      picture(&r, "sysgraph/win0.tm2", {24, 266, 592, 160});
      syncDialogue();
      if (textBox_) {
        textBox_->show();
        textBox_->render(r, scale);
        if (textBox_->isDialogueComplete())
          picture(&r, "sysgraph/flower.tm2", {572, 394, 24, 24});
      }
    }
    if (session_->ended()) {
      ui.panel(r, {60, 130, 520, 120});
      ui.textSized(r, "このイベントは終了しました", 85, 150, 470, 25);
      ui.textSized(r, "Enter: タイトルへ", 85, 195, 470, 20);
    }
    if (paused_) {
      ui.backdrop(r);
      const std::vector<std::string> menu = {"戻る", "セーブ", "ロード",
                                             "タイトルへ"};
      for (size_t i = 0; i < menu.size(); ++i) {
        SDL_FRect b{180, float(100 + i * 54), 280, 44};
        ui.panel(r, b,
                 i == focus_ ? SDL_Color{133, 58, 110, 255}
                             : SDL_Color{25, 30, 50, 255});
        ui.textSized(r, menu[i], b.x + 15, b.y + 8, b.w - 30, 24);
      }
    }
    if (backlog_) {
      ui.panel(r, {10, 10, 620, 420}, {10, 15, 30, 250});
      float y = 20;
      const auto &history = session_->history();
      size_t begin = history.size() > 4 ? history.size() - 4 : 0;
      for (size_t i = begin; i < history.size(); ++i) {
        ui.textSized(r, history[i].speaker, 25, y, 590, 16,
                     {255, 170, 210, 255});
        y += 22;
        y += ui.textSized(r, history[i].text, 25, y, 590, 18) + 12;
      }
    }
  }
  if (!notice_.empty())
    ui.textSized(r, notice_, 10, 5, 620, 14, {255, 235, 150, 255});
  if (!error_.empty()) {
    ui.panel(r, {20, 30, 600, 180}, {60, 15, 25, 250});
    ui.textSized(r, "Execution stopped: " + error_, 35, 45, 570, 16);
  }
}
void AmagamiRuntime::activate(size_t i) {
  if (!session_) {
    if (!titleMenu_) {
      titleMenu_ = true;
      focus_ = 0;
      return;
    }
    if (i == 0)
      start("n0dea_ev01A");
    else if (i == 1) {
      if (!session_)
        session_ = std::make_unique<Session>(assets_->source());
      loadGame(savePath_);
    } else {
      SDL_Event quit{};
      quit.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&quit);
    }
    return;
  }
  if (paused_) {
    if (i == 1)
      saveGame(savePath_);
    else if (i == 2)
      loadGame(savePath_);
    else if (i == 3) {
      shutdown();
      SetState(Kotonoha::GameState::Menu);
    }
    paused_ = false;
    focus_ = 0;
    return;
  }
  if (session_->ended()) {
    shutdown();
    SetState(Kotonoha::GameState::Menu);
    focus_ = 0;
    return;
  }
  advance();
}
void AmagamiRuntime::onEvent(Kotonoha::EventRef event) {
  if (GetState() == Kotonoha::GameState::Splash) {
    Runtime::onEvent(event);
    return;
  }
  try {
    auto e =
        Kotonoha::UI::toFrame(event, Kotonoha::UI::fit({640, 448}, viewport()));
    syncChoices();
    if (session_ && !paused_ && !backlog_ && choices_->isActive()) {
      if (e.type == SDL_EVENT_KEY_DOWN && e.key.key >= SDLK_1 &&
          e.key.key <= SDLK_9 && !e.key.repeat) {
        size_t i = e.key.key - SDLK_1;
        if (i < choices_->getOptions().size())
          choices_->chooseOption(i);
        return;
      }
      const bool globalKey =
          e.type == SDL_EVENT_KEY_DOWN &&
          (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_PAGEUP ||
           e.key.key == SDLK_F5 || e.key.key == SDLK_F9);
      if (!globalKey) {
        choices_->onEvent(e);
        return;
      }
    }
    size_t count = !session_ ? 3 : paused_ ? 4 : session_->choices().size();
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
      if (e.key.key == SDLK_ESCAPE) {
        if (!error_.empty()) {
          error_.clear();
          shutdown();
        } else {
          paused_ = !paused_;
          focus_ = 0;
        }
        return;
      }
      if (e.key.key == SDLK_PAGEUP) {
        backlog_ = !backlog_;
        return;
      }
      if (e.key.key == SDLK_F5 && session_) {
        saveGame(savePath_);
        notice_ = "Saved";
        return;
      }
      if (e.key.key == SDLK_F9 && session_) {
        loadGame(savePath_);
        notice_ = "Loaded";
        return;
      }
      if (backlog_) {
        backlog_ = false;
        return;
      }
      if (count && (e.key.key == SDLK_UP || e.key.key == SDLK_DOWN)) {
        focus_ = (focus_ + (e.key.key == SDLK_UP ? count - 1 : 1)) % count;
        return;
      }
      if (e.key.key >= SDLK_1 && e.key.key <= SDLK_9 && session_ &&
          !session_->choices().empty()) {
        size_t i = e.key.key - SDLK_1;
        if (i < count) {
          focus_ = i;
          activate(i);
        }
        return;
      }
      if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) {
        if (error_.empty())
          activate(focus_);
        return;
      }
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        e.button.button == SDL_BUTTON_LEFT && error_.empty()) {
      if (backlog_) {
        backlog_ = false;
        return;
      }
      if (!session_ && !titleMenu_) {
        activate(0);
        return;
      }
      for (size_t i = 0; i < count; ++i) {
        SDL_FRect b = !session_ ? SDL_FRect{264, float(305 + i * 36), 112, 32}
                      : paused_ ? SDL_FRect{180, float(100 + i * 54), 280, 44}
                                : SDL_FRect{55, float(70 + i * 49), 530, 44};
        if (Kotonoha::UI::clicked(e, b)) {
          focus_ = i;
          activate(i);
          return;
        }
      }
      if (count == 0)
        activate(0);
    }
  } catch (const std::exception &e) {
    error_ = e.what();
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Amagami: %s", e.what());
  }
}
void AmagamiRuntime::update() {
  Runtime::update();
  const double delta = frameClock_.seconds();
  frameClock_.reset();
  if (session_ && !paused_ && !backlog_) {
    try {
      session_->tick(delta);
    } catch (const std::exception &e) {
      error_ = e.what();
    }
  }
  syncDialogue();
  syncChoices();
  if (textBox_) {
    if (paused_ || backlog_ || !session_ || !session_->messageVisible())
      textBox_->hide();
    else
      textBox_->show();
    textBox_->update();
  }
  music_.SetPaused(paused_ || backlog_);
  voice_.SetPaused(paused_ || backlog_);
  if (session_ && !paused_ && voiced_ != session_->history().size()) {
    voiced_ = session_->history().size();
    const auto &line = session_->dialogue();
    if (line.voice >= 0 && !line.voiceCode.empty())
      audio("voice/" + line.voiceCode + "/" + std::to_string(line.voice),
            "voice");
  }
}
std::string AmagamiRuntime::debugNode() const {
  return session_ ? session_->location() : "title";
}
std::vector<Kotonoha::DebugNode> AmagamiRuntime::debugNodes() const {
  std::vector<Kotonoha::DebugNode> out;
  for (const auto &name : assets_->source()->scenarios())
    out.push_back({name, name, {}});
  return out;
}
void AmagamiRuntime::debugJumpToNode(const std::string &id) { start(id); }
std::vector<int> AmagamiRuntime::translationRoutes() const {
  std::vector<int> out;
  for (size_t i = 0; i < assets_->source()->scenarios().size(); ++i)
    out.push_back(int(i));
  return out;
}
std::vector<std::string> AmagamiRuntime::translationTexts(int route) const {
  return route < 0 ? std::vector<std::string>{}
                   : assets_->source()->script(route).texts();
}
void AmagamiRuntime::saveGame(const std::string &path) {
  if (!session_)
    throw std::runtime_error("No active Amagami game");
  session_->save(path);
}
void AmagamiRuntime::loadGame(const std::string &path) {
  if (!session_)
    session_ = std::make_unique<Session>(assets_->source());
  session_->load(path);
  session_->setRealtime(true);
  frameClock_.reset();
  choices_->hide();
  session_->sound = [this](const std::string &kind, int n) {
    if (kind == "music-gain")
      music_.SetGain(0.48f * n / 100.0f);
    else if (n < 0)
      music_.Stop();
    else if (kind == "music")
      audio("music/" + std::to_string(n), "bgm", true);
  };
  voice_.Stop();
  music_.Stop();
  if (session_->musicTrack() >= 0)
    audio("music/" + std::to_string(session_->musicTrack()), "bgm", true);
  music_.SetGain(0.48f * session_->musicGain() / 100.0f);
  SetState(Kotonoha::GameState::Gameplay);
  paused_ = false;
  voiced_ = 0;
  displayed_ = 0;
  syncDialogue();
}
} // namespace Amagami
