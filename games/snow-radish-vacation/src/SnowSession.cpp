#include "SnowSession.hpp"

#include <Kotonoha/componentes/VNSave.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace SnowRadish {
namespace {

constexpr int kW = kGameWidth, kH = kGameHeight;
constexpr int kSampleRate = 22050;
constexpr int kWipeTick = 15; // the original 15 ms transition scheduler
constexpr uint64_t kLogoFrameMs = 0x30;
constexpr int kFadeFrames = 60;
constexpr int kMaxFlakes[2] = {20, 40}, kFlakeSpeed[2] = {2, 1};
constexpr int kWindowTint[3] = {0, 0, 150};
constexpr SDL_FRect kVoiceButton{760, 515, 20, 20}, kAutoButton{760, 544, 20, 20};
constexpr SDL_FRect kTextBox{56, 468, kW - 56 - 84, 132};
constexpr float kTextSize = 24, kLineHeight = 28;
constexpr float kItemH = 52, kItemGap = 16, kChoiceFont = 28;
constexpr const char *kSaveMagic = "SRV1";

// The engine UI font has no Japanese glyphs: use a system CJK font.
std::string FindFont() {
  for (const char *env : {"SNOW_FONT", "KOTONOHA_CJK_FONT"})
    if (const char *path = std::getenv(env); path && *path) return path;
  static const char *const kFonts[] = {
      "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
      "/System/Library/Fonts/Hiragino Sans GB.ttc",
      "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
      "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
      "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
      "C:/Windows/Fonts/YuGothM.ttc",
      "C:/Windows/Fonts/msgothic.ttc",
  };
  std::error_code ec;
  for (const char *path : kFonts)
    if (std::filesystem::exists(std::filesystem::u8path(path), ec)) return path;
  return {};
}

std::vector<uint8_t> ReadFile(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

size_t NextChar(const std::string &s, size_t i) {
  const unsigned char c = static_cast<unsigned char>(s[i]);
  const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
  return std::min(s.size(), i + n);
}

std::string Graphic(const std::string &file) { return "graphic/" + SnowAssets::Lower(file); }

} // namespace

bool SnowSession::AlphaHit::hit(float px, float py) const {
  const int ix = int(px) - x, iy = int(py) - y;
  return ix >= 0 && iy >= 0 && ix < w && iy < h && alpha[size_t(iy) * size_t(w) + size_t(ix)] != 0;
}

SnowSession::SnowSession(Kotonoha::Engine &engine, std::shared_ptr<SnowAssets> assets)
    : engine_(engine), assets_(std::move(assets)) {
  script_ = assets_->Script();
  systemPath_ = Kotonoha::preferredDocumentsDirectory("snow-radish-vacation") / "system.dat";
  cleared_ = ReadFile(systemPath_) == std::vector<uint8_t>{83, 82, 86, 1, 1};
  const std::string customFont = engine_.GetSettings().fontPath;
  const std::string fontPath = !customFont.empty() ? Kotonoha::FileResolver::font(customFont).string() : FindFont();
  if (!fontPath.empty()) font_ = TTF_OpenFont(fontPath.c_str(), kTextSize);
  if (!font_) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[snow] no CJK font found (set SNOW_FONT)");
}

SnowSession::~SnowSession() { Shutdown(); }

void SnowSession::Shutdown() {
  StopAll();
  for (SDL_Texture **t : {&from_, &wipeTexture_, &to_})
    if (*t) {
      SDL_DestroyTexture(*t);
      *t = nullptr;
    }
  for (auto &[key, tex] : texts_) SDL_DestroyTexture(tex);
  texts_.clear();
  for (auto &[size, font] : sizedFonts_) TTF_CloseFont(font);
  sizedFonts_.clear();
  if (font_) TTF_CloseFont(font_);
  font_ = nullptr;
  game_.reset();
  playing_ = false;
}

// ---------------------------------------------------------------- audio

bool SnowSession::StartChannel(Kotonoha::PcmPlayer &player, const char *channel, std::vector<int16_t> first,
                               const std::vector<int16_t> &loop, int channels, bool looping) {
  player.Stop();
  if (first.empty() && loop.empty()) return false;
  Kotonoha::PcmPlayer::Clip clip;
  clip.loopStart = first.size() / size_t(channels);
  clip.samples = std::move(first);
  clip.samples.insert(clip.samples.end(), loop.begin(), loop.end());
  clip.channels = channels;
  clip.rate = kSampleRate;
  clip.loop = looping && !loop.empty();
  return player.Play(engine_.GetAudio().channel(channel), std::move(clip));
}

void SnowSession::StopAll() {
  bgm_.Stop();
  se_.Stop();
  voice_.Stop();
}

void SnowSession::PlaySound(const std::string &kind, const std::string &name, bool loop) {
  if (kind == "bgmstop") {
    lastBgm_.clear();
    bgm_.Stop();
    return;
  }
  if (kind == "sestop") return se_.Stop();
  if (kind == "voicestop") return voice_.Stop();
  if (kind == "bgm" && !resumeBgm_.empty()) {
    // The same BGM requested again after a load / title keeps playing.
    const bool same = name == resumeBgm_ && bgm_.Playing();
    resumeBgm_.clear();
    if (same) {
      lastBgm_ = name;
      lastBgmLoop_ = loop;
      return;
    }
  }
  if (!muted_) SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[sound] %s %s%s", kind.c_str(), name.c_str(), loop ? " (loop)" : "");
  if (kind == "bgm") {
    lastBgm_ = name;
    lastBgmLoop_ = loop;
    if (muted_) return;
    AdpsTrack track;
    if (assets_->Adps(name, track))
      StartChannel(bgm_, "bgm", std::move(track.intro.samples), track.loop.samples, 2, loop);
    return;
  }
  if (muted_) return;
  if (kind == "se") {
    AdpsTrack track;
    if (assets_->Adps(name, track)) StartChannel(se_, "se", std::move(track.intro.samples), track.loop.samples, 2, loop);
  } else if (kind == "voice") {
    Pcm pcm = assets_->Voice(name);
    if (pcm.samples.empty()) return; // ~2k script voices have no recording
    std::vector<int16_t> repeat;
    if (loop) repeat = pcm.samples;
    voiceSamples_ = pcm.samples;
    voiceStart_ = SDL_GetTicks();
    voiceActor_ = game_ ? game_->voiceActor() : 0;
    StartChannel(voice_, "voice", std::move(pcm.samples), repeat, 1, loop);
  }
}

void SnowSession::SetMuted(bool muted) {
  muted_ = muted;
  if (muted) {
    StopAll();
  } else if (game_) {
    game_->setClearFlag(cleared_);
    if (!lastBgm_.empty()) PlaySound("bgm", lastBgm_, lastBgmLoop_);
  }
}

// system.dat {'S','R','V',1,flag}: the persistent "game cleared" flag. Replays never write it.
void SnowSession::PersistCleared(bool flag) {
  if (muted_ || cleared_ == flag || systemPath_.empty()) return;
  const char system[] = {83, 82, 86, 1, char(flag)};
  std::ofstream out(systemPath_, std::ios::binary | std::ios::trunc);
  out.write(system, sizeof system);
  out.flush();
  if (out) cleared_ = flag;
  else SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[system] cannot persist cleared flag: %s", systemPath_.string().c_str());
}

// ---------------------------------------------------------------- story

bool SnowSession::Validate() {
  muted_ = true;
  const bool ok = Start();
  muted_ = false;
  lastBgm_.clear();
  playing_ = false;
  return ok;
}

bool SnowSession::Start() {
  debugJumped_ = false;
  resumeBgm_ = muted_ || !bgm_.Playing() ? "" : lastBgm_;
  if (resumeBgm_.empty()) bgm_.Stop();
  se_.Stop();
  voice_.Stop();
  lastBgm_.clear();
  booting_ = true;
  ended_ = false;
  auto_ = false;
  fadeMs_ = 0;
  focus_ = mapHover_ = -1;
  inputs_.clear();
  game_ = std::make_unique<Game>(script_);
  game_->sound = [this](const std::string &kind, const std::string &name, bool loop) { PlaySound(kind, name, loop); };
  if (SDL_getenv("SNOW_TRACE"))
    game_->trace = [](const std::string &t) { SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[vm] %s", t.c_str()); };
  game_->voicePlaying = [this] { return voice_.Playing(); };
  game_->sePlaying = [this] { return se_.Playing(); };
  game_->transition = [this](int type, int pattern, int speed) {
    if (!booting_) BeginTransition(type, pattern, speed);
  };
  // Native 213: the first one is the boot title (shown by the front end), any
  // later one ends the story and returns to the title.
  game_->title = [this] {
    if (booting_) {
      booting_ = false;
    } else {
      ended_ = true;
      game_->vm().block();
    }
  };
  game_->cleared = [this](bool flag) { PersistCleared(flag); };
  game_->transitionPlaying = [this] { return Fading(); };
  if (!game_->init()) {
    game_.reset();
    return false;
  }
  game_->setClearFlag(cleared_);
  game_->pump();
  seenVersion_ = ~0u;
  playing_ = true;
  SyncScene();
  return true;
}

bool SnowSession::Ended() const { return ended_ || (game_ && game_->finished()); }

void SnowSession::DebugJump(const std::string &name) {
  if (!playing_ || !game_) throw std::logic_error("Start gameplay before jumping");
  if (!game_->debugJump(name)) throw std::runtime_error("Cannot load debug script: " + name);
  debugJumped_ = true;
  StopAll();
  lastBgm_.clear();
  ended_ = false;
  booting_ = false;
  auto_ = false;
  fadeMs_ = 0;
  focus_ = mapHover_ = -1;
  game_->pump();
  seenVersion_ = ~0u;
  SyncScene();
}

void SnowSession::Advance(int input) {
  if (!playing_ || !game_) return;
  Game &g = *game_;
  const int in = g.choosing() ? input : -1;
  if (g.choosing() && in < 0) return;
  if (in < 0 && Skip()) return;
  if (g.choosing()) {
    g.choose(size_t(in));
    focus_ = mapHover_ = -1;
  } else if (!g.click()) {
    return; // ignored clicks must not be recorded, the replay would turn them into page turns
  }
  inputs_.push_back(in);
  g.pump();
  SyncScene();
}

void SnowSession::Replay(const std::vector<int> &inputs) {
  SetMuted(true);
  const bool started = Start();
  if (started)
    for (int in : inputs) {
      Game &g = *game_;
      if (g.choosing()) {
        if (in < 0) continue;
        g.choose(size_t(in));
      } else {
        g.click();
      }
      g.pump();
      inputs_.push_back(in);
    }
  SetMuted(false);
  focus_ = mapHover_ = -1;
  if (started) SyncScene();
}

void SnowSession::Frame() {
  if (!playing_ || !game_) return;
  Game &g = *game_;
  if (!g.waitingForClick() && !g.choosing() && !g.finished()) g.pump();
  SyncScene();
}

bool SnowSession::Skip() {
  if (!game_ || !game_->waitingForAudio() || !game_->skipAudioWait()) return false;
  fadeMs_ = 0;
  return true;
}

bool SnowSession::CanSkip() const {
  return playing_ && game_ && !game_->choosing() && (game_->waitingForClick() || game_->waitingForAudio());
}

bool SnowSession::WaitingForClick() const { return game_ && game_->waitingForClick(); }
bool SnowSession::HasMessage() const { return game_ && game_->hasMessage(); }
std::string SnowSession::Speaker() const {
  if (!game_) return {};
  const Message &m = game_->message();
  return engine_.GetTranslation().text(m.nameRoute, m.name);
}

std::string SnowSession::Text() const {
  if (!game_) return {};
  const Message &m = game_->message();
  return engine_.GetTranslation().text(m.route, m.text);
}
bool SnowSession::Choosing() const { return game_ && game_->choosing(); }
SnowSession::ChoiceKind SnowSession::Kind() const { return game_ ? game_->choiceKind() : ChoiceKind::Text; }
std::vector<std::string> SnowSession::Choices() const {
  if (!game_) return {};
  std::vector<std::string> labels = game_->choices();
  const auto &routes = game_->choiceRoutes();
  for (size_t i = 0; i < labels.size() && i < routes.size(); ++i)
    labels[i] = engine_.GetTranslation().text(routes[i], labels[i]);
  return labels;
}

std::map<int, std::vector<std::string>> SnowSession::TranslationSources() const {
  Game catalog(script_); // a separate VM: listing loads every script object
  return catalog.texts();
}
int SnowSession::InitialFocus() const { return game_ ? game_->initialFocus() : -1; }

std::string SnowSession::SaveLabel() const {
  std::string text = Text();
  size_t end = 0;
  for (int n = 0; end < text.size() && n < 24; ++n) end = NextChar(text, end);
  return text.substr(0, end);
}

std::string SnowSession::Serialize() const {
  if (debugJumped_)
    throw std::logic_error("Cannot create a replay save after a debug jump; start or load a normal game first");
  std::ostringstream out;
  out << kSaveMagic << ' ' << inputs_.size();
  for (int v : inputs_) out << ' ' << v;
  return out.str();
}

bool SnowSession::Restore(const std::string &data) {
  std::istringstream in(data);
  std::string magic;
  size_t count = 0;
  if (!(in >> magic >> count) || magic != kSaveMagic) return false;
  std::vector<int> inputs;
  for (int v; inputs.size() < count && in >> v;) inputs.push_back(v);
  if (inputs.size() != count) return false;
  Replay(inputs);
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[load] %zu inputs", inputs.size());
  return playing_;
}

std::filesystem::path SnowSession::SaveDirectory() const {
  if (const char *dir = SDL_getenv("SNOW_SAVE_DIR"); dir && *dir) return dir;
  return systemPath_.empty() ? std::filesystem::path("saves") : systemPath_.parent_path() / "saves";
}

bool SnowSession::HasSaves() const {
  try {
    return !Kotonoha::VNSaveSlots(SaveDirectory().string()).list().empty();
  } catch (const std::exception &) {
    return false;
  }
}

// ---------------------------------------------------------------- drawing

// Pictures go through the engine upscaler at its factor (1.0 = off).
float SnowSession::UpscaleFactor() const {
  const auto &settings = engine_.GetUpscaler().getSettings();
  return settings.enabled() ? settings.factor : 1.0f;
}

SDL_Texture *SnowSession::Texture(SDL_Renderer *r, const std::string &id, float &w, float &h) {
  if (missing_.count(id)) return nullptr;
  try {
    const float scale = UpscaleFactor();
    SDL_Texture &t = scale > 1 ? assets_->texture(*r, id, scale, &engine_.GetUpscaler()) : assets_->texture(*r, id);
    SDL_GetTextureSize(&t, &w, &h);
    w = std::round(w / scale);
    h = std::round(h / scale);
    SDL_SetTextureBlendMode(&t, SDL_BLENDMODE_BLEND);
    return &t;
  } catch (const std::exception &e) {
    missing_.insert(id);
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[gfx] %s", e.what());
    return nullptr;
  }
}

void SnowSession::DrawTexture(SDL_Renderer *r, SDL_Texture *t, float x, float y, float w, float h, const int *tint) {
  if (!t) return;
  const SDL_FRect dst{x, y, w, h};
  if (!tint) {
    SDL_RenderTexture(r, t, nullptr, &dst);
    return;
  }
  // 0x100 is neutral; channels above it add the excess on a second pass.
  auto mod = [](int v) { return Uint8(std::clamp(v, 0, 255)); };
  SDL_SetTextureColorMod(t, mod(tint[0]), mod(tint[1]), mod(tint[2]));
  SDL_RenderTexture(r, t, nullptr, &dst);
  if (tint[0] > 0x100 || tint[1] > 0x100 || tint[2] > 0x100) {
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
    SDL_SetTextureColorMod(t, mod(tint[0] - 0x100), mod(tint[1] - 0x100), mod(tint[2] - 0x100));
    SDL_RenderTexture(r, t, nullptr, &dst);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  }
  SDL_SetTextureColorMod(t, 255, 255, 255);
}

void SnowSession::DrawPicture(SDL_Renderer *r, const std::string &file, float x, float y, float alpha) {
  float w = 0, h = 0;
  SDL_Texture *t = Texture(r, Graphic(file), w, h);
  if (!t || alpha <= 0) return;
  SDL_SetTextureAlphaModFloat(t, std::min(alpha, 1.0f));
  DrawTexture(r, t, x, y, w, h, nullptr);
  SDL_SetTextureAlphaModFloat(t, 1.0f);
}

void SnowSession::DrawSprite(SDL_Renderer *r, const std::string &name, float dx, float dy, const int *tint) {
  std::string file = name;
  SnowAssets::Placement at = assets_->Sprite(file);
  if (!at.valid) {
    if (missing_.insert("sprite:" + name).second)
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[gfx] missing sprite %s", name.c_str());
    file = "dummy00";
    at = assets_->Sprite(file);
    if (!at.valid) return;
  }
  float w = 0, h = 0;
  DrawTexture(r, Texture(r, Graphic(file + ".chd"), w, h), at.x + dx, at.y + dy, at.w, at.h, tint);
}

void SnowSession::DrawFrame(SDL_Renderer *r, const std::string &animation, size_t frame, float x, float y,
                            const int *tint) {
  const auto &info = assets_->AnimationOf(animation);
  if (frame >= info.frames.size()) return;
  const auto &at = info.frames[frame];
  float w = 0, h = 0;
  SDL_Texture *t = Texture(r, Graphic(animation + ".anm#" + std::to_string(frame)), w, h);
  DrawTexture(r, t, at.x + x, at.y + y, at.w, at.h, tint);
}

void SnowSession::SyncScene() {
  if (!game_ || game_->sceneVersion() == seenVersion_) return;
  Game &g = *game_;
  seenVersion_ = g.sceneVersion();
  filled_ = g.filled();
  fillColor_ = g.fillColor();
  background_ = g.background();
  sprites_.clear();
  for (const auto &sp : g.sprites()) {
    Shown v;
    v.name = sp.name;
    v.id = sp.id;
    v.dx = sp.x;
    v.dy = sp.y;
    std::copy(std::begin(sp.tint), std::end(sp.tint), v.tint);
    sprites_.push_back(v);
  }
}

void SnowSession::DrawStage(SDL_Renderer *r) {
  DrawScene(r);
  DrawWipe(r);
}

void SnowSession::DrawScene(SDL_Renderer *r) {
  if (game_ && game_->uniformChoosing()) {
    DrawUniformSelect(r);
    return;
  }
  if (filled_) {
    SDL_SetRenderDrawColor(r, Uint8(fillColor_ >> 16), Uint8(fillColor_ >> 8), Uint8(fillColor_), 255);
    const SDL_FRect all{0, 0, kW, kH};
    SDL_RenderFillRect(r, &all);
  }
  if (!background_.empty()) DrawPicture(r, background_ + ".evd", 0, 0);
  if (game_ && !filled_) DrawAnimation(r, background_, 0, 0, nullptr);
  for (const auto &sp : sprites_) {
    float dx = float(sp.dx);
    // Transition type 11 slides the main character in from the right.
    if (fadeType_ == 11 && Fading() && sp.id == 1) dx += 256.0f * (1.0f - WipeProgress());
    DrawSprite(r, sp.name, dx, float(sp.dy), sp.tint);
    DrawAnimation(r, sp.name, dx, float(sp.dy), sp.tint);
  }
}

// Lip sync: the voice amplitude sampled every 119 ms picks the mouth frame of
// the regions that belong to the speaking actor.
void SnowSession::DrawAnimation(SDL_Renderer *r, const std::string &name, float dx, float dy, const int *tint) {
  if (name.empty() || !voice_.Playing()) return;
  const auto &animation = assets_->AnimationOf(name);
  if (animation.cursor || animation.frames.empty()) return;
  static constexpr uint8_t quiet[] = {1, 1, 2, 0, 1, 1, 0, 2, 1, 1, 1, 0, 2, 1, 1, 0};
  static constexpr uint8_t loud[] = {2, 0, 2, 1, 2, 1, 0, 2, 0, 1, 2, 1, 2, 0, 2, 1};
  const uint64_t elapsed = SDL_GetTicks() - voiceStart_;
  const size_t tick = size_t(elapsed / 119);
  const size_t sample = size_t((tick * 119) * kSampleRate / 1000);
  const int amplitude = sample < voiceSamples_.size() ? std::abs(int(voiceSamples_[sample])) : 0;
  const int frame = amplitude <= 64 ? 0 : amplitude > 16377 ? loud[(tick + 1) & 15] : quiet[(tick + 1) & 15];
  for (const auto &region : animation.regions) {
    if (region.actor != uint32_t(voiceActor_)) continue;
    DrawFrame(r, name, region.first + size_t(frame), dx, dy, tint);
  }
}

// ---------------------------------------------------------------- wipes

void SnowSession::BeginTransition(int type, int pattern, int speed) {
  static constexpr int steps[] = {4, 8, 2, 16, 1};
  fadeType_ = type;
  fadePattern_ = pattern;
  if (speed == 5 || type == 4) {
    fadeMs_ = 0;
    keepFrom_ = false;
    return;
  }
  maskEdge_ = (pattern & 0xff00) == 0x100 ? 64 : (pattern & 0xff00) == 0x200 ? 32 : 128;
  const int range =
      type == 2 || type == 3 ? 256 + maskEdge_ : type == 5 ? (pattern == 6 || pattern == 7 ? 1400 : 800) : 256;
  fadeRange_ = range;
  fadeStep_ = steps[std::clamp(speed, 0, 4)];
  const int msec = ((range + fadeStep_ - 1) / fadeStep_) * kWipeTick;
  wipeMask_ = {};
  if (type == 2) {
    static const char *names[] = {"hart200", "maru200", "sima200", "star200", "suri200", "suri200r", "tako200"};
    const int index = pattern & 255;
    std::string name;
    if (index < 7) {
      name = names[index];
    } else if (index < 49) {
      char n[24];
      std::snprintf(n, sizeof n, "pm%02d200", index - 7);
      name = n;
    } else if (index < 51) {
      name = index == 49 ? "ap00200" : "ap01200";
    }
    if (!name.empty()) assets_->Alpha(name + ".alpf", false, wipeMask_);
  } else if (type == 3) {
    assets_->Alpha("asabase.alpf", false, wipeMask_);
    assets_->Alpha("asaflash.alpa", true, flashMask_);
  }

  fadeMs_ = 0;
  if (muted_) return;
  if (keepFrom_) keepFrom_ = false;
  else if (!Capture([this](SDL_Renderer *r) { DrawScene(r); })) return;
  if (!wipeMask_.alpha.empty()) {
    SDL_Renderer *r = &engine_.GetRenderer();
    SDL_Texture *old = SDL_GetRenderTarget(r);
    SDL_SetRenderTarget(r, from_);
    SDL_Surface *pixels = SDL_RenderReadPixels(r, nullptr);
    SDL_SetRenderTarget(r, old);
    SDL_Surface *rgba = pixels ? SDL_ConvertSurface(pixels, SDL_PIXELFORMAT_RGBA32) : nullptr;
    if (pixels) SDL_DestroySurface(pixels);
    fromPixels_.clear();
    if (rgba) {
      fromPixels_.resize(size_t(kW) * kH * 4);
      for (int y = 0; y < kH && y < rgba->h; ++y)
        std::copy_n(static_cast<uint8_t *>(rgba->pixels) + y * rgba->pitch, kW * 4, fromPixels_.data() + size_t(y) * kW * 4);
      SDL_DestroySurface(rgba);
    }
    if (!wipeTexture_) {
      wipeTexture_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, kW, kH);
      if (wipeTexture_) SDL_SetTextureBlendMode(wipeTexture_, SDL_BLENDMODE_BLEND);
    }
  }
  fadeStart_ = SDL_GetTicks();
  fadeMs_ = uint64_t(msec);
}

// Renders `draw` into the 800x600 "from" picture of the next wipe.
bool SnowSession::Capture(const std::function<void(SDL_Renderer *)> &draw) {
  SDL_Renderer *r = &engine_.GetRenderer();
  if (!from_) {
    from_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, kW, kH);
    if (!from_) return false;
    SDL_SetTextureBlendMode(from_, SDL_BLENDMODE_BLEND);
  }
  SDL_Texture *old = SDL_GetRenderTarget(r);
  SDL_SetRenderTarget(r, from_);
  SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
  SDL_RenderClear(r);
  draw(r);
  SDL_SetRenderTarget(r, old);
  return true;
}

bool SnowSession::Fading() const { return fadeMs_ && SDL_GetTicks() - fadeStart_ < fadeMs_; }

float SnowSession::WipeProgress() const {
  return std::min(float((SDL_GetTicks() - fadeStart_) / kWipeTick) * float(fadeStep_) / float(fadeRange_), 1.0f);
}

void SnowSession::DrawWipe(SDL_Renderer *r) {
  if (!Fading() || !from_) return;
  const float t = WipeProgress();
  if (!wipeMask_.alpha.empty() && fromPixels_.size() == size_t(kW) * kH * 4 && wipeTexture_) {
    // Alpha masks (200x150, bottom-up): pattern 0x400 mirrors, 0x800 inverts;
    // type 3 adds the scrolling ASAFLASH mask.
    const int phase = int(t * float(256 + maskEdge_)) - 256;
    for (int y = 0; y < kH; ++y)
      for (int x = 0; x < kW; ++x) {
        int mx = x / 4;
        const int my = (kH - 1 - y) / 4;
        if ((fadePattern_ & 0xff00) == 0x400) mx = 199 - mx;
        int alpha = wipeMask_.alpha[size_t(my) * 200 + size_t(mx)];
        if ((fadePattern_ & 0xff00) == 0x800) alpha = uint8_t(128 - alpha);
        if (fadeType_ == 3 && !flashMask_.alpha.empty()) {
          const size_t flash = size_t((phase + 256) / 8) * 249 + size_t(my) * 250 + size_t(mx) + 47;
          if (flash < flashMask_.alpha.size()) alpha = std::min(255, alpha + flashMask_.alpha[flash]);
        }
        const int reveal = std::clamp(alpha + phase, 0, maskEdge_) * 255 / maskEdge_;
        fromPixels_[(size_t(y) * kW + size_t(x)) * 4 + 3] = uint8_t(255 - reveal);
      }
    SDL_UpdateTexture(wipeTexture_, nullptr, fromPixels_.data(), kW * 4);
    const SDL_FRect all{0, 0, kW, kH};
    SDL_RenderTexture(r, wipeTexture_, nullptr, &all);
  } else if (fadeType_ == 5) {
    // Type 5: scrolls, partial reveals, subtractive shading, diagonal and interlaced wipes.
    if (!to_) to_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, kW, kH);
    if (!to_) return;
    if (toClock_ != fadeStart_) {
      SDL_Texture *target = SDL_GetRenderTarget(r);
      SDL_SetRenderTarget(r, to_);
      SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
      SDL_RenderClear(r);
      DrawScene(r);
      SDL_SetRenderTarget(r, target);
      toClock_ = fadeStart_;
    }
    SDL_SetTextureAlphaMod(from_, 255);
    const float progress = t * kW, remaining = kW - progress;
    auto part = [&](SDL_Texture *texture, float sourceX, float sourceW, float destX, float destW) {
      if (sourceW <= 0 || destW <= 0) return;
      const SDL_FRect source{sourceX, 0, sourceW, kH}, dest{destX, 0, destW, kH};
      SDL_RenderTexture(r, texture, &source, &dest);
    };
    auto row = [&](SDL_Texture *texture, float sx, float sw, float dx, float dw, int y) {
      if (sw <= 0 || dw <= 0) return;
      const SDL_FRect source{sx, float(y), sw, 1}, dest{dx, float(y), dw, 1};
      SDL_RenderTexture(r, texture, &source, &dest);
    };
    part(from_, 0, kW, 0, kW);
    switch (fadePattern_) {
    case 0:
      part(from_, progress, remaining, 0, remaining);
      part(to_, 0, progress, remaining, progress);
      break;
    case 1: part(from_, 0, kW, 0, kW); break;
    case 2:
      part(from_, 0, remaining, 0, remaining);
      part(to_, 0, progress, remaining, progress);
      break;
    case 3:
      part(to_, remaining, progress, 0, progress);
      part(from_, progress, remaining, progress, remaining);
      break;
    case 4: {
      part(from_, 0, kW, 0, remaining);
      part(to_, 0, kW, remaining, progress);
      SDL_BlendMode previous;
      SDL_GetRenderDrawBlendMode(r, &previous);
      const auto subtract =
          SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT,
                                     SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
      const bool supported = SDL_SetRenderDrawBlendMode(r, subtract);
      if (!supported) SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
      const int p = int(progress), rest = kW - p;
      for (int x = 0; x < kW; ++x) {
        int shade = 0;
        if (x < rest && rest) shade = ((799 - x * kW / rest) * (p * 256 / kW) / kW) / 2;
        else if (p) shade = ((x - rest) * kW / p * (rest * 256 / kW) / kW) / 2;
        if (supported) SDL_SetRenderDrawColor(r, Uint8(shade), Uint8(shade), Uint8(shade), 255);
        else SDL_SetRenderDrawColor(r, 0, 0, 0, Uint8(shade));
        const SDL_FRect column{float(x), 0, 1, kH};
        SDL_RenderFillRect(r, &column);
      }
      SDL_SetRenderDrawBlendMode(r, previous);
      break;
    }
    case 5:
      part(to_, 0, kW, 0, progress);
      part(from_, 0, kW, progress, remaining);
      break;
    case 6:
    case 7:
      for (int y = 0; y < kH; ++y) {
        const float width = std::clamp(t * 1400 - float(kH - 1 - y), 0.0f, float(kW));
        if (fadePattern_ == 6) row(to_, kW - width, width, 0, width, y);
        else row(to_, 0, width, kW - width, width, y);
      }
      break;
    case 8:
    case 9:
      for (int y = 0; y < kH; ++y) {
        const bool left = ((kH - 1 - y) & 1) == 0;
        if (fadePattern_ == 8) {
          if (left) row(to_, remaining, progress, 0, progress, y);
          else row(to_, 0, progress, remaining, progress, y);
        } else if (left) {
          row(to_, 0, kW, 0, progress, y);
          row(from_, 0, kW, progress, remaining, y);
        } else {
          row(from_, 0, kW, 0, remaining, y);
          row(to_, 0, kW, remaining, progress, y);
        }
      }
      break;
    default: break;
    }
  } else {
    SDL_SetTextureAlphaModFloat(from_, 1.0f - t);
    const SDL_FRect all{0, 0, kW, kH};
    SDL_RenderTexture(r, from_, nullptr, &all);
  }
}

// ---------------------------------------------------------------- message window

SnowSession::WindowClick SnowSession::MessageWindowClick(float x, float y) {
  if (Choosing() || Fading()) return WindowClick::None;
  const SDL_FPoint p{x, y};
  if (SDL_PointInRectFloat(&p, &kAutoButton)) return WindowClick::ToggleAuto;
  if (SDL_PointInRectFloat(&p, &kVoiceButton)) {
    const std::string voice = game_ ? game_->message().voice : std::string();
    if (!voice.empty()) PlaySound("voice", voice, false);
    return WindowClick::Consumed;
  }
  return WindowClick::None;
}

// WAKUH tinted (0, 0, 150) with the BOT buttons; the voice / auto buttons light on hover.
void SnowSession::DrawMessageWindow(SDL_Renderer *r) {
  if (!HasMessage() || Choosing()) return;
  DrawSprite(r, "wakuh", 0, 0, kWindowTint);
  DrawSprite(r, "bot");
  auto lit = [&](const std::string &name, const SDL_FRect &at) {
    const auto p = assets_->Sprite(name);
    DrawSprite(r, name, at.x - p.x, at.y - p.y);
  };
  if (SDL_PointInRectFloat(&mouse_, &kVoiceButton)) lit("bsc", kVoiceButton);
  if (auto_ || SDL_PointInRectFloat(&mouse_, &kAutoButton)) lit(auto_ ? "auto_on" : "auto_off", kAutoButton);
}

void SnowSession::DrawMessageText(SDL_Renderer *r, const std::string &speaker, const std::string &text) {
  if (!speaker.empty()) {
    const SDL_FPoint size = DrawText(r, speaker, 0, 0, kTextSize, {}, true);
    DrawText(r, speaker, kTextBox.x, kTextBox.y - size.y, kTextSize, {255, 230, 140, 255});
  }
  if (text != wrappedKey_) {
    // Greedy per-character wrap (Japanese has no spaces), so a prefix wraps like the full page.
    wrappedKey_ = text;
    wrapped_.clear();
    std::string line;
    for (size_t i = 0; i < text.size();) {
      const size_t next = NextChar(text, i);
      const std::string ch = text.substr(i, next - i);
      i = next;
      if (ch == "\n") {
        wrapped_.push_back(std::move(line));
        line.clear();
        continue;
      }
      if (!line.empty() && DrawText(r, line + ch, 0, 0, kTextSize, {}, true).x > kTextBox.w) {
        wrapped_.push_back(std::move(line));
        line.clear();
      }
      line += ch;
    }
    if (!line.empty()) wrapped_.push_back(std::move(line));
  }
  float y = kTextBox.y;
  for (const auto &line : wrapped_) {
    if (y + kLineHeight > kTextBox.y + kTextBox.h + 4) break;
    DrawText(r, line, kTextBox.x, y, kTextSize, {255, 255, 255, 255});
    y += kLineHeight;
  }
}

// CURSOR01 (CURSOR02 in auto mode) at (715, 555), one frame every 170 ms.
void SnowSession::DrawMessageCursor(SDL_Renderer *r) {
  if (!HasMessage() || Choosing() || Fading()) return;
  const std::string name = auto_ ? "cursor02" : "cursor01";
  const auto &animation = assets_->AnimationOf(name);
  if (animation.frames.empty()) return;
  DrawFrame(r, name, size_t(SDL_GetTicks() / 170) % animation.frames.size(), 715, 555);
}

// ---------------------------------------------------------------- choices

// Text choices: 52 px items 16 px apart, width (widest + 0x57) & ~0x3f, centred.
SDL_FRect SnowSession::WindowItem(size_t index) {
  const auto items = Choices();
  float widest = 0;
  SDL_Renderer *r = &engine_.GetRenderer();
  for (const auto &t : items) widest = std::max(widest, DrawText(r, t, 0, 0, kChoiceFont, {}, true).x);
  const float w = float((int(widest) + 0x57) & ~0x3f);
  const float top = std::floor((kH - float(items.size()) * (kItemH + kItemGap)) / 2);
  return {std::floor((kW - w) / 2), top + float(index) * (kItemH + kItemGap), w, kItemH};
}

// SELH 8 px strips tinted (0, 149, 149) when focused, with a shadowed label.
void SnowSession::DrawWindowItem(SDL_Renderer *r, size_t index, bool focused) {
  const auto items = Choices();
  if (index >= items.size()) return;
  const SDL_FRect rect = WindowItem(index);
  const auto strip = assets_->Sprite("selh");
  float w = 0, h = 0;
  if (SDL_Texture *t = strip.valid ? Texture(r, Graphic("selh.chd"), w, h) : nullptr) {
    SDL_SetTextureColorMod(t, 0, focused ? 149 : 0, focused ? 149 : 0);
    for (float x = 0; x + 8 <= rect.w; x += 8) {
      const SDL_FRect dst{rect.x + x, rect.y + 7, strip.w, strip.h};
      SDL_RenderTexture(r, t, nullptr, &dst);
    }
    SDL_SetTextureColorMod(t, 255, 255, 255);
  }
  const SDL_FPoint size = DrawText(r, items[index], 0, 0, kChoiceFont, {}, true);
  const float x = std::floor(rect.x + (rect.w - size.x) / 2), y = std::floor(rect.y + 7 + (38 - size.y) / 2);
  DrawText(r, items[index], x + 1, y + 1, kChoiceFont, {0, 0, 0, 255});
  DrawText(r, items[index], x, y, kChoiceFont, {255, 255, 255, 255});
}

SDL_FRect SnowSession::Hotspot(size_t index) {
  char name[24];
  std::snprintf(name, sizeof name, "map%04d", game_ ? game_->destination(index) : 0);
  const auto at = assets_->Sprite(name);
  return {at.x, at.y, at.w, at.h};
}

void SnowSession::PickMap() {
  keepFrom_ = Capture([this](SDL_Renderer *r) {
    DrawScene(r);
    DrawMap(r);
  });
}

// The map: clock (am/pm, day, hh:mm from mapArgs[7]), the focused hotspot, the
// characters waiting at each destination with their affection hearts and a
// preview of the destination background.
void SnowSession::DrawMap(SDL_Renderer *r) {
  if (!game_) return;
  const auto &args = game_->mapArgs();
  const int minutes = args.size() > 7 ? args[7] : 720;
  const int hour = minutes / 60 % 24;
  auto sprite = [&](const std::string &name, float dx = 0, float dy = 0) { DrawSprite(r, name, dx, dy); };
  sprite(hour < 12 ? "map_am" : "map_pm");
  sprite("map_d" + std::to_string(std::clamp(minutes / 1440, 0, 9)), 454, 310);
  int h = hour % 12;
  if (!h) h = 12;
  if (h >= 10) sprite("map_t1", 635, 311);
  sprite("map_t" + std::to_string(h % 10), 658, 311);
  sprite("map_t" + std::to_string(minutes % 60 / 10), 707, 311);
  sprite("map_t" + std::to_string(minutes % 10), 730, 311);
  const size_t n = game_->choices().size();
  const int hover = focus_;
  const bool hovered = hover >= 0 && size_t(hover) < n;
  char name[24];
  if (hovered) {
    std::snprintf(name, sizeof name, "map%04d", game_->destination(size_t(hover)));
    sprite(name);
  }
  for (size_t i = 0; i < n; ++i) {
    const int dest = game_->destination(i);
    const auto rect = Hotspot(i);
    static const char *actors[] = {"meg", "alc", "asg", "tme", "htk"};
    static constexpr uint32_t affection[] = {0x312e, 0x3131, 0x312f, 0x3130, 0x3132};
    for (size_t a = 0; a < 5 && a < args.size(); ++a) {
      if (args[a] != dest) continue;
      const auto badge = std::string("map_") + actors[a] + (hover == int(i) ? "1" : "0");
      sprite(badge, rect.x - 6, rect.y - 51);
      if (hover == int(i)) {
        const int hearts = std::clamp(int32_t(game_->vm().var(affection[a])), 0, 19) / 4;
        if (hearts) sprite("map_h0" + std::to_string(hearts), rect.x + 69, rect.y + 56);
      }
    }
  }
  if (!hovered) return;
  const int dest = game_->destination(size_t(hover));
  static constexpr int bg[] = {0, 8, 7, 4, 3, 9, 6, 10, 5, 2, 1};
  if (dest < 0 || dest >= int(std::size(bg))) return;
  const int hhmm = hour * 100 + minutes % 60;
  char suffix = hour < 5 || hour >= 18 ? 'e' : hour >= 15 ? 'c' : 'b';
  const int day = minutes / 1440;
  if ((dest == 10 && suffix == 'e') || ((day == 1 || day == 2) && dest == 2 && hhmm > 129 && hhmm < 431) ||
      ((day == 1 ? hhmm > 329 : day == 2 && hhmm > 129) && dest == 5 && hhmm < 431))
    suffix = 'd';
  std::snprintf(name, sizeof name, "bg%02d%c.img", bg[dest], suffix);
  DrawPicture(r, name, 464, 364);
}

// Uniform select (native 220): hit test on the STSEL<n>A alpha.
int SnowSession::UniformHit(float x, float y) {
  if (uniformHit_.empty())
    for (int i = 1; i <= 3; ++i) {
      AlphaHit out;
      SnowRadish::Sprite sp;
      if (decodeChd(assets_->Graphic("stsel" + std::to_string(i) + "a.chd"), sp)) {
        out.x = sp.x;
        out.y = sp.y;
        out.w = int(sp.width);
        out.h = int(sp.height);
        out.alpha.resize(size_t(out.w) * size_t(out.h));
        for (size_t p = 0; p < out.alpha.size(); ++p) out.alpha[p] = sp.rgba[p * 4 + 3];
      }
      uniformHit_.push_back(std::move(out));
    }
  for (size_t i = 0; i < uniformHit_.size(); ++i)
    if (uniformHit_[i].hit(x, y)) return int(i);
  return -1;
}

void SnowSession::DrawUniformSelect(SDL_Renderer *r) {
  DrawPicture(r, "selbase.evd", 0, 0);
  if (focus_ >= 0 && focus_ <= 2) DrawSprite(r, "stsel" + std::to_string(focus_ + 1) + "a");
}

// ---------------------------------------------------------------- front end

void SnowSession::FillWhite(SDL_Renderer *r) {
  SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
  const SDL_FRect all{0, 0, kW, kH};
  SDL_RenderFillRect(r, &all);
}

void SnowSession::BeginPhase(Phase phase, int ms) {
  phase_ = phase;
  phaseStart_ = SDL_GetTicks();
  phaseMs_ = uint64_t(std::max(0, ms));
}

// Boot: the Overflow logo with falling snow; later visits cross-fade the
// last story picture into the title.
void SnowSession::EnterFrontend() {
  const bool boot = !booted_;
  booted_ = true;
  playing_ = false;
  voice_.Stop();
  se_.Stop();
  hover_ = -1;
  action_.reset();
  titleSaves_ = HasSaves();
  boot_ = boot;
  if (boot) {
    StartLogo();
    return;
  }
  Capture([this](SDL_Renderer *r) { DrawScene(r); });
  PlaySound("bgm", "sr001", true);
  BeginPhase(Phase::TitleIn, kWipeTick * 256 / 8);
}

void SnowSession::FrontendEvent(Kotonoha::EventRef e) {
  if (e.type == SDL_EVENT_MOUSE_MOTION) mouse_ = {e.motion.x, e.motion.y};
  const bool press = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat);
  switch (phase_) {
  case Phase::Logo:
    if (press) EndLogo();
    break;
  case Phase::White:
  case Phase::TitleIn:
    if (press) phaseMs_ = 0;
    break;
  case Phase::TitleWait: break;
  case Phase::Title: TitleEvent(e); break;
  }
}

std::optional<SnowSession::TitleAction> SnowSession::FrontendTick() {
  const uint64_t now = SDL_GetTicks();
  if (phase_ == Phase::Logo) {
    if (now - logoClock_ > 1000) logoClock_ = now - kLogoFrameMs;
    while (phase_ == Phase::Logo && now - logoClock_ >= kLogoFrameMs) {
      logoClock_ += kLogoFrameMs;
      LogoFrame();
    }
  } else if (phase_ != Phase::Title && now - phaseStart_ >= phaseMs_) {
    if (phase_ == Phase::White) {
      PlaySound("bgm", "sr001", true);
      Capture([](SDL_Renderer *r) { FillWhite(r); });
      BeginPhase(Phase::TitleIn, kWipeTick * 256 / 2);
    } else if (phase_ == Phase::TitleIn && boot_) {
      BeginPhase(Phase::TitleWait, 500);
    } else {
      BeginPhase(Phase::Title, 0);
    }
  }
  auto action = action_;
  action_.reset();
  return action;
}

void SnowSession::DrawFrontend(SDL_Renderer *r) {
  switch (phase_) {
  case Phase::Logo: DrawLogo(r); break;
  case Phase::White: FillWhite(r); break;
  case Phase::TitleIn: {
    DrawTitleScreen(r, -1);
    const float t = phaseMs_ ? float(SDL_GetTicks() - phaseStart_) / float(phaseMs_) : 1.0f;
    if (from_ && t < 1.0f) {
      SDL_SetTextureAlphaModFloat(from_, 1.0f - t);
      const SDL_FRect all{0, 0, kW, kH};
      SDL_RenderTexture(r, from_, nullptr, &all);
    }
    break;
  }
  case Phase::TitleWait: DrawTitleScreen(r, -1); break;
  case Phase::Title: DrawTitleScreen(r, hover_); break;
  }
}

void SnowSession::StartLogo() {
  LoadPaths();
  for (auto &group : flakes_)
    for (auto &flake : group) Spawn(flake);
  count_[0] = count_[1] = 1;
  frame_ = sec_ = fade_ = logoState_ = 0;
  voiced_ = false;
  logoClock_ = SDL_GetTicks();
  BeginPhase(Phase::Logo, 0);
}

void SnowSession::EndLogo() {
  voice_.Stop();
  BeginPhase(Phase::White, kWipeTick * 256 / 4);
}

// SLINE0n.SNW: u32 count at 8, then the x offset of every row of a flake path.
void SnowSession::LoadPaths() {
  if (!paths_[0].empty()) return;
  for (int i = 0; i < 3; ++i) {
    const auto d = assets_->Graphic("sline0" + std::to_string(i + 1) + ".snw");
    if (d.size() < 12) continue;
    auto u32 = [&](size_t o) {
      return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
    };
    const size_t n = std::min<size_t>(u32(8), (d.size() - 12) / 4);
    for (size_t k = 0; k < n; ++k) paths_[i].push_back(int32_t(u32(12 + k * 4)));
  }
}

void SnowSession::Spawn(Flake &f) {
  static const int kPath[10] = {0, 1, 2, 0, 1, 2, 0, 1, 2, 0};
  f.type = SDL_rand(2);
  f.path = &paths_[kPath[SDL_rand(10)]];
  f.x = SDL_rand(800) - 400;
  f.y = 0;
  f.alive = !f.path->empty();
}

// One 48 ms logo frame: fade in after 9 s (then the logo voice), out from 15 s, end at 20 s.
void SnowSession::LogoFrame() {
  if (sec_ > 9 && logoState_ == 0) fade_ = std::min(fade_ + 1, kFadeFrames);
  if (sec_ >= 15 && logoState_ == 1) fade_ = std::min(fade_ + 1, kFadeFrames);
  if (++frame_ > 29) {
    frame_ = 0;
    for (int g = 0; g < 2; ++g) count_[g] = std::min(count_[g] + 1, kMaxFlakes[g]);
    if (++sec_ == 20) return EndLogo();
  }
  for (int g = 0; g < 2; ++g)
    for (int i = 0; i < count_[g]; ++i) {
      Flake &f = flakes_[g][size_t(i)];
      f.y += kFlakeSpeed[g];
      if (!f.alive || f.y >= int(f.path->size())) Spawn(f);
    }
  if (fade_ >= kFadeFrames && logoState_ == 1) logoState_ = 2;
  if (fade_ >= kFadeFrames && logoState_ == 0) {
    logoState_ = 1;
    fade_ = 0;
    if (!voiced_) PlaySound("voice", "V0310000", false);
    voiced_ = true;
  }
}

void SnowSession::DrawLogo(SDL_Renderer *r) {
  const float t = float(fade_) / float(kFadeFrames);
  FillWhite(r);
  float alpha = 0.0f;
  if (logoState_ == 0 && sec_ >= 10) alpha = t;
  else if (logoState_ == 1) alpha = sec_ >= 15 ? 1.0f - t : 1.0f;
  DrawPicture(r, "logo00.evd", 0, 0, alpha);
  for (int g = 0; g < 2; ++g)
    for (int i = 0; i < count_[g]; ++i) {
      const Flake &f = flakes_[g][size_t(i)];
      if (!f.alive || f.y >= int(f.path->size())) continue;
      int x = f.x + (*f.path)[size_t(f.y)];
      int y = f.y - 10;
      if (f.type == 0) {
        x -= 4;
        y = f.y - 14;
      } else {
        x -= 5;
        y -= 5;
      }
      if (x >= kW || y >= kH) continue;
      DrawSprite(r, f.type == 0 ? "snow09" : "snow11", float(x), float(y));
    }
}

std::vector<SnowSession::TitleItem> SnowSession::Items() const {
  if (titleSaves_)
    return {{{0x144, 0x159, 0xa5, 0x31}, TitleAction::NewGame},
            {{0x144, 0x19f, 0xa5, 0x31}, TitleAction::Load},
            {{0x144, 0x1e4, 0xa5, 0x31}, TitleAction::Quit}};
  return {{{0x144, 0x170, 0xa5, 0x31}, TitleAction::NewGame}, {{0x144, 0x1b5, 0xa5, 0x31}, TitleAction::Quit}};
}

// T00<suffix>.IMG, with T0<n><suffix>.IMG over the focused item.
void SnowSession::DrawTitleScreen(SDL_Renderer *r, int hover) {
  const std::string suffix = titleSuffix(cleared_, titleSaves_);
  DrawPicture(r, "T00" + suffix + ".evd", 0, 0);
  const auto items = Items();
  if (hover < 0 || size_t(hover) >= items.size()) return;
  DrawPicture(r, "T0" + std::to_string(hover + 1) + suffix + ".img", items[size_t(hover)].rect.x,
              items[size_t(hover)].rect.y);
}

void SnowSession::TitleEvent(Kotonoha::EventRef e) {
  const auto items = Items();
  const int n = int(items.size());
  if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    const SDL_FPoint pt =
        e.type == SDL_EVENT_MOUSE_MOTION ? SDL_FPoint{e.motion.x, e.motion.y} : SDL_FPoint{e.button.x, e.button.y};
    hover_ = -1;
    for (int i = 0; i < n; ++i)
      if (SDL_PointInRectFloat(&pt, &items[size_t(i)].rect)) hover_ = i;
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT && hover_ >= 0)
      SelectTitle(items[size_t(hover_)].action);
  } else if (e.type == SDL_EVENT_KEY_DOWN) {
    if (e.key.key == SDLK_UP) hover_ = hover_ <= 0 ? n - 1 : hover_ - 1;
    else if (e.key.key == SDLK_DOWN) hover_ = hover_ < 0 || hover_ >= n - 1 ? 0 : hover_ + 1;
    else if ((e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER || e.key.key == SDLK_SPACE) && hover_ >= 0)
      SelectTitle(items[size_t(hover_)].action);
  }
}

void SnowSession::SelectTitle(TitleAction action) {
  if (action == TitleAction::NewGame) {
    // The title is the "from" picture of the first story wipe.
    const int h = hover_;
    keepFrom_ = Capture([this, h](SDL_Renderer *r) { DrawTitleScreen(r, h); });
  }
  action_ = action;
}

// ---------------------------------------------------------------- text

SDL_FPoint SnowSession::DrawText(SDL_Renderer *r, const std::string &text, float x, float y, float size,
                                 SDL_Color color, bool measureOnly) {
  if (!font_ || text.empty()) return {0, 0};
  // Rasterised at the output pixel scale so the text stays sharp.
  const int px = std::max(1, int(size * std::max(pixelScale_, 1.0f) + 0.5f));
  TTF_Font *&font = sizedFonts_[px];
  if (!font) {
    font = TTF_CopyFont(font_);
    if (!font) return {0, 0};
    TTF_SetFontSize(font, float(px));
  }
  const float k = size / float(px);
  if (measureOnly) {
    int w = 0, h = 0;
    TTF_GetStringSize(font, text.c_str(), 0, &w, &h);
    return {float(w) * k, float(h) * k};
  }
  const std::string key = std::to_string(px) + "|" + std::to_string(color.r) + "," + std::to_string(color.g) + "," +
                          std::to_string(color.b) + "|" + text;
  auto found = texts_.find(key);
  if (found == texts_.end()) {
    if (texts_.size() >= 1024) {
      for (auto &[unused, tex] : texts_) SDL_DestroyTexture(tex);
      texts_.clear();
    }
    SDL_Surface *surface = TTF_RenderText_Blended(font, text.c_str(), 0, {color.r, color.g, color.b, 255});
    if (!surface) return {0, 0};
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surface);
    SDL_DestroySurface(surface);
    if (!tex) return {0, 0};
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    found = texts_.emplace(key, tex).first;
  }
  SDL_Texture *tex = found->second;
  float w = 0, h = 0;
  SDL_GetTextureSize(tex, &w, &h);
  SDL_SetTextureAlphaMod(tex, color.a);
  const SDL_FRect dst{x, y, w * k, h * k};
  SDL_RenderTexture(r, tex, nullptr, &dst);
  return {dst.w, dst.h};
}

} // namespace SnowRadish
