#include "AmagamiRuntime.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace Amagami {
void AmagamiTextBox::setOriginalKey(const std::string &key) {
  originalKey_ = key;
  waits_.clear();
  nextWait_ = 0;
  pauseSeconds_ = 0;
  for (size_t i = 0; i + 1 < key.size(); ++i) {
    if (key[i] != 'W' || key[i + 1] < '0' || key[i + 1] > '9')
      continue;
    size_t at = i + 1;
    int frames = 0;
    while (at < key.size() && key[at] >= '0' && key[at] <= '9')
      frames = std::min(36000, frames * 10 + key[at++] - '0');
    waits_.emplace_back(Kotonoha::VNTextBoxUtility::CountUtf8Chars(
                            DisplayText(key.substr(0, i))),
                        frames / 60.0);
    i = at - 1;
  }
}
void AmagamiTextBox::completeDialogue() {
  nextWait_ = waits_.size();
  pauseSeconds_ = 0;
  VNTextBox::completeDialogue();
}
void AmagamiTextBox::update() {
  const double delta = clock.seconds();
  if (!visible || engine.isStoryPaused()) {
    VNTextBox::update();
    return;
  }
  if (pauseSeconds_ > 0) {
    clock.reset();
    pauseSeconds_ = std::max(0.0, pauseSeconds_ - delta);
    return;
  }
  VNTextBox::update();
  if (nextWait_ < waits_.size() && visibleChars >= waits_[nextWait_].first) {
    const auto [at, seconds] = waits_[nextWait_++];
    pauseSeconds_ =
        std::max(0.0, seconds - (visibleChars - at) / charactersPerSecond());
    visibleChars = double(at);
    visibleDialogue =
        Kotonoha::VNTextBoxUtility::GetSubStringUtf8(fullDialogue, at);
    isComplete = false;
  }
}
void AmagamiTextBox::translationChanged(const std::string &original,
                                        const std::string &previous,
                                        const std::string &translated) {
  if (original == originalKey_) {
    const auto replacement =
        translated == originalKey_ ? DisplayText(originalKey_) : translated;
    replaceTranslation(fullDialogue, fullDialogue, replacement);
  } else
    VNTextBox::translationChanged(original, previous, translated);
}
void AmagamiTextBox::renderSpeaker(Kotonoha::RendererRef r) {
  auto name = speakerName;
  auto n = Kotonoha::VNTextBoxUtility::CountUtf8Chars(name);
  if (n == 1)
    name = "　" + name + "　";
  else if (n == 2)
    name.insert(Kotonoha::VNTextBoxUtility::NextUtf8Char(name, 0), "　");
  float nameSize = 24;
  while (nameSize > 14 &&
         engine.GetVNUI().measureSized(name, 2000, nameSize).x > 90)
    nameSize -= 1;
  engine.GetVNUI().textSized(r, name, 44, 288, 90, nameSize,
                             engine.GetSettings().vnTextColorOverride
                                 ? engine.GetSettings().vnSpeakerColor
                                 : SDL_Color{240, 240, 245, 255});
}
void AmagamiTextBox::renderDialogue(Kotonoha::RendererRef r) {
  const float x = speakerName.empty() ? 44 : 136;
  const auto key = speakerName + '\0' + fullDialogue + '\0' +
                   engine.GetSettings().vnFontPath + '\0' +
                   engine.GetSettings().fontPath;
  if (key != layoutKey_) {
    layoutKey_ = key;
    for (layoutFont_ = 24; layoutFont_ >= 14; layoutFont_ -= 2) {
      auto width = [&](const std::string &text) {
        return engine.GetVNUI().measureSized(text, 2000, layoutFont_).x;
      };
      const auto count = Kotonoha::VNTextBoxUtility::RevealLines(
                             fullDialogue, fullDialogue.size(), width, 596 - x)
                             .size();
      layoutStep_ = count <= 3 ? 44 : 128.0f / float(count);
      if (layoutStep_ >= layoutFont_ * 1.1f || layoutFont_ == 14)
        break;
    }
  }
  auto measure = [&](const std::string &text) {
    return engine.GetVNUI().measureSized(text, 2000, layoutFont_).x;
  };
  const auto lines = Kotonoha::VNTextBoxUtility::RevealLines(
      fullDialogue, visibleDialogue.size(), measure, 596 - x);
  SDL_Rect clip{40, 284, 560, 132};
  SDL_SetRenderClipRect(&r, &clip);
  for (size_t i = 0; i < lines.size(); ++i)
    engine.GetVNUI().textSized(r, lines[i], x, 288 + float(i) * layoutStep_,
                               596 - x, layoutFont_,
                               engine.GetSettings().vnTextColorOverride
                                   ? engine.GetSettings().vnTextColor
                                   : SDL_Color{240, 240, 245, 255});
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
    Kotonoha::RenderUpscaledTexture(&r, &tex, nullptr, &rect);
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
    Kotonoha::RenderUpscaledTexture(&r, &tex, nullptr, &b);
    SDL_SetTextureColorMod(&tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(&tex, 255);
  }
  engine.GetVNUI().textSized(r, options[i].label, b.x + 4, b.y + 3, b.w - 8,
                             24);
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
    options.push_back({engine_->GetTranslation().text(
                           translationRoute(), session_->choices()[i].text),
                       int(i)});
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
  const auto text = engine_->GetTranslation().text(route, line.original);
  textBox_->setOriginalKey(line.original);
  textBox_->setDialogue(engine_->GetTranslation().text(route, line.speaker),
                        text.empty() || text == line.original
                            ? DisplayText(line.original)
                            : text);
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
  AddScene(textBox_.get(), Kotonoha::GameState::Gameplay);
  AddScene(choices_.get(), Kotonoha::GameState::Gameplay);
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
  presentationSeconds_ = 0;
  textBox_->setDialogue("", "");
  textBox_->hide();
  syncDialogue();
  SetState(Kotonoha::GameState::Gameplay);
}
void AmagamiRuntime::audio(const std::string &id, const std::string &channel,
                           bool loop) {
  try {
    auto clip = assets_->loadAudio(id);
    if (clip.data.empty())
      return;
    if (channel == "voice" && clip.spec.format == SDL_AUDIO_F32) {
      voiceEnvelope_.clear();
      const size_t chunk =
          std::max(1, clip.spec.freq / 60) * clip.spec.channels;
      for (size_t start = 0; start < clip.data.size() / sizeof(float);
           start += chunk) {
        const size_t end =
            std::min(start + chunk, clip.data.size() / sizeof(float));
        double power = 0;
        for (size_t i = start; i < end; ++i) {
          float sample;
          std::memcpy(&sample, clip.data.data() + i * sizeof(float),
                      sizeof(float));
          power += double(sample) * sample;
        }
        voiceEnvelope_.push_back(float(std::sqrt(power / (end - start))));
      }
    }
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
bool AmagamiRuntime::handleNewGameAction() {
  start("n0dea_ev01A");
  return true;
}
bool AmagamiRuntime::handleAdvanceAction() {
  if (!session_ || session_->ended() || session_->waiting() || paused_ ||
      backlog_ || !session_->choices().empty())
    throw std::logic_error("Amagami story advance is blocked");
  advance();
  return true;
}
bool AmagamiRuntime::advanceStoryTime(double seconds) {
  if (!session_ || paused_ || backlog_)
    throw std::logic_error("No active Amagami story to step");
  updatePresentation(seconds);
  frameClock_.reset();
  return true;
}
void AmagamiRuntime::advance() {
  if (!session_ || session_->ended() || session_->waiting())
    return;
  syncDialogue();
  if (session_->choices().empty() && textBox_ &&
      !textBox_->isDialogueComplete()) {
    textBox_->completeDialogue();
    session_->revealDialogue(SIZE_MAX);
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
  Kotonoha::RenderUpscaledTexture(r, &t, nullptr, &rect);
}
void AmagamiRuntime::actor(SDL_Renderer *r, const Visual &v) {
  const float zoom = 0.5f * v.scaleX * (1 + v.stretchX);
  const float zoomY = 0.5f * v.scaleY * (1 + v.stretchY);
  int eyeFrame = std::clamp(v.eye, 0, 2),
      mouthFrame = std::clamp(v.mouth, 0, 2);
  if (v.blink && v.eye == 0 && v.blinkFrame >= v.nextBlink) {
    const double frame = v.blinkFrame - v.nextBlink;
    eyeFrame = frame < 2 || frame >= 8 ? 1 : 2;
  }
  static const std::vector<std::string> actorCodes{
      "", "TUK", "RIH", "CAO", "SAE", "HAR", "AII", "MIY", "RIS", "MAY"};
  const size_t actorId = v.file.size() > 7 ? size_t(v.file[7] - '0') : 0;
  if (session_ && actorId < actorCodes.size() &&
      session_->dialogue().voiceCode == actorCodes[actorId] && !v.manualMouth &&
      voice_.Playing()) {
    const size_t frame = size_t(voice_.Position() * 60);
    if (frame < voiceEnvelope_.size())
      mouthFrame = voiceEnvelope_[frame] > 0.04f    ? 0
                   : voiceEnvelope_[frame] > 0.015f ? 1
                                                    : 2;
  }
  const auto origin = assets_->source()->actorOrigin(unsigned(actorId), v.pose);
  const float left =
      320 + v.x + v.motionX + v.nodX + origin.x * v.scaleX * (1 + v.stretchX);
  const float top =
      224 + v.y + v.motionY + v.nodY + origin.y * v.scaleY * (1 + v.stretchY);
  auto rectangle = [&](SDL_FRect part) {
    return SDL_FRect{left + part.x * zoom, top + part.y * zoomY, part.w * zoom,
                     part.h * zoomY};
  };
  const auto bounds = assets_->bodyBounds(v.file);
  auto &body = assets_->texture(*r, "actor-body/" + v.file);
  const auto destination = rectangle(
      {float(bounds.x), float(bounds.y), float(bounds.w), float(bounds.h)});
  SDL_SetTextureAlphaModFloat(&body, std::clamp(v.alpha, 0.0f, 1.0f));
  Kotonoha::RenderUpscaledTexture(r, &body, nullptr, &destination);
  SDL_SetTextureAlphaModFloat(&body, 1);
  SDL_FPoint mouthPoint{400, 425};
  if (!v.face.empty() && assets_->contains(v.face)) {
    const auto &parts = assets_->fragments(v.face);
    for (size_t i = 0; i < parts.size(); ++i) {
      const auto &part = parts[i];
      const auto group = part.group;
      if (group < 1 || group > 3)
        continue;
      const auto expression = size_t(
          std::clamp(group == 3 ? v.mouthExpression : v.expression, 0, 8));
      const auto selected = (group - 1) * (parts.size() / 3) + expression * 3 +
                            size_t(group == 3 ? mouthFrame : eyeFrame);
      if (i != selected)
        continue;
      if (group == 3)
        mouthPoint = {part.destination.x + part.destination.w / 2,
                      part.destination.y + part.destination.h / 2};
      const auto dst = rectangle(part.destination);
      auto &tile = assets_->texture(*r, "actor-face/" + v.face + "/" +
                                            std::to_string(i));
      SDL_SetTextureBlendMode(&tile, SDL_BLENDMODE_BLEND);
      SDL_SetTextureAlphaModFloat(&tile, std::clamp(v.alpha, 0.0f, 1.0f));
      Kotonoha::RenderUpscaledTexture(r, &tile, nullptr, &dst);
      SDL_SetTextureAlphaModFloat(&tile, 1);
    }
  }
  if (v.breathAge >= 0 && v.breathTime > 0) {
    const int dir = std::clamp(v.breathDirection, 0, 8);
    constexpr int dx[]{0, 1, -1}, dy[]{1, -1, 0};
    constexpr float drift[]{0.144f, 0.192f, 0.256f};
    constexpr float startSize[]{0.08f, 0.06f, 0.045f},
        endSize[]{0.256f, 0.192f, 0.144f};
    constexpr float fadeIn[]{2, 3, 5}, fadeOut[]{2, 3, 4}, fadeAt[]{2, 3, 5};
    const float age = float(v.breathAge / v.breathTime);
    const float progress = std::sin(std::min(1.0f, age / 9) * 1.57079632679f);
    for (size_t i = 0; i < 3; ++i) {
      const float opacity =
          v.alpha * v.breathDensity * std::clamp(age / fadeIn[i], 0.0f, 1.0f) *
          std::clamp(1 - (age - fadeAt[i]) / fadeOut[i], 0.0f, 1.0f);
      if (opacity <= 0)
        continue;
      const std::string id = "sysgraph/breath" + std::to_string(i + 1) + ".tm2";
      if (!assets_->contains(id))
        continue;
      auto &tex = assets_->texture(*r, id);
      const float size =
          128 * v.breathSize *
          (startSize[i] + (endSize[i] - startSize[i]) * progress);
      const float travel = drift[i] * v.breathSize * 32 * progress;
      const float x = left + (mouthPoint.x + dx[dir % 3] * travel) * zoom;
      const float y = top + (mouthPoint.y + dy[dir / 3] * travel) * zoomY;
      SDL_FRect puff{x - size * zoom / 2, y - size * zoomY / 2, size * zoom,
                     size * zoomY};
      SDL_SetTextureAlphaModFloat(&tex, opacity);
      Kotonoha::RenderUpscaledTexture(r, &tex, nullptr, &puff);
      SDL_SetTextureAlphaModFloat(&tex, 1);
    }
  }
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
      const auto prompt = engine_->GetTranslation().text(-1, "PRESS START");
      if (prompt != "PRESS START")
        ui.textSized(r, prompt, 196, 320, 248, 24,
                     {255, 255, 255, Uint8(alpha * 255)});
      else {
        auto &tex = assets_->texture(r, "sysgraph/txt_start.tm2");
        SDL_SetTextureAlphaModFloat(&tex, alpha);
        SDL_FRect dst{196, 320, 248, 32};
        SDL_RenderTexture(&r, &tex, nullptr, &dst);
        SDL_SetTextureAlphaModFloat(&tex, 1);
      }
    } else {
      picture(&r, "sysgraph/menu_main.tm2", {232, 245, 176, 56});
      for (size_t i = 0; i < 3; ++i) {
        SDL_FRect b{264, float(305 + i * 36), 112, 32};
        if (focus_ == i) {
          auto &tex = assets_->texture(r, "sysgraph/cursor_base.tm2");
          Kotonoha::RenderUpscaledTexture(&r, &tex, nullptr, &b);
        }
        const std::string original = i == 0   ? "はじめから"
                                     : i == 1 ? "続きから"
                                              : "終了";
        const auto label = engine_->GetTranslation().text(-1, original);
        if (label != original || i == 2)
          ui.textSized(r, label, b.x + 6, b.y + 3, 100, 20, {35, 75, 85, 255});
        else
          picture(&r, "sysgraph/menu_main" + std::to_string(i) + ".tm2", b);
      }
    }
  } else {
    for (const auto &[name, v] : session_->visuals()) {
      if (name == "ITEM_BASE" || name == "ITEM_ICON")
        continue;
      if (!v.visible || v.file.empty())
        continue;
      if (v.file.rfind("stand/b", 0) == 0) {
        auto live = v;
        if (auto transition = session_->actorTransitions().find(name);
            transition != session_->actorTransitions().end()) {
          const float remaining =
              float(transition->second.remaining / transition->second.frames);
          auto old = transition->second.previous;
          old.alpha *= remaining;
          actor(&r, old);
          live.alpha *= 1 - remaining;
        }
        actor(&r, live);
      } else {
        auto &tex = assets_->texture(r, v.file);
        // A quarter-turn exchanges the image axes before camera scaling.
        const bool quarterTurn =
            std::abs(std::sin(v.angle * 3.14159265358979323846)) > 0.999;
        const float width = 640 * (quarterTurn ? v.scaleY : v.scaleX);
        const float height = 448 * (quarterTurn ? v.scaleX : v.scaleY);
        SDL_FRect dst{320 + v.x - width / 2, 224 + v.y - height / 2, width,
                      height};
        SDL_SetTextureAlphaModFloat(&tex, std::clamp(v.alpha, 0.0f, 1.0f));
        if (!Kotonoha::RenderUpscaledTextureRotated(&r, &tex, nullptr, &dst,
                                                    -v.angle * 180.0, nullptr,
                                                    SDL_FLIP_NONE))
          throw std::runtime_error(SDL_GetError());
        SDL_SetTextureAlphaModFloat(&tex, 1);
      }
    }
    if (session_->stageFade() > 0) {
      Uint8 shade = session_->whiteFade() ? 255 : 0;
      ui.panel(r, {0, 0, 640, 448},
               {shade, shade, shade, Uint8(session_->stageFade() * 255)});
    }
    // ItemView is a foreground overlay, independent of the background camera.
    for (const auto &key : {"ITEM_BASE", "ITEM_ICON"}) {
      const auto it = session_->visuals().find(key);
      if (it == session_->visuals().end() || !it->second.visible)
        continue;
      const auto &v = it->second;
      auto &tex = assets_->texture(r, v.file);
      float width = 0, height = 0;
      SDL_GetTextureSize(&tex, &width, &height);
      SDL_FRect rect{320 - width * v.scaleX / 2, 224 - height * v.scaleY / 2,
                     width * v.scaleX, height * v.scaleY};
      SDL_SetTextureAlphaModFloat(&tex, std::clamp(v.alpha, 0.0f, 1.0f));
      Kotonoha::RenderUpscaledTexture(&r, &tex, nullptr, &rect);
      SDL_SetTextureAlphaModFloat(&tex, 1);
    }
    syncChoices();
    if (choices_->isActive())
      choices_->render(r, scale);
    else if (session_->messageVisible()) {
      picture(&r, "sysgraph/win0.tm2", {24, 266, 592, 160});
      syncDialogue();
      if (textBox_ && !session_->messageCleared()) {
        textBox_->show();
        textBox_->render(r, scale);
        if (textBox_->isDialogueComplete())
          picture(&r, "sysgraph/flower.tm2", {572, 394, 24, 24});
      }
    }
    if (session_->ended()) {
      ui.panel(r, {60, 130, 520, 120});
      ui.textSized(
          r, engine_->GetTranslation().text(-1, "このイベントは終了しました"),
          85, 150, 470, 25);
      ui.textSized(r, engine_->GetTranslation().text(-1, "Enter: タイトルへ"),
                   85, 195, 470, 20);
    }
    if (paused_) {
      ui.backdrop(r);
      const std::vector<std::string> menu = {"戻る", "セーブ", "ロード",
                                             "タイトルへ", "設定"};
      for (size_t i = 0; i < menu.size(); ++i) {
        SDL_FRect b{180, float(100 + i * 54), 280, 44};
        ui.panel(r, b,
                 i == focus_ ? SDL_Color{133, 58, 110, 255}
                             : SDL_Color{25, 30, 50, 255});
        ui.textSized(r, engine_->GetTranslation().text(-1, menu[i]), b.x + 15,
                     b.y + 8, b.w - 30, 24);
      }
    }
    if (backlog_) {
      ui.panel(r, {10, 10, 620, 420}, {10, 15, 30, 250});
      float y = 20;
      const auto &history = session_->history();
      size_t begin = history.size() > 4 ? history.size() - 4 : 0;
      for (size_t i = begin; i < history.size(); ++i) {
        ui.textSized(r,
                     engine_->GetTranslation().text(translationRoute(),
                                                    history[i].speaker),
                     25, y, 590, 16, {255, 170, 210, 255});
        y += 22;
        const auto translated = engine_->GetTranslation().text(
            translationRoute(), history[i].original);
        y +=
            ui.textSized(r,
                         translated.empty() || translated == history[i].original
                             ? history[i].text
                             : translated,
                         25, y, 590, 18) +
            12;
      }
    }
  }
  if (!notice_.empty())
    ui.textSized(r, engine_->GetTranslation().text(-1, notice_), 10, 5, 620, 14,
                 {255, 235, 150, 255});
  if (!error_.empty()) {
    ui.panel(r, {20, 30, 600, 180}, {60, 15, 25, 250});
    ui.textSized(
        r, engine_->GetTranslation().text(-1, "Execution stopped: ") + error_,
        35, 45, 570, 16);
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
    else if (i == 4)
      engine_->openSettings();
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
    size_t count = !session_ ? 3 : paused_ ? 5 : session_->choices().size();
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
void AmagamiRuntime::updatePresentation(double seconds) {
  if (session_ && !paused_ && !backlog_) {
    session_->tick(seconds);
    presentationSeconds_ += seconds;
  }
  syncDialogue();
  syncChoices();
}
void AmagamiRuntime::update() {
  if (GetState() == Kotonoha::GameState::Splash) {
    Runtime::update();
    return;
  }
  const double delta = frameClock_.seconds();
  frameClock_.reset();
  if (session_ && !paused_ && !backlog_ && !engine_->isAgentPaused()) {
    try {
      updatePresentation(delta);
    } catch (const std::exception &e) {
      error_ = e.what();
    }
  }
  syncDialogue();
  syncChoices();
  if (textBox_) {
    if (paused_ || backlog_ || !session_ || !session_->messageVisible() ||
        session_->messageCleared())
      textBox_->hide();
    else
      textBox_->show();
    Runtime::update();
    if (session_ && !paused_ && !backlog_ && session_->messageVisible())
      session_->revealDialogue(Kotonoha::VNTextBoxUtility::CountUtf8Chars(
          textBox_->getVisibleText()));
  }
  music_.SetPaused(paused_ || backlog_ || engine_->isAgentPaused());
  voice_.SetPaused(paused_ || backlog_ || engine_->isAgentPaused());
  if (session_ && !paused_ && voiced_ != session_->history().size()) {
    voiced_ = session_->history().size();
    const auto &line = session_->dialogue();
    if (line.voice >= 0 && !line.voiceCode.empty())
      audio("voice/" + line.voiceCode + "/" + std::to_string(line.voice),
            "voice");
  }
}
SDL_FPoint AmagamiRuntime::sceneSize(const Kotonoha::Scene &scene) const {
  if (dynamic_cast<const AmagamiTextBox *>(&scene) ||
      dynamic_cast<const AmagamiChoices *>(&scene))
    return {640, 448};
  return Runtime::sceneSize(scene);
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
std::vector<std::string> AmagamiRuntime::activeTranslationKeys() const {
  if (!session_)
    return {};
  std::vector<std::string> keys;
  if (!session_->choices().empty())
    for (const auto &choice : session_->choices())
      keys.push_back(choice.text);
  else if (session_->messageVisible() && !session_->messageCleared())
    keys.push_back(session_->dialogue().original);
  return keys;
}
int AmagamiRuntime::translationRoute() const {
  return session_ ? int(assets_->source()->scenarioIndex(session_->name()))
                  : -1;
}
Kotonoha::Translation::SourceTexts AmagamiRuntime::translationSources() const {
  Kotonoha::Translation::SourceTexts sources;
  sources[-1] = {"はじめから",
                 "続きから",
                 "終了",
                 "戻る",
                 "セーブ",
                 "ロード",
                 "タイトルへ",
                 "このイベントは終了しました",
                 "Enter: タイトルへ",
                 "Saved",
                 "Loaded",
                 "Execution stopped: ",
                 "PRESS START",
                 "メインメニュー",
                 "選択肢",
                 "設定",
                 "バックログ"};
  for (size_t i = 0; i < assets_->source()->classes().size(); ++i) {
    auto texts = assets_->source()->systemClass(i).texts();
    sources[-1].insert(sources[-1].end(), texts.begin(), texts.end());
  }
  for (size_t i = 0; i < assets_->source()->scenarios().size(); ++i)
    sources.emplace(int(i), assets_->source()->script(i).texts());
  return sources;
}
std::vector<int> AmagamiRuntime::translationRoutes() const {
  std::vector<int> out{-1};
  for (size_t i = 0; i < assets_->source()->scenarios().size(); ++i)
    out.push_back(int(i));
  return out;
}
std::vector<std::string> AmagamiRuntime::translationTexts(int route) const {
  return route < 0 ? translationSources().at(-1)
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
  backlog_ = false;
  error_.clear();
  focus_ = 0;
  voiced_ = 0;
  displayed_ = 0;
  presentationSeconds_ = 0;
  textBox_->setDialogue("", "");
  textBox_->hide();
  syncDialogue();
  syncChoices();
  if (session_->messageVisible() && !session_->messageCleared())
    textBox_->show();
}
} // namespace Amagami
