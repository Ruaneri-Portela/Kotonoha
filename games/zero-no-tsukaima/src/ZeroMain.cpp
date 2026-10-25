// Zero no Tsukaima (PS2) scene scripts running on the Kotonoha engine.
// Usage: ZeroNoTsukaima --zero <disc.bin (MODE1/2048)>
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
#include <Kotonoha/components/SaveStore.hpp>
#include <Kotonoha/components/Sound.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

using Kotonoha::global;
namespace {

constexpr int kW = 640, kH = 448;
constexpr const char *kFlowId = "zero-no-tsukaima";
constexpr int kSaveSlots = 64; // SaveState: 16 pages x 4

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
  bool Slot(int slot, ZeroNoTsukaima::SlotInfo *info) override {
    std::string label;
    if (!Kotonoha::SaveStore(kFlowId, kSaveSlots).Read(slot, &label, nullptr)) return false;
    *info = ZeroNoTsukaima::SlotInfo::Parse(label);
    return true;
  }
  void PlayVoice(int voice) override { playVoice(voice); }
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
  explicit ZeroSession(State &s) : s_(s), host_(s), frontend_(host_) {
    if (!s_.systemPath.empty()) {
      readPath_ = s_.systemPath.substr(0, s_.systemPath.find_last_of("/\\") + 1) + "read.txt";
      std::ifstream in(readPath_);
      for (std::string line; std::getline(in, line);)
        if (!line.empty()) read_.insert(line);
    }
    rouletteEvent_ = SDL_RegisterEvents(1);
  }
  ~ZeroSession() override {
    if (snapshot_) SDL_DestroyTexture(snapshot_);
  }

  bool HasFrontend() const override { return true; }
  void EnterFrontend(bool boot) override {
    playing_ = false;
    roulette_ = {};
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
    case ZeroNoTsukaima::Frontend::Result::LoadSlot: LoadRequest(frontend_.Slot()); return std::nullopt;
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
    history_.clear();
    historyId_.clear();
    roulette_ = {};
    playMs_ = pendingPlayMs_;
    pendingPlayMs_ = 0;
    playing_ = true;
    game_->Pump();
    Settle();
    return true;
  }

  void Step(int input) override {
    if (game_->Choosing()) {
      if (input < 0) return;
      game_->Choose(size_t(input));
    } else {
      MarkRead(game_->PageId());
      game_->Click();
    }
    game_->Pump();
    Settle();
    RecordHistory();
  }

  // Runs the game clock and ends the waits on sounds / movies the host plays.
  void Idle() override {
    const Uint64 now = SDL_GetTicks();
    const Uint64 delta = lastTicks_ ? now - lastTicks_ : 0;
    lastTicks_ = now;
    // The in-game screens pause the story (and the play time).
    if (frontend_.InGame()) return;
    playMs_ += int64_t(delta);
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
  int ChoiceEvent(const SDL_Event &e) override {
    if (game_->Roulette()) return RouletteEvent(e);
    return game_->Moving() ? MoveEvent(e) : SelectEvent(e);
  }
  void DrawChoices(SDL_Renderer *r) override {
    if (game_->Roulette()) DrawRoulette(r);
    else if (game_->Moving()) DrawMove(r);
    else DrawSelect(r);
  }
  std::vector<std::string> Choices() const override { return game_->Choices(); }
  bool HasMessage() const override { return game_->MessageVisible(); }
  std::string Speaker() const override { return game_->Speaker(); }
  std::string Text() const override { return game_->Text(); }

  void SetReplaying(bool r) override { replaying_ = r; }

  void Sync() override {
    if (!replaying_) Remember();
    RecordHistory();
    // The command screen / roulette own the music while they show.
    const bool music = !replaying_ && !frontend_.InGame() && !roulette_.active;
    if (replaying_ || !game_->MessageVisible()) {
      if (music) playBgm(game_->Bgm());
      return;
    }
    const std::string key = game_->Speaker() + game_->Text();
    if (key != lastKey_) {
      lastKey_ = key;
      if (VoiceAllowed()) playVoice(game_->Voice());
      else s_.voice.Stop();
    }
    if (music) playBgm(game_->Bgm());
  }

  void Tick() override {
    if (!playing_ || !game_) return;
    // coffee tag: the command screen in coffee-break mode (NEXT goes on).
    if (!replaying_ && game_->Blocked() == Block::Coffee && !frontend_.InGame()) {
      s_.voice.Stop();
      frontend_.OpenCommand(true, int(game_->Flag("f.prm_tundere_eval")), MaxLike());
    }
    using R = ZeroNoTsukaima::Frontend::Result;
    switch (frontend_.Tick()) {
    case R::Save: request_ = {FlowRequest::Save, frontend_.Slot()}; break;
    case R::LoadSlot: LoadRequest(frontend_.Slot()); break;
    case R::Title: request_ = {FlowRequest::Title, 0}; break;
    case R::Next:
      s_.bgm.Stop();
      s_.lastBgm.clear();
      game_->FinishWait();
      RunCues();
      break;
    default: break;
    }
    TickRoulette();
  }

  // CommandState (Esc / right click) and HistoryState (wheel up / Page Up).
  bool OpenScreen(Screen screen) override {
    if (!game_ || frontend_.InGame()) return true;
    if (screen == Screen::History) {
      if (!history_.empty()) frontend_.OpenHistory({history_.begin(), history_.end()});
      return true;
    }
    if (roulette_.active) return true;
    frontend_.OpenCommand(false, int(game_->Flag("f.prm_tundere_eval")), MaxLike());
    return true;
  }
  bool ScreenActive() const override { return game_ && frontend_.InGame(); }
  void ScreenEvent(const SDL_Event &e) override { frontend_.Event(e); }
  void DrawScreen(SDL_Renderer *r) override { frontend_.Draw(r); }
  FlowRequest TakeRequest() override { return std::exchange(request_, FlowRequest{}); }
  std::string SaveLabel() const override {
    ZeroNoTsukaima::SlotInfo info;
    info.title = game_->StringVar("f.title");
    info.playMs = playMs_;
    info.maxLike = MaxLike();
    return info.Label();
  }

  // sys_0027: getCharSpeed = (10 - charSpeed) * 10 ms, getAutoWait = (10 - autoWait) * 150 ms.
  int TextDelayMs() const override { return (10 - std::clamp(s_.system.charSpeed, 0, 10)) * 10; }
  int AutoDelayMs() const override { return (10 - std::clamp(s_.system.autoWait, 0, 10)) * 150; }
  bool VoiceActive() const override { return s_.voice.Playing(); }
  void AutoChanged(bool on) override {
    if (on == auto_) return;
    auto_ = on;
    host_.SystemSe(on ? "SYS_SE02" : "SYS_SE03");
  }
  // skipMode 1 = すべて (everything), 0 = 既読 (pages already read).
  bool CanSkip() const override {
    return game_ && !frontend_.InGame() && (s_.system.skipMode == 1 || read_.count(game_->PageId()) != 0);
  }
  bool WaitingForClick() const override { return game_ && game_->WaitingForClick() && !frontend_.InGame(); }

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
      return;
    }
    if (const auto *m = game_->ActiveMeter()) DrawMeter(r, *m);
    if (auto_) DrawNormal(r, "MAIN_AUTOPLAY", 40, 45);
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
      if (b != Block::Time && b != Block::Voice && b != Block::Se && b != Block::Movie && b != Block::Coffee) break;
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

  // selopt (height=200 top=80 in every script) narrows the area the buttons spread over.
  SDL_FRect SelectRect(int i, int n) const {
    const SDL_Rect area = game_->SelectArea();
    const int h = area.h / (n + 1);
    return {float(kW / 2 - kSelectW / 2), float(area.y + h * (i + 1) - kSelectH / 2), float(kSelectW), float(kSelectH)};
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

  void DrawNormal(SDL_Renderer *r, const std::string &name, float x, float y, float alpha = 1.0f) {
    if (alpha <= 0.0f) return;
    if (Kotonoha::CanvasImage *img = host_.Picture(name)) {
      float w = 0, h = 0;
      SDL_GetTextureSize(img->Source(), &w, &h);
      const SDL_FRect dst{x, y, w, h};
      SDL_SetTextureAlphaModFloat(img->Source(), std::min(alpha, 1.0f));
      img->Draw(r, nullptr, &dst);
      SDL_SetTextureAlphaModFloat(img->Source(), 1.0f);
    }
  }

  // getMaxLike(f): first maximum of f.cp_ru, cp_cu, cp_ta, cp_ch, cp_si, cp_an, cp_ri.
  int MaxLike() const {
    static const char *const kLikes[] = {"f.cp_ru", "f.cp_cu", "f.cp_ta", "f.cp_ch", "f.cp_si", "f.cp_an", "f.cp_ri"};
    int best = 0;
    for (int i = 1; i < 7; ++i)
      if (game_->Flag(kLikes[i]) > game_->Flag(kLikes[best])) best = i;
    return best;
  }

  void LoadRequest(int slot) {
    ZeroNoTsukaima::SlotInfo info;
    if (!host_.Slot(slot, &info)) return;
    pendingPlayMs_ = info.playMs;
    request_ = {FlowRequest::Load, slot};
  }

  void MarkRead(const std::string &id) {
    if (id.empty() || !read_.insert(id).second || readPath_.empty()) return;
    std::ofstream(readPath_, std::ios::app) << id << '\n';
  }

  // HistoryState back log: one entry per page, speaker names through historyNameMap.
  void RecordHistory() {
    if (!game_ || !game_->MessageVisible()) return;
    const std::string id = game_->PageId();
    if (id.empty() || id == historyId_) return;
    historyId_ = id;
    static const std::map<std::string, std::string> kNames = {
        {"母ちゃん", "サイトの母"},
        {"アンリエッタ・ルイズ", "２人"},
        {"クリス・タバサ", "２人"},
        {"サイト・ギーシュ・モンモランシー", "２人"},
        {"サイト・クリス", "２人"},
        {"サイト・シエスタ", "２人"},
        {"サイト・リシュ・シエスタ", "３人"},
        {"サイト・ルイズ", "２人"},
        {"シエスタ・アンリエッタ", "２人"},
        {"タバサ・クリス", "２人"},
        {"ルイズ・キュルケ", "２人"},
        {"女子生徒Ａ", "女子高生"},
        {"女子生徒Ｂ", "女子高生"},
        {"女子生徒Ｃ", "女子高生"},
        {"女子生徒Ｄ", "女子高生"},
        {"男子生徒Ａ", "男子生徒"},
        {"男子生徒Ｂ", "男子生徒"},
        {"男子生徒Ｃ", "男子生徒"},
        {"男子生徒Ｄ", "男子生徒"},
        {"男子生徒Ｅ", "男子生徒"},
        {"部員Ａ", "部員"},
        {"部員Ｂ", "部員"},
        {"ミスコン司会", "司会"}};
    std::string name = game_->Speaker();
    if (auto it = kNames.find(name); it != kNames.end()) name = it->second;
    ZeroNoTsukaima::HistoryEntry entry{name, game_->Text(), game_->Voice()};
    // A page that continues the previous one (no er) replaces it.
    if (!history_.empty() && history_.back().name == entry.name &&
        entry.text.compare(0, history_.back().text.size(), history_.back().text) == 0)
      history_.back() = entry;
    else
      history_.push_back(entry);
    while (history_.size() > 200) history_.pop_front();
  }

  // LikeMeter (好感度) and TndlMeter (ツンデレパラメータ), OVERRIDE.NUT.
  void DrawMeter(SDL_Renderer *r, const ZeroNoTsukaima::Game::Meter &m) {
    const float t = std::clamp(float(game_->Clock() - m.start) / 1000.0f, 0.0f, 1.0f);
    if (m.kind == ZeroNoTsukaima::Game::Meter::Like) {
      static const std::map<std::string, std::string> kIcons = {
          {"RU", "RUIZE"}, {"CU", "KYURUKE"}, {"TA", "TABASA"}, {"SI", "SIESTA"},
          {"AN", "ANRIETTA"}, {"CH", "KURISH"}, {"RI", "RISH"}, {"YRI", "YRISH"}};
      auto it = kIcons.find(upper(m.name));
      if (it != kIcons.end()) DrawNormal(r, "MAIN_LOVE" + it->second, 242, 208);
      DrawNormal(r, m.delta >= 0 ? "MAIN_LOVEUP" : "MAIN_LOVEDOWN", 265, 122 + 20 * (1 - t), t);
      return;
    }
    DrawNormal(r, "MAIN_TUNDEREMETER", 371, 29);
    const float eval = float(m.from) + float(m.to - m.from) * t;
    DrawNormal(r, "MAIN_TUNDEREPOINT", 404 + eval * 176 / 255 - 7, 53);
    if (m.delta > 0) DrawNormal(r, "MAIN_DEREUP", 258, 121 + 20 * (1 - t), t);
    else if (m.delta < 0) DrawNormal(r, "MAIN_TUNUP", 260, 124 + 20 * (1 - t), t);
  }

  // RouletteState (tndlslot): two reels; CIRCLE stops the left, then the right one;
  // f.prm_tundere_boot = left * 3 + rpoint[right] (+18 on the tsun slot).
  struct RouletteAnim {
    bool active = false, slot03 = false, finished = false;
    int state = 0, left = 0, right = 0, pick = -1;
    double shiftl = 0, shiftr = 0;
    Uint64 start = 0, last = 0, doneAt = 0;
  };

  static float Decel(float t) { return 1 - (1 - t) * (1 - t); }

  void TickRoulette() {
    if (!game_->Roulette()) {
      roulette_ = {};
      return;
    }
    RouletteAnim &a = roulette_;
    const Uint64 now = SDL_GetTicks();
    if (!a.active) {
      a = {};
      a.active = true;
      a.start = a.last = now;
      playSe("SLOT00");
      playBgm("BGM23");
    }
    const double dt = double(now - a.last);
    a.last = now;
    if (a.state == 0 && now - a.start >= 1000) a.state = 1;
    if (a.state == 1 && now - a.start >= 2000) {
      a.state = 2;
      playSe("SLOT01");
    }
    constexpr double kSpeed = 5; // patterns per second
    if (a.state <= 2) a.shiftl += kSpeed * dt / 1000.0;
    if (a.state <= 3) a.shiftr += kSpeed * dt / 1000.0;
    if (a.state == 4) {
      if (!a.slot03 && now - a.doneAt >= 500) {
        a.slot03 = true;
        playSe("SLOT03");
        s_.bgm.Stop();
        s_.lastBgm.clear();
      }
      if (!a.finished && now - a.doneAt >= 2500) {
        a.finished = true;
        SDL_Event e{};
        e.type = rouletteEvent_;
        SDL_PushEvent(&e);
      }
    }
  }

  int RouletteEvent(const SDL_Event &e) {
    RouletteAnim &a = roulette_;
    if (e.type == rouletteEvent_) return a.finished ? a.pick : -1;
    const bool press = (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
                        (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER || e.key.key == SDLK_SPACE ||
                         e.key.key == SDLK_Z)) ||
                       (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT);
    if (!press || !a.active) return -1;
    if (a.state == 2) {
      playSe("SLOT02");
      a.state = 3;
      a.shiftl = std::ceil(a.shiftl);
    } else if (a.state == 3) {
      static constexpr int kPoint[] = {1, 3, 2, 3};
      playSe("SLOT02");
      a.state = 4;
      a.shiftr = std::ceil(a.shiftr);
      a.left = int(a.shiftl) % 6;
      a.right = int(a.shiftr) % 4;
      a.pick = a.left * 3 + kPoint[a.right] + (game_->RouletteTsun() ? 18 : 0);
      a.doneAt = SDL_GetTicks();
    }
    return -1;
  }

  void DrawRoulette(SDL_Renderer *r) {
    const RouletteAnim &a = roulette_;
    if (!a.active) return;
    const Uint64 now = SDL_GetTicks();
    const bool tun = game_->RouletteTsun();
    DrawNormal(r, "RULET_TUNDEREBASE02", 101, 135);
    // layout(): pattern i at y - wrap(i * 50 - (50 * shift) % len), len = 6 * 50.
    auto reel = [&](float x, double shift, auto name) {
      constexpr double kStep = 50, kLen = 6 * kStep;
      const double s0 = std::fmod(kStep * shift, kLen);
      for (int i = 0; i < 6; ++i) {
        double s = i * kStep - s0;
        if (s < -kLen / 2) s += kLen;
        else if (s > kLen / 2) s -= kLen;
        DrawNormal(r, name(i), x, float(230 - s));
      }
    };
    char buf[48];
    reel(150, a.shiftl, [&](int i) {
      SDL_snprintf(buf, sizeof buf, tun ? "RNAMES_TUNTUN%02d" : "RNAMES_DEREDERE%02d", i + 1);
      return std::string(buf);
    });
    reel(340, a.shiftr, [&](int i) { return std::string(i % 2 ? "RNAMES_TUNDERE2P" : "RNAMES_TUNDERE1P"); });
    DrawNormal(r, "RULET_TUNDEREBASE01", 0, 0);
    if (a.state >= 1) {
      // TriangleAction: opacity 0 -> 255 in 500 ms and back in 500 ms (solid once stopped).
      const Uint64 t = (now - a.start - 1000) % 1000;
      const float star = a.state == 4 ? 1.0f : t < 500 ? float(t) / 500.0f : 1.0f - float(t - 500) / 500.0f;
      DrawNormal(r, "RULET_STAR", 45, 136, star);
    }
    const float bt = Decel(std::min(1.0f, float(now - a.start) / 1000.0f));
    DrawNormal(r, "RULET_BUTTON", 81 + 640 * (1 - bt), 362);
    const float rt = a.state >= 1 ? Decel(std::min(1.0f, float(now - a.start - 1000) / 1000.0f)) : 0.0f;
    DrawNormal(r, "RULET_TUNDEREBAR", 91, 217 - 448 * (1 - rt));
    if (a.state == 4 && now - a.doneAt >= 500) {
      struct Tero {
        float x, y;
        const char *name;
      };
      static constexpr Tero kDere[] = {{144, 249, "RULET_TERO_DERE01"}, {108, 250, "RULET_TERO_DERE02"},
                                       {195, 249, "RULET_TERO_DERE03"}, {170, 247, "RULET_TERO_DERE04"},
                                       {167, 250, "RULET_TERO_DERE05"}, {172, 250, "RULET_TERO_DERE06"}};
      static constexpr Tero kTun[] = {{194, 249, "RULET_TERO_TUN01"}, {252, 249, "RULET_TERO_TUN02"},
                                      {252, 249, "RULET_TERO_TUN03"}, {167, 247, "RULET_TERO_TUN04"},
                                      {197, 248, "RULET_TERO_TUN05"}, {137, 249, "RULET_TERO_TUN06"}};
      static constexpr Tero kNum[] = {{112, 162, "RULET_TERO_HITORI"}, {112, 162, "RULET_TERO_HUTARI"},
                                      {112, 162, "RULET_TERO_HITORI"}, {112, 162, "RULET_TERO_HUTARI"}};
      const float drop = 448 * (1 - Decel(std::min(1.0f, float(now - a.doneAt - 500) / 1000.0f)));
      const Tero &l = (tun ? kTun : kDere)[a.left];
      const Tero &n = kNum[a.right];
      DrawNormal(r, l.name, l.x, l.y - drop);
      DrawNormal(r, n.name, n.x, n.y - drop);
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
  bool replaying_ = false, playing_ = false, auto_ = false;
  int moveFocus_ = 0, selectFocus_ = -1;
  FlowRequest request_;
  int64_t playMs_ = 0, pendingPlayMs_ = 0;
  std::deque<ZeroNoTsukaima::HistoryEntry> history_;
  std::string historyId_;
  std::set<std::string> read_;
  std::string readPath_;
  RouletteAnim roulette_;
  Uint32 rouletteEvent_ = 0;
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
  cfg.saveSlots = kSaveSlots;
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
