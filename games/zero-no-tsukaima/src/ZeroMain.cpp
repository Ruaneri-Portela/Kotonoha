// Zero no Tsukaima (PS2) scene scripts running on the Kotonoha engine.
// Usage: ZeroNoTsukaima --zero <disc.bin (MODE1/2048)> [--mcp [port]]
// Backgrounds, character sprites and event CGs are drawn from SCENEDAT; the boot
// logos, opening movie, title and mode menu are the original ones (Frontend).
#include "Frontend.hpp"
#include "MoviePlayer.hpp"

#include <ZeroNoTsukaima/Game.hpp>
#include <ZeroNoTsukaima/Source.hpp>
#include <ZeroNoTsukaima/Vag.hpp>

#include <Kotonoha/Main.hpp>
#include <Kotonoha/components/CanvasImage.hpp>
#include <Kotonoha/components/GameplayFlow.hpp>
#include <Kotonoha/components/PcmPlayer.hpp>
#include <Kotonoha/components/Sound.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <map>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <string>

using Kotonoha::global;
namespace {

constexpr int kW = 640, kH = 448;

std::string g_disc;

// Rates from the original executable / SOUND_ID.LST format headers:
// - voices: Talk() starts the VOICE_ID stream at 22050 Hz (SLPS_258.30 @ 0x102eac);
// - BGM (*.STV, section header ST44TL.PSH): stereo 44100 Hz (stream pitch 3763 = 44100 * 4096 / 48000);
// - SE (*.MNV, section headers MN2205.PSH / MN2205C.PSH): mono 22050 Hz.
// Override with ZERO_VOICE_RATE / ZERO_BGM_RATE / ZERO_SE_RATE.
int rateFromEnv(const char *name, int fallback) {
  const char *v = std::getenv(name);
  const int r = v ? std::atoi(v) : 0;
  return r >= 8000 && r <= 96000 ? r : fallback;
}
// Live-tunable: F2/F3 lower/raise the voice rate, F4/F5 the BGM rate (applied to what is playing too).
int kVoiceRate = rateFromEnv("ZERO_VOICE_RATE", 22050), kBgmRate = rateFromEnv("ZERO_BGM_RATE", 44100);
const int kSeRate = rateFromEnv("ZERO_SE_RATE", 22050);
constexpr int kRatePresets[] = {11025, 16000, 22050, 24000, 32000, 44100, 48000};

struct Picture {
  Kotonoha::CanvasImage img; // drawn through the engine upscaler
  int w = 0, h = 0;
};

struct State {
  ZeroNoTsukaima::SceneSource source;
  std::map<int, Picture> pictures;
  std::map<std::string, Picture> normals; // NORMAL (system UI) pictures by name
  int bgIndex = -1, cgIndex = -1;
  std::vector<std::pair<int, int>> sprites; // (SCENEDAT index, centre offset), back to front
  // Played on the engine's Voice / BGM / Se channels (Audio Mixer volumes apply).
  Kotonoha::PcmPlayer voice, bgm, se;
  std::string lastBgm;
  Kotonoha::GameplayFlow *gameplay = nullptr;
  TTF_Font *font = nullptr;                // NotoSansJP from the engine, for menu text
  std::map<int, TTF_Font *> sizedFonts;    // copies at a pixel size
  std::map<std::string, SDL_Texture *> texts;
  ZeroNoTsukaima::SystemData system;
  std::string systemPath;
};
State *g_state = nullptr;

// Decodes a SOUND/VOICE stream and starts it on one of the engine channels.
bool playStream(Kotonoha::PcmPlayer &out, const char *channel, const std::vector<uint8_t> &raw, int rate, bool loop,
                const char *what) {
  out.Stop();
  Kotonoha::PcmPlayer::Clip clip;
  if (loop) {
    // BGM is stereo: 4 KiB chunks alternate between the left and right channel.
    constexpr size_t kChunk = 0x1000;
    std::vector<uint8_t> ch[2];
    for (size_t o = 0; o < raw.size(); o += kChunk) {
      const size_t n = std::min(kChunk, raw.size() - o);
      ch[(o / kChunk) & 1].insert(ch[(o / kChunk) & 1].end(), raw.begin() + o, raw.begin() + o + n);
    }
    const auto l = ZeroNoTsukaima::DecodeVag(ch[0].data(), ch[0].size());
    const auto r = ZeroNoTsukaima::DecodeVag(ch[1].data(), ch[1].size());
    const size_t n = std::min(l.size(), r.size());
    clip.samples.resize(n * 2);
    for (size_t i = 0; i < n; ++i) {
      clip.samples[2 * i] = l[i];
      clip.samples[2 * i + 1] = r[i];
    }
    clip.channels = 2;
  } else {
    clip.samples = ZeroNoTsukaima::DecodeVag(raw.data(), raw.size());
  }
  if (clip.samples.size() < 256) return false;
  const size_t samples = clip.samples.size();
  clip.rate = rate;
  clip.loop = loop;
  if (!out.Play(static_cast<Kotonoha::Sound *>(global.sound), channel, std::move(clip))) return false;
  SDL_Log("[audio] %s %zu samples @%d Hz (%s channel)", what, samples, rate, channel);
  return true;
}

void stepRate(int &rate, Kotonoha::PcmPlayer &live, int dir, const char *what) {
  constexpr int n = int(sizeof(kRatePresets) / sizeof(kRatePresets[0]));
  int i = 0;
  while (i < n - 1 && kRatePresets[i] < rate) ++i;
  rate = kRatePresets[std::clamp(i + dir, 0, n - 1)];
  live.SetRate(rate);
  SDL_Log("[audio] %s rate -> %d Hz", what, rate);
}

void playVoice(int index) {
  g_state->voice.Stop();
  std::vector<uint8_t> raw;
  if (index > 0 && g_state->source.LoadVoice(size_t(index), &raw))
    playStream(g_state->voice, "Voice", raw, kVoiceRate, false, "voice");
}

// Music by SOUND_ID.LST name: "bgm04" -> SOUND\BGM04.STV, "BGMA01" -> SOUND\BGMA01.STV.
void playBgm(const std::string &name) {
  State &s = *g_state;
  if (name == s.lastBgm) return;
  s.lastBgm = name;
  s.bgm.Stop();
  std::vector<uint8_t> raw;
  const int index = s.source.SoundIndex(name);
  if (index >= 0 && s.source.LoadSound(size_t(index), &raw)) playStream(s.bgm, "BGM", raw, kBgmRate, true, "bgm");
}

void playSe(const std::string &name) {
  State &s = *g_state;
  std::vector<uint8_t> raw;
  const int index = s.source.SoundIndex(name);
  s.se.Stop();
  if (index >= 0 && s.source.LoadSound(size_t(index), &raw)) playStream(s.se, "Se", raw, kSeRate, false, "se");
}

// Option volumes (0..10) drive the engine Voice / Se / BGM channels.
void applyVolumes(State &s) {
  auto *sound = static_cast<Kotonoha::Sound *>(global.sound);
  if (!sound) return;
  const std::pair<const char *, int> levels[] = {
      {"Voice", s.system.voiceVol}, {"Se", s.system.seVol}, {"BGM", s.system.bgmVol}};
  for (const auto &[name, vol] : levels)
    if (auto *ch = sound->GetChannelByName(name)) ch->volume = float(std::clamp(vol, 0, 10)) / 10.0f;
}

std::string upper(std::string v) {
  for (char &c : v) c = char(std::toupper(static_cast<unsigned char>(c)));
  return v;
}

Kotonoha::CanvasImage texture(const ZeroNoTsukaima::Image &img) {
  SDL_Surface *surf = SDL_CreateSurfaceFrom(img.width, img.height, SDL_PIXELFORMAT_RGBA32,
                                            const_cast<uint8_t *>(img.rgba.data()), img.width * 4);
  if (!surf) return {};
  SDL_Texture *tex = SDL_CreateTextureFromSurface(global.render, surf);
  SDL_DestroySurface(surf);
  return Kotonoha::CanvasImage(tex);
}

Picture *picture(int index) {
  State &s = *g_state;
  if (index < 0) return nullptr;
  auto it = s.pictures.find(index);
  if (it == s.pictures.end()) {
    Picture p;
    ZeroNoTsukaima::Image img;
    if (s.source.LoadPicture(index, &img)) {
      p.img = texture(img);
      p.w = img.width;
      p.h = img.height;
    }
    it = s.pictures.emplace(index, std::move(p)).first;
  }
  return it->second.img ? &it->second : nullptr;
}

class Host : public ZeroNoTsukaima::FrontendHost {
public:
  explicit Host(State &s) : s_(s) {}
  Kotonoha::CanvasImage *NormalPicture(const std::string &name) override {
    auto it = s_.normals.find(name);
    if (it == s_.normals.end()) {
      struct Picture p;
      ZeroNoTsukaima::Image img;
      if (s_.source.LoadNormalPicture(s_.source.NormalIndex(name), &img)) p.img = texture(img);
      else SDL_Log("[zero] NORMAL picture %s not found", name.c_str());
      it = s_.normals.emplace(name, std::move(p)).first;
    }
    return it->second.img ? &it->second.img : nullptr;
  }
  Kotonoha::CanvasImage *ScenePicture(const std::string &name) override {
    struct Picture *p = picture(s_.source.ScenedatIndex(name));
    return p ? &p->img : nullptr;
  }
  Kotonoha::CanvasImage *Picture(const std::string &name) override {
    if (s_.source.NormalIndex(name) >= 0) return NormalPicture(upper(name));
    return ScenePicture(name);
  }
  SDL_FPoint DrawText(SDL_Renderer *r, const std::string &text, float x, float y, float size, SDL_Color color,
                      bool measureOnly) override {
    if (!s_.font || text.empty()) return {0, 0};
    // Rasterised at the output scale so the text stays sharp on the upscaled canvas.
    float sx = 1, sy = 1;
    SDL_GetRenderScale(r, &sx, &sy);
    const int px = std::max(1, int(size * std::max(sx, 1.0f) + 0.5f));
    TTF_Font *&font = s_.sizedFonts[px];
    if (!font) {
      font = TTF_CopyFont(s_.font);
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
    SDL_Texture *&tex = s_.texts[key];
    if (!tex) {
      SDL_Surface *surf = TTF_RenderText_Blended(font, text.c_str(), 0, {color.r, color.g, color.b, 255});
      if (!surf) return {0, 0};
      tex = SDL_CreateTextureFromSurface(r, surf);
      SDL_DestroySurface(surf);
      if (!tex) return {0, 0};
      SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    }
    float w = 0, h = 0;
    SDL_GetTextureSize(tex, &w, &h);
    SDL_SetTextureAlphaMod(tex, color.a);
    const SDL_FRect dst{x, y, w * k, h * k};
    SDL_RenderTexture(r, tex, nullptr, &dst);
    return {dst.w, dst.h};
  }
  ZeroNoTsukaima::SystemData &System() override { return s_.system; }
  void SystemChanged() override {
    applyVolumes(s_);
    if (!s_.systemPath.empty()) s_.system.Save(s_.systemPath);
  }
  bool HasScene(const std::string &name) override {
    const std::string want = upper(name) + ".KS";
    for (const auto &n : s_.source.Names())
      if (upper(n) == want) return true;
    return false;
  }
  int SystemVoice(const std::string &name) override {
    playVoice(s_.source.SystemVoice(name));
    return s_.voice.RemainingMs();
  }
  void StopVoice() override { s_.voice.Stop(); }
  void SystemSe(const std::string &name) override { playSe(name); }
  void Music(const std::string &name) override { playBgm(name); }
  void StopMusic() override {
    s_.bgm.Stop();
    s_.lastBgm.clear();
  }
  bool Movie(int number, std::vector<uint8_t> *out) override { return s_.source.LoadMovie(number, out); }
  SDL_Renderer *Renderer() override { return global.render; }
  // The opening theme belongs to the music, so the BGM slider controls it.
  Kotonoha::Sound::Channel *MovieAudio() override {
    auto *sound = static_cast<Kotonoha::Sound *>(global.sound);
    return sound ? sound->GetChannelByName("BGM") : nullptr;
  }

private:
  State &s_;
};

// Draws a picture with its top-left corner at (x, y), natural size.
void drawImage(SDL_Renderer *r, Picture *p, float x, float y, float alpha, const float *tint = nullptr) {
  if (!p) return;
  SDL_SetTextureAlphaModFloat(p->img.Source(), alpha);
  if (tint) SDL_SetTextureColorModFloat(p->img.Source(), tint[0], tint[1], tint[2]);
  const SDL_FRect dst{x, y, float(p->w), float(p->h)};
  p->img.Draw(r, nullptr, &dst);
  SDL_SetTextureAlphaModFloat(p->img.Source(), 1.0f);
  if (tint) SDL_SetTextureColorModFloat(p->img.Source(), 1, 1, 1);
}

void fill(SDL_Renderer *r, Uint8 v, float alpha) {
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r, v, v, v, Uint8(alpha * 255.0f + 0.5f));
  const SDL_FRect all{0, 0, kW, kH};
  SDL_RenderFillRect(r, &all);
}

class ZeroSession : public Kotonoha::GameSession {
public:
  explicit ZeroSession(State &s) : s_(s), host_(s), frontend_(host_) {}
  ~ZeroSession() override {
    if (snapshot_) SDL_DestroyTexture(snapshot_);
  }

  bool HasFrontend() const override { return true; }
  void EnterFrontend(bool boot) override {
    s_.voice.Stop();
    s_.se.Stop();
    movie_.Close();
    // sceneEndState: a finished replay goes back to its gallery, the story to the logo.
    if (game_ && game_->Finished() && !boot) {
      s_.bgm.Stop();
      s_.lastBgm.clear();
      frontend_.Return(replay_);
    } else {
      frontend_.Enter(boot);
    }
    replay_ = ZeroNoTsukaima::Frontend::Replay::None;
  }
  bool Ended() const override { return game_ && game_->Finished(); }
  void FrontendEvent(const SDL_Event &e) override { frontend_.Event(e); }
  std::optional<TitleAction> FrontendTick() override {
    switch (frontend_.Tick()) {
    case ZeroNoTsukaima::Frontend::Result::NewGame:
    case ZeroNoTsukaima::Frontend::Result::PlayScene: return TitleAction::NewGame;
    case ZeroNoTsukaima::Frontend::Result::Load: return TitleAction::Load;
    default: return std::nullopt;
    }
  }
  void DrawFrontend(SDL_Renderer *r) override { frontend_.Draw(r); }

  bool Start() override {
    replay_ = frontend_.ReplayKind();
    // ZERO_START=ST01_10.KS starts New Game at another script (development aid).
    const char *dev = std::getenv("ZERO_START");
    const std::string script = replay_ != ZeroNoTsukaima::Frontend::Replay::None ? frontend_.Scene() + ".KS"
                               : dev && *dev                                      ? std::string(dev)
                                                                                  : "START.KS";
    frontend_.Leave();
    movie_.Close();
    game_ = std::make_unique<ZeroNoTsukaima::Game>(
        s_.source.Names(), [this](size_t i, std::vector<uint8_t> *o) { return s_.source.LoadScene(i, o); });
    game_->SetCharacterTable(&s_.source.Catalog().Characters());
    game_->SetStageNames(s_.source.Catalog().StageNames());
    for (const auto &[name, value] : s_.system.flags) game_->SetFlag(name, value);
    if (!game_->Start(script)) return false;
    s_.lastBgm.clear();
    s_.voice.Stop();
    s_.bgm.Stop();
    s_.se.Stop();
    lastKey_.clear();
    lastTicks_ = 0;
    game_->Pump();
    Settle();
    return true;
  }

  void Step(int input) override {
    if (game_->Choosing()) {
      if (input < 0) return;
      game_->Choose(size_t(input));
    } else {
      game_->Click();
    }
    game_->Pump();
    Settle();
  }

  // Runs the game clock and ends the waits on sounds / movies the host plays.
  void Idle() override {
    const Uint64 now = SDL_GetTicks();
    const Uint64 delta = lastTicks_ ? now - lastTicks_ : 0;
    lastTicks_ = now;
    game_->Advance(int64_t(std::min<Uint64>(delta, 100)));
    for (int guard = 0; guard < 8; ++guard) {
      RunCues();
      const auto b = game_->Blocked();
      if (b == Block::Voice && !s_.voice.Playing()) game_->FinishWait();
      else if (b == Block::Se && !s_.se.Playing()) game_->FinishWait();
      else if (b == Block::Movie && movie_.Finished()) game_->FinishWait();
      else break;
    }
    if (!movie_.Finished()) {
      movie_.Update();
      if (movie_.Finished()) movie_.Close();
    }
  }

  // A click during a timed wait, a voice / SE wait or an in-game movie ends it;
  // inside beginskip..endskip it skips the whole section.
  bool Skip() override {
    const auto b = game_->Blocked();
    if (b != Block::Time && b != Block::Voice && b != Block::Se && b != Block::Movie) return false;
    if (b == Block::Movie) movie_.Close();
    game_->FinishWait(true);
    RunCues();
    return true;
  }

  bool Choosing() const override { return game_->Choosing(); }
  // The original select window (choices) and MoveState map replace the engine choice panel.
  bool OwnsChoices() const override { return true; }
  int ChoiceEvent(const SDL_Event &e) override { return game_->Moving() ? MoveEvent(e) : SelectEvent(e); }
  void DrawChoices(SDL_Renderer *r) override {
    if (game_->Moving()) DrawMove(r);
    else DrawSelect(r);
  }
  std::vector<std::string> Choices() const override { return game_->Choices(); }
  bool HasMessage() const override { return game_->MessageVisible(); }
  std::string Speaker() const override { return game_->Speaker(); }
  std::string Text() const override { return game_->Text(); }

  void SetReplaying(bool r) override { replaying_ = r; }

  void Sync() override {
    if (!replaying_) Remember();
    if (replaying_ || !game_->MessageVisible()) {
      if (!replaying_) playBgm(game_->Bgm());
      return;
    }
    const std::string key = game_->Speaker() + game_->Text();
    if (key != lastKey_) {
      lastKey_ = key;
      if (VoiceAllowed()) playVoice(game_->Voice());
      else s_.voice.Stop();
    }
    playBgm(game_->Bgm());
  }

  void Draw(SDL_Renderer *r) override {
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    DrawElements(r, game_->Scene());
    float progress = 1;
    if (const auto *t = game_->ActiveTransition(&progress)) {
      // Whole-screen crossfade: the scene captured when it began fades out.
      if (SDL_Texture *snap = Snapshot(r, *t)) {
        SDL_SetTextureAlphaModFloat(snap, 1.0f - progress);
        const SDL_FRect all{0, 0, kW, kH};
        SDL_RenderTexture(r, snap, nullptr, &all);
      }
    }
    if (!movie_.Finished()) {
      SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
      SDL_RenderClear(r);
      movie_.Draw(r, SDL_FRect{0, 0, kW, kH});
    }
  }

  // MAIN_MESSAGEBASE frame, name plate picture and page cursor (PR_00 msgwin:
  // frame at 8,276; name plate centred in 160 px at +64,+5; cursor at +550,+122).
  void DrawMessageWindow(SDL_Renderer *r) override {
    if (!movie_.Finished()) return;
    if (Kotonoha::CanvasImage *frame = host_.NormalPicture("main_messagebase")) {
      float w = 0, h = 0;
      SDL_GetTextureSize(frame->Source(), &w, &h);
      const SDL_FRect dst{kMsgX, kMsgY, w, h};
      frame->Draw(r, nullptr, &dst);
    }
    if (const std::string *plate = s_.source.Catalog().NameImage(game_->Speaker())) {
      if (Kotonoha::CanvasImage *name = host_.NormalPicture(*plate)) {
        float w = 0, h = 0;
        SDL_GetTextureSize(name->Source(), &w, &h);
        const SDL_FRect dst{kMsgX + 64 + (160 - w) / 2, kMsgY + 5, w, h};
        name->Draw(r, nullptr, &dst);
      }
    }
    if (game_->WaitingForClick() && (SDL_GetTicks() / 500) % 2 == 0)
      if (Kotonoha::CanvasImage *cursor = host_.NormalPicture("main_messagecursor")) {
        float w = 0, h = 0;
        SDL_GetTextureSize(cursor->Source(), &w, &h);
        const SDL_FRect dst{kMsgX + 550, kMsgY + 122, w, h};
        cursor->Draw(r, nullptr, &dst);
      }
  }

  static constexpr float kMsgX = 8, kMsgY = 276;

private:
  using Block = ZeroNoTsukaima::Game::Block;

  // While replaying, every timed / sound / movie wait ends at once and nothing plays.
  void Settle() {
    if (!replaying_) {
      RunCues();
      return;
    }
    for (int guard = 0; guard < 100000; ++guard) {
      game_->TakeCues();
      const auto b = game_->Blocked();
      if (b != Block::Time && b != Block::Voice && b != Block::Se && b != Block::Movie) break;
      game_->FinishWait();
    }
    game_->TakeCues();
  }

  void RunCues() {
    for (const auto &cue : game_->TakeCues()) {
      if (replaying_) continue;
      switch (cue.kind) {
      case ZeroNoTsukaima::Game::Cue::Se:
        if (cue.name.empty()) s_.se.Stop();
        else playSe(cue.name);
        break;
      case ZeroNoTsukaima::Game::Cue::Voice:
        if (VoiceAllowed()) playVoice(cue.number);
        break;
      case ZeroNoTsukaima::Game::Cue::Movie: {
        std::vector<uint8_t> pss;
        movie_.Close();
        if (s_.source.LoadMovie(cue.number, &pss)) {
          s_.voice.Stop();
          if (!movie_.Open(pss, global.render, host_.MovieAudio(), 1.0f)) SDL_Log("[zero] movie %d failed", cue.number);
        }
        break;
      }
      }
    }
  }

  // VoiceOptionState: a character's voice can be switched off (VOICES.NUT code).
  bool VoiceAllowed() const {
    return s_.system.VoiceOn(ZeroNoTsukaima::VoiceCode(game_->Speaker()));
  }

  // System save: sf.* flags set by the scripts and the event CGs shown (CG gallery).
  void Remember() {
    bool changed = false;
    for (const auto &[name, value] : game_->Flags()) {
      if (name.compare(0, 3, "sf.") != 0) continue;
      auto it = s_.system.flags.find(name);
      if (it == s_.system.flags.end() || it->second != value) {
        s_.system.flags[name] = value;
        changed = true;
      }
    }
    const std::string ev = upper(game_->Event());
    if (!ev.empty() && ev.compare(0, 2, "CG") == 0 && s_.system.seen.insert(ev).second) changed = true;
    if (changed && !s_.systemPath.empty()) s_.system.Save(s_.systemPath);
  }

  // SelectWindow (ENVINIT selectopt): main_select-off / -on buttons, 400x50, centred
  // horizontally; button i at top = 448 / (n + 1) * (i + 1) - 25; caption centred in black.
  // No initial focus: the first UP selects the last button, the first DOWN the first.
  static constexpr int kSelectW = 400, kSelectH = 50;

  SDL_FRect SelectRect(int i, int n) const {
    const int h = kH / (n + 1);
    return {float(kW / 2 - kSelectW / 2), float(h * (i + 1) - kSelectH / 2), float(kSelectW), float(kSelectH)};
  }

  int SelectEvent(const SDL_Event &e) {
    const int n = int(game_->Choices().size());
    if (n == 0) return -1;
    if (selectFocus_ >= n) selectFocus_ = -1;
    auto focus = [&](int i) {
      if (i == selectFocus_) return;
      selectFocus_ = i;
      host_.SystemSe("SYS_SE01");
    };
    auto hit = [&](float x, float y) {
      const SDL_FPoint pt{x, y};
      for (int i = 0; i < n; ++i) {
        const SDL_FRect b = SelectRect(i, n);
        if (SDL_PointInRectFloat(&pt, &b)) return i;
      }
      return -1;
    };
    if (e.type == SDL_EVENT_KEY_DOWN) {
      switch (e.key.key) {
      case SDLK_UP: focus(selectFocus_ < 0 ? n - 1 : (selectFocus_ + n - 1) % n); break;
      case SDLK_DOWN: focus(selectFocus_ < 0 ? 0 : (selectFocus_ + 1) % n); break;
      case SDLK_RETURN:
      case SDLK_KP_ENTER:
      case SDLK_SPACE:
      case SDLK_Z:
        if (selectFocus_ >= 0) return PickedSelect();
        break;
      default: break;
      }
    } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
      if (const int i = hit(e.motion.x, e.motion.y); i >= 0) focus(i);
    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
      if (const int i = hit(e.button.x, e.button.y); i >= 0) {
        selectFocus_ = i;
        return PickedSelect();
      }
    }
    return -1;
  }

  int PickedSelect() {
    host_.SystemSe("SYS_SE02");
    const int pick = selectFocus_;
    selectFocus_ = -1;
    return pick;
  }

  void DrawSelect(SDL_Renderer *r) {
    const auto choices = game_->Choices();
    const int n = int(choices.size());
    for (int i = 0; i < n; ++i) {
      const SDL_FRect b = SelectRect(i, n);
      DrawNormal(r, i == selectFocus_ ? "MAIN_SELECT-ON" : "MAIN_SELECT-OFF", b.x, b.y);
      const SDL_FPoint size = host_.DrawText(r, choices[i], 0, 0, 24, {0, 0, 0, 255}, true);
      host_.DrawText(r, choices[i], b.x + (b.w - size.x) / 2, b.y + (b.h - size.y) / 2, 24, {0, 0, 0, 255}, false);
    }
  }

  // MoveState (map): MOVE_MOVE_OFF / _ON buttons at (39, 70 + 60n), 247x48, with the
  // destination name picture centred on them; the focused button shows its
  // character pose centred at SCWIDTH / 2 + 132 over the moveinit background.
  static constexpr float kMoveX = 39, kMoveY = 70, kMoveStep = 60, kMoveW = 247, kMoveH = 48;

  int MoveHit(SDL_FPoint pt) const {
    const size_t n = game_->MoveDestinations().size();
    for (size_t i = 0; i < n; ++i) {
      const SDL_FRect b{kMoveX, kMoveY + kMoveStep * float(i), kMoveW, kMoveH};
      if (SDL_PointInRectFloat(&pt, &b)) return int(i);
    }
    return -1;
  }

  int MoveEvent(const SDL_Event &e) {
    const int n = int(game_->MoveDestinations().size());
    if (n == 0) return -1;
    moveFocus_ = std::clamp(moveFocus_, 0, n - 1);
    if (e.type == SDL_EVENT_KEY_DOWN) {
      switch (e.key.key) {
      case SDLK_UP: moveFocus_ = (moveFocus_ + n - 1) % n; host_.SystemSe("SYS_SE01"); break;
      case SDLK_DOWN: moveFocus_ = (moveFocus_ + 1) % n; host_.SystemSe("SYS_SE01"); break;
      case SDLK_RETURN:
      case SDLK_KP_ENTER:
      case SDLK_SPACE:
      case SDLK_Z: return Picked(moveFocus_);
      default: break;
      }
    } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
      const int hit = MoveHit({e.motion.x, e.motion.y});
      if (hit >= 0 && hit != moveFocus_) {
        moveFocus_ = hit;
        host_.SystemSe("SYS_SE01");
      }
    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
      const int hit = MoveHit({e.button.x, e.button.y});
      if (hit >= 0) return Picked(hit);
    }
    return -1;
  }

  int Picked(int index) {
    host_.SystemSe("SYS_SE02");
    moveFocus_ = 0;
    return index;
  }

  void DrawMove(SDL_Renderer *r) {
    const auto &dest = game_->MoveDestinations();
    const auto &catalog = s_.source.Catalog();
    const int back = catalog.ResolveStage(game_->MoveBack());
    if (struct Picture *p = back >= 0 ? picture(back) : Named(game_->MoveBack())) drawImage(r, p, 0, 0, 1);
    moveFocus_ = std::clamp(moveFocus_, 0, std::max(0, int(dest.size()) - 1));
    for (size_t i = 0; i < dest.size(); ++i) {
      const bool on = int(i) == moveFocus_;
      const float y = kMoveY + kMoveStep * float(i);
      if (on && !dest[i].pose.empty())
        if (struct Picture *pose = picture(catalog.ResolveSprite(dest[i].pose)))
          drawImage(r, pose, float(kW / 2 + 132 - pose->w / 2), 0, 1);
      DrawNormal(r, on ? "MOVE_MOVE_ON" : "MOVE_MOVE_OFF", kMoveX, y);
      char name[40];
      SDL_snprintf(name, sizeof name, "MOVETEXT_POINT%02d_%s", MovePosition(dest[i].text), on ? "ON" : "OFF");
      if (Kotonoha::CanvasImage *t = host_.NormalPicture(name)) {
        float w = 0, h = 0;
        SDL_GetTextureSize(t->Source(), &w, &h);
        const SDL_FRect dst{kMoveX + (kMoveW - w) / 2, y + (kMoveH - h) / 2 - 2, w, h};
        t->Draw(r, nullptr, &dst);
      }
    }
  }

  void DrawNormal(SDL_Renderer *r, const char *name, float x, float y) {
    if (Kotonoha::CanvasImage *img = host_.NormalPicture(name)) {
      float w = 0, h = 0;
      SDL_GetTextureSize(img->Source(), &w, &h);
      const SDL_FRect dst{x, y, w, h};
      img->Draw(r, nullptr, &dst);
    }
  }

  // ScenePlayer.movePositions (OVERRIDE.NUT).
  static int MovePosition(const std::string &text) {
    static const char *const kPlaces[] = {
        "廊下", "図書室", "教室", "食堂", "厨房", "ルイズの部屋", "タバサの部屋", "キュルケの部屋",
        "アンリエッタの部屋", "クリスの部屋", "シエスタの部屋", "広場", "シエスタの教室", "屋上", "校庭",
        "体育館", "駅前", "通学路", "商店街", "公園", "生徒会室", "学校前", "劇場前", "市場", "裏街道", "街道"};
    for (int i = 0; i < int(sizeof(kPlaces) / sizeof(kPlaces[0])); ++i)
      if (text == kPlaces[i]) return i;
    return 0;
  }

  Picture *Named(const std::string &name) {
    auto it = named_.find(name);
    if (it == named_.end()) it = named_.emplace(name, s_.source.ScenedatIndex(name)).first;
    return picture(it->second);
  }

  void DrawElements(SDL_Renderer *r, const std::vector<ZeroNoTsukaima::Game::Element> &scene) {
    using E = ZeroNoTsukaima::Game::Element;
    const auto &catalog = s_.source.Catalog();
    for (const E &e : scene) {
      std::string image = e.image;
      int index = -1;
      if (e.kind == E::Stage) {
        const std::string special = catalog.SpecialStage(e.image);
        if (special.empty()) index = catalog.ResolveStage(e.image);
        else image = special;
      } else if (e.kind == E::Event) {
        index = catalog.ResolveEvent(e.image);
      } else if (e.kind == E::Character) {
        index = catalog.ResolveSprite(e.image);
        if (Picture *p = picture(index))
          drawImage(r, p, float((kW - p->w) / 2) + e.x, float(kH - p->h) + e.y, e.alpha, e.tint);
        continue;
      }
      if (image == "black" || image == "white") {
        fill(r, image == "white" ? 255 : 0, e.alpha);
        continue;
      }
      Picture *p = index >= 0 ? picture(index) : Named(image);
      drawImage(r, p, e.x, e.y, e.alpha);
    }
  }

  SDL_Texture *Snapshot(SDL_Renderer *r, const ZeroNoTsukaima::Game::Transition &t) {
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(r, &w, &h);
    if (w <= 0 || h <= 0) return nullptr;
    if (snapshot_ && (snapW_ != w || snapH_ != h)) {
      SDL_DestroyTexture(snapshot_);
      snapshot_ = nullptr;
    }
    if (!snapshot_) {
      snapshot_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
      if (!snapshot_) return nullptr;
      SDL_SetTextureBlendMode(snapshot_, SDL_BLENDMODE_BLEND);
      snapW_ = w;
      snapH_ = h;
      snapSerial_ = 0;
    }
    if (snapSerial_ != t.serial) {
      SDL_Texture *target = SDL_GetRenderTarget(r);
      float sx = 1, sy = 1;
      SDL_GetRenderScale(r, &sx, &sy);
      SDL_SetRenderTarget(r, snapshot_);
      SDL_SetRenderScale(r, sx, sy);
      SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
      SDL_RenderClear(r);
      DrawElements(r, t.from);
      SDL_SetRenderTarget(r, target);
      SDL_SetRenderScale(r, sx, sy);
      snapSerial_ = t.serial;
    }
    return snapshot_;
  }

  State &s_;
  Host host_;
  ZeroNoTsukaima::Frontend frontend_;
  std::unique_ptr<ZeroNoTsukaima::Game> game_;
  ZeroNoTsukaima::MoviePlayer movie_;
  std::map<std::string, int> named_;
  std::string lastKey_;
  Uint64 lastTicks_ = 0;
  SDL_Texture *snapshot_ = nullptr;
  int snapW_ = 0, snapH_ = 0;
  uint32_t snapSerial_ = 0;
  bool replaying_ = false;
  int moveFocus_ = 0, selectFocus_ = -1;
  ZeroNoTsukaima::Frontend::Replay replay_ = ZeroNoTsukaima::Frontend::Replay::None;
};

} // namespace

SDL_AppResult LocalArgs(int argc, char *argv[]) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] && std::string(argv[i]) == "--zero") {
      g_disc = argv[i + 1];
      argv[i][0] = '!';
      argv[i + 1][0] = '!';
      break;
    }
  }
  if (g_disc.empty())
    if (const char *e = std::getenv("ZERO_DISC")) g_disc = e;
  return SDL_APP_CONTINUE;
}

SDL_AppResult LocalInit(void **, int, char *[]) {
  if (g_disc.empty()) {
    SDL_Log("Zero no Tsukaima: use --zero <disc.bin>");
    return SDL_APP_FAILURE;
  }
  // Game ports show only the game UI; F7 or KOTONOHA_DEBUG_UI=1 brings the debug panels back.
  global.config.enableDebugUi = std::getenv("KOTONOHA_DEBUG_UI") != nullptr;
  auto *s = new State();
  g_state = s;
  std::string error;
  if (!s->source.Open(g_disc, &error)) {
    SDL_Log("Zero no Tsukaima: %s", error.c_str());
    return SDL_APP_FAILURE;
  }
  if (char *pref = SDL_GetPrefPath("Kotonoha", "zero-no-tsukaima")) {
    s->systemPath = std::string(pref) + "system.txt";
    SDL_free(pref);
    s->system.Load(s->systemPath);
  }
  applyVolumes(*s);
  Kotonoha::GameplayFlow::Config cfg;
  cfg.id = "zero-no-tsukaima";
  cfg.title = "Zero no Tsukaima";
  cfg.width = kW;
  cfg.height = kH;
  // Original window (PR_00 msgwin / deffont): black 24 px text, 32 px lines, at the
  // frame origin plus margins l44 t52 r48; the frame and name plate are drawn by the session.
  cfg.textBox.frame = false;
  cfg.textBox.speakerPlate = false;
  cfg.textBox.padding = 0;
  cfg.textBox.box = {ZeroSession::kMsgX + 44, ZeroSession::kMsgY + 52, 624 - 44 - 48, 120};
  cfg.textBox.fontSize = 24.0f;
  cfg.textBox.lineHeight = 32.0f;
  cfg.textBox.body = {0, 0, 0, 255};
  s->gameplay = new Kotonoha::GameplayFlow(cfg);
  if (!s->gameplay->Attach(std::make_unique<ZeroSession>(*s))) {
    SDL_Log("Zero no Tsukaima: cannot start START.KS");
    return SDL_APP_FAILURE;
  }
  SDL_SetWindowTitle(global.window, "Zero no Tsukaima");
  return SDL_APP_CONTINUE;
}

SDL_AppResult LocalEvent(void *, SDL_Event *e) {
  if (g_state && e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
    switch (e->key.key) {
    case SDLK_F2: stepRate(kVoiceRate, g_state->voice, -1, "voice"); break;
    case SDLK_F3: stepRate(kVoiceRate, g_state->voice, +1, "voice"); break;
    case SDLK_F4: stepRate(kBgmRate, g_state->bgm, -1, "bgm"); break;
    case SDLK_F5: stepRate(kBgmRate, g_state->bgm, +1, "bgm"); break;
    default: break;
    }
  }
  if (g_state && g_state->gameplay) g_state->gameplay->HandleEvent(e, &global);
  return SDL_APP_CONTINUE;
}

SDL_AppResult LocalIterate(void *appstate) {
  if (!g_state) return SDL_APP_CONTINUE;
  Kotonoha::Kotonoha *app = Kotonoha::GetApp(appstate);
  TTF_Font *font = nullptr;
  if (app) {
    auto it = app->fonts.find("NotoSansJP-Regular");
    if (it != app->fonts.end()) font = it->second;
  }
  g_state->font = font;
  Kotonoha::MarkFrameHandled();
  return g_state->gameplay->RunFrame(app, &global, font);
}

void LocalQuit(void *, SDL_AppResult) {
  if (g_state) {
    g_state->voice.Stop();
    g_state->bgm.Stop();
    g_state->se.Stop();
    delete g_state->gameplay;
    for (auto &[key, tex] : g_state->texts) SDL_DestroyTexture(tex);
    for (auto &[size, font] : g_state->sizedFonts) TTF_CloseFont(font);
  }
  delete g_state;
  g_state = nullptr;
}
