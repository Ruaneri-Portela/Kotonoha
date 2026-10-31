#include "ZeroSession.hpp"

#include <ZeroNoTsukaima/Vag.hpp>

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace ZeroNoTsukaima {
namespace {

constexpr int kW = kGameWidth, kH = kGameHeight;

// Rates from the original executable / SOUND_ID.LST format headers:
// - voices: Talk() starts the VOICE_ID stream at 22050 Hz (SLPS_258.30 @ 0x102eac);
// - BGM (*.STV, ST44TL.PSH): stereo 44100 Hz; SE (*.MNV, MN2205.PSH): mono 22050 Hz.
// Override with ZERO_VOICE_RATE / ZERO_BGM_RATE / ZERO_SE_RATE.
int RateFromEnv(const char *name, int fallback) {
  const char *v = std::getenv(name);
  const int r = v ? std::atoi(v) : 0;
  return r >= 8000 && r <= 96000 ? r : fallback;
}
constexpr int kRatePresets[] = {11025, 16000, 22050, 24000, 32000, 44100, 48000};

std::string Upper(std::string v) {
  for (char &c : v) c = char(std::toupper(static_cast<unsigned char>(c)));
  return v;
}

size_t Utf8Next(const std::string &s, size_t i) {
  if (i >= s.size()) return s.size();
  const auto c = static_cast<unsigned char>(s[i]);
  const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
  return std::min(s.size(), i + n);
}

// The engine UI font has no Japanese glyphs: use a system CJK font.
std::string FindFont() {
  if (const char *env = std::getenv("ZERO_FONT"); env && *env) return env;
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

void DrawPicture(SDL_Renderer *r, Picture *p, float x, float y, float alpha, const float *tint = nullptr) {
  if (!p || !*p) return;
  SDL_Texture *tex = p->Source();
  SDL_SetTextureAlphaModFloat(tex, std::clamp(alpha, 0.0f, 1.0f));
  if (tint) SDL_SetTextureColorModFloat(tex, tint[0], tint[1], tint[2]);
  const SDL_FRect dst{x, y, p->Width(), p->Height()};
  p->Draw(r, nullptr, &dst);
  SDL_SetTextureAlphaModFloat(tex, 1.0f);
  if (tint) SDL_SetTextureColorModFloat(tex, 1, 1, 1);
}

void Fill(SDL_Renderer *r, Uint8 v, float alpha) {
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r, v, v, v, Uint8(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f));
  const SDL_FRect all{0, 0, kW, kH};
  SDL_RenderFillRect(r, &all);
}

float Decel(float t) { return 1 - (1 - t) * (1 - t); }

} // namespace

ZeroSession::ZeroSession(Kotonoha::Engine &engine, std::shared_ptr<DiscAssetProvider> disc)
    : engine_(engine), disc_(std::move(disc)), frontend_(*this),
      voiceRate_(RateFromEnv("ZERO_VOICE_RATE", 22050)), bgmRate_(RateFromEnv("ZERO_BGM_RATE", 44100)),
      seRate_(RateFromEnv("ZERO_SE_RATE", 22050)) {
  if (!disc_) throw std::invalid_argument("Zero no Tsukaima needs its disc");
  {
    const auto documents = Kotonoha::preferredDocumentsDirectory("zero-no-tsukaima");
    systemPath_ = (documents / "system.txt").string();
    readPath_ = (documents / "read.txt").string();
    system_.Load(systemPath_);
    std::ifstream in(readPath_);
    for (std::string line; std::getline(in, line);)
      if (!line.empty()) read_.insert(line);
  }
  const std::string customFont = engine_.GetSettings().fontPath;
  const std::string fontPath = !customFont.empty() ? Kotonoha::FileResolver::font(customFont).string() : FindFont();
  if (!fontPath.empty()) font_ = TTF_OpenFont(fontPath.c_str(), 24.0f);
  if (font_) SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[zero] font %s", fontPath.c_str());
  else SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[zero] no Japanese font found (set ZERO_FONT)");
  ApplyVolumes();
}

ZeroSession::~ZeroSession() { Shutdown(); }

void ZeroSession::Shutdown() {
  voice_.Stop();
  bgm_.Stop();
  se_.Stop();
  movie_.reset();
  frontend_.Leave();
  pictures_.clear();
  for (auto &[key, tex] : texts_) SDL_DestroyTexture(tex);
  texts_.clear();
  for (auto &[size, font] : sizedFonts_) TTF_CloseFont(font);
  sizedFonts_.clear();
  if (font_) TTF_CloseFont(font_);
  font_ = nullptr;
  if (snapshot_) SDL_DestroyTexture(snapshot_);
  snapshot_ = nullptr;
}

// ---------------------------------------------------------------- assets / audio

bool ZeroSession::ReadAsset(const std::string &id, std::vector<uint8_t> *out) const {
  auto &assets = engine_.GetAssets();
  if (!assets.contains(id)) return false;
  try {
    *out = assets.read(id);
    return true;
  } catch (const std::exception &e) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[zero] %s", e.what());
    return false;
  }
}

// Pictures go through the engine upscaler at its factor (1.0 = off); a new
// factor reloads them.
ZeroNoTsukaima::Picture *ZeroSession::Texture(const std::string &id) {
  const auto &settings = engine_.GetUpscaler().getSettings();
  const float scale = settings.enabled() ? settings.factor : 1.0f;
  if (scale != picturesScale_) {
    pictures_.clear();
    picturesScale_ = scale;
  }
  auto it = pictures_.find(id);
  if (it == pictures_.end()) {
    ZeroNoTsukaima::Picture picture;
    auto &assets = engine_.GetAssets();
    if (assets.contains(id)) {
      try {
        // Cached (and owned) by the engine asset provider.
        if (scale > 1)
          picture.Reset(&assets.texture(engine_.GetRenderer(), id, scale, &engine_.GetUpscaler()), false, scale);
        else
          picture.Reset(&assets.texture(engine_.GetRenderer(), id), false);
      } catch (const std::exception &e) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[zero] %s", e.what());
      }
    }
    it = pictures_.emplace(id, std::move(picture)).first;
  }
  return it->second ? &it->second : nullptr;
}

ZeroNoTsukaima::Picture *ZeroSession::SceneIndexPicture(int index) {
  return index >= 0 ? Texture("scenedat/" + std::to_string(index)) : nullptr;
}

ZeroNoTsukaima::Picture *ZeroSession::NamedScene(const std::string &name) {
  return SceneIndexPicture(disc_->Source().ScenedatIndex(name));
}

ZeroNoTsukaima::Picture *ZeroSession::NormalPicture(const std::string &name) {
  ZeroNoTsukaima::Picture *p = Texture("normal/" + Upper(name));
  if (!p) SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "[zero] NORMAL picture %s not found", name.c_str());
  return p;
}

ZeroNoTsukaima::Picture *ZeroSession::ScenePicture(const std::string &name) { return NamedScene(name); }

ZeroNoTsukaima::Picture *ZeroSession::Picture(const std::string &name) {
  if (disc_->Source().NormalIndex(name) >= 0) return NormalPicture(name);
  return ScenePicture(name);
}

// Decodes a SOUND/VOICE stream and plays it on an engine audio channel.
bool ZeroSession::PlayStream(PcmPlayer &out, const char *channel, const std::vector<uint8_t> &raw, int rate,
                             bool loop) {
  out.Stop();
  PcmPlayer::Clip clip;
  if (loop) {
    // BGM is stereo: 4 KiB chunks alternate between the left and right channel.
    constexpr size_t kChunk = 0x1000;
    std::vector<uint8_t> ch[2];
    for (size_t o = 0; o < raw.size(); o += kChunk) {
      const size_t n = std::min(kChunk, raw.size() - o);
      auto &dst = ch[(o / kChunk) & 1];
      dst.insert(dst.end(), raw.begin() + std::ptrdiff_t(o), raw.begin() + std::ptrdiff_t(o + n));
    }
    const auto l = DecodeVag(ch[0].data(), ch[0].size());
    const auto r = DecodeVag(ch[1].data(), ch[1].size());
    const size_t n = std::min(l.size(), r.size());
    clip.samples.resize(n * 2);
    for (size_t i = 0; i < n; ++i) {
      clip.samples[2 * i] = l[i];
      clip.samples[2 * i + 1] = r[i];
    }
    clip.channels = 2;
  } else {
    clip.samples = DecodeVag(raw.data(), raw.size());
  }
  if (clip.samples.size() < 256) return false;
  const size_t samples = clip.samples.size();
  clip.rate = rate;
  clip.loop = loop;
  if (!out.Play(engine_.GetAudio().channel(channel), std::move(clip))) return false;
  SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "[audio] %zu samples @%d Hz on \"%s\"", samples, rate, channel);
  return true;
}

void ZeroSession::PlayVoiceIndex(int index) {
  voice_.Stop();
  std::vector<uint8_t> raw;
  if (index > 0 && ReadAsset("voice/" + std::to_string(index), &raw)) PlayStream(voice_, "voice", raw, voiceRate_, false);
}

// Music by SOUND_ID.LST name: "bgm04" -> SOUND\BGM04.STV, "BGMA01" -> SOUND\BGMA01.STV.
void ZeroSession::PlayBgm(const std::string &name) {
  if (name == lastBgm_) return;
  lastBgm_ = name;
  bgm_.Stop();
  std::vector<uint8_t> raw;
  if (!name.empty() && ReadAsset("sound/" + name, &raw)) PlayStream(bgm_, "bgm", raw, bgmRate_, true);
}

void ZeroSession::PlaySe(const std::string &name) {
  se_.Stop();
  std::vector<uint8_t> raw;
  if (!name.empty() && ReadAsset("sound/" + name, &raw)) PlayStream(se_, "se", raw, seRate_, false);
}

// Option volumes (0..10) drive the engine "voice" / "se" / "bgm" channels.
void ZeroSession::ApplyVolumes() {
  const std::pair<const char *, int> levels[] = {
      {"voice", system_.voiceVol}, {"se", system_.seVol}, {"bgm", system_.bgmVol}};
  for (const auto &[name, vol] : levels) engine_.GetAudio().channel(name)->setVolume(float(std::clamp(vol, 0, 10)) / 10.0f);
}

void ZeroSession::StepRate(bool bgm, int dir) {
  constexpr int n = int(sizeof(kRatePresets) / sizeof(kRatePresets[0]));
  int &rate = bgm ? bgmRate_ : voiceRate_;
  int i = 0;
  while (i < n - 1 && kRatePresets[i] < rate) ++i;
  rate = kRatePresets[std::clamp(i + dir, 0, n - 1)];
  (bgm ? bgm_ : voice_).SetRate(rate);
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[audio] %s rate -> %d Hz", bgm ? "bgm" : "voice", rate);
}

// ---------------------------------------------------------------- FrontendHost

SDL_FPoint ZeroSession::DrawText(SDL_Renderer *r, const std::string &text, float x, float y, float size,
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
    SDL_Surface *surf = TTF_RenderText_Blended(font, text.c_str(), 0, {color.r, color.g, color.b, 255});
    if (!surf) return {0, 0};
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_DestroySurface(surf);
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

void ZeroSession::SystemChanged() {
  ApplyVolumes();
  if (!systemPath_.empty()) system_.Save(systemPath_);
}

bool ZeroSession::HasScene(const std::string &name) {
  const std::string want = Upper(name) + ".KS";
  for (const auto &n : disc_->Source().Names())
    if (Upper(n) == want) return true;
  return false;
}

int ZeroSession::SystemVoice(const std::string &name) {
  PlayVoiceIndex(disc_->Source().SystemVoice(name));
  return voice_.RemainingMs();
}

void ZeroSession::StopMusic() {
  bgm_.Stop();
  lastBgm_.clear();
}

std::unique_ptr<Kotonoha::MediaPlayer> ZeroSession::OpenMovie(int number) {
  auto &assets = engine_.GetAssets();
  const std::string id = "movie/" + std::to_string(number);
  if (!assets.contains(id)) return nullptr;
  try {
    // FFmpeg demuxes the PSS video; the SShd soundtrack comes as movie/<n>.wav.
    auto player = std::make_unique<Kotonoha::MediaPlayer>(engine_.GetAudio());
    player->open(assets, id, false, "bgm");
    if (assets.contains(id + ".wav")) player->openSoundtrack(assets, id + ".wav");
    return player;
  } catch (const std::exception &error) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[zero] movie %d: %s", number, error.what());
    return nullptr;
  }
}

Kotonoha::Upscaling *ZeroSession::Upscaler() { return &engine_.GetUpscaler(); }

SDL_Renderer *ZeroSession::Renderer() { return &engine_.GetRenderer(); }

// The opening theme belongs to the music, so the "bgm" channel controls it.

std::string ZeroSession::SlotPath(int slot) {
  return (Kotonoha::preferredDocumentsDirectory("zero-no-tsukaima") /
          ("slot" + std::to_string(slot) + ".sav")).string();
}

bool ZeroSession::Slot(int slot, SlotInfo *info) {
  if (slot < 0 || slot >= kSaveSlots) return false;
  std::ifstream in(SlotPath(slot));
  std::string label;
  if (!in || !std::getline(in, label)) return false;
  *info = SlotInfo::Parse(label);
  return true;
}

void ZeroSession::PlayVoice(int voice) { PlayVoiceIndex(voice); }

// ---------------------------------------------------------------- front end

void ZeroSession::EnterFrontend() {
  const bool boot = !booted_;
  booted_ = true;
  playing_ = false;
  roulette_ = {};
  SetAuto(false);
  voice_.Stop();
  se_.Stop();
  movie_.reset();
  // sceneEndState: a finished replay goes back to its gallery, the story to the logo.
  if (game_ && game_->Finished() && !boot) {
    StopMusic();
    frontend_.Return(replay_);
  } else {
    frontend_.Enter(boot);
  }
  replay_ = Frontend::Replay::None;
}

ZeroSession::TitleAction ZeroSession::FrontendTick() {
  switch (frontend_.Tick()) {
  case Frontend::Result::NewGame:
  case Frontend::Result::PlayScene: return TitleAction::NewGame;
  case Frontend::Result::LoadSlot:
    LoadRequest(frontend_.Slot());
    return pendingLoad_ >= 0 ? TitleAction::Load : TitleAction::None;
  default: return TitleAction::None;
  }
}

// ---------------------------------------------------------------- story

bool ZeroSession::Start() {
  debugJumped_ = false;
  if (pendingLoad_ >= 0) return LoadSlot(std::exchange(pendingLoad_, -1));
  replay_ = frontend_.ReplayKind();
  // ZERO_START=ST01_10.KS starts New Game at another script (development aid).
  const char *dev = std::getenv("ZERO_START");
  const std::string script = replay_ != Frontend::Replay::None ? frontend_.Scene() + ".KS"
                             : dev && *dev                       ? std::string(dev)
                                                                 : "START.KS";
  frontend_.Leave();
  movie_.reset();
  const SceneSource &source = disc_->Source();
  game_ = std::make_unique<Game>(source.Names(), [this](size_t i, std::vector<uint8_t> *o) {
    return disc_->Source().LoadScene(i, o);
  });
  game_->translate = [this](size_t scene, const std::string &text) {
    return engine_.GetTranslation().text(int(scene), text);
  };
  game_->SetCharacterTable(&source.Catalog().Characters());
  game_->SetStageNames(source.Catalog().StageNames());
  for (const auto &[name, value] : system_.flags) game_->SetFlag(name, value);
  if (!game_->Start(script)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[zero] cannot start %s", script.c_str());
    game_.reset();
    playing_ = false;
    return false;
  }
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[zero] start %s", script.c_str());
  lastBgm_.clear();
  voice_.Stop();
  bgm_.Stop();
  se_.Stop();
  lastKey_.clear();
  lastLogged_.clear();
  lastTicks_ = 0;
  history_.clear();
  historyId_.clear();
  roulette_ = {};
  inputs_.clear();
  titleRequest_ = false;
  SetAuto(false);
  playMs_ = std::exchange(pendingPlayMs_, 0);
  playing_ = true;
  game_->Pump();
  Settle();
  if (!replaying_) Sync();
  return true;
}

void ZeroSession::Step(int input) {
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

void ZeroSession::DebugJump(const std::string &script) {
  if (!Playing()) throw std::logic_error("Start gameplay before jumping");
  if (!game_->DebugJump(script))
    throw std::runtime_error("Cannot load debug script: " + script);
  debugJumped_ = true;
  frontend_.Leave();
  movie_.reset();
  voice_.Stop();
  se_.Stop();
  roulette_ = {};
  lastKey_.clear();
  lastLogged_.clear();
  history_.clear();
  historyId_.clear();
  SetAuto(false);
  game_->Pump();
  Settle();
  Sync();
}

void ZeroSession::Advance(int choice) {
  if (!Playing()) return;
  const int in = game_->Choosing() ? choice : -1;
  if (game_->Choosing() && in < 0) return;
  if (in < 0 && Skip()) return;
  inputs_.push_back(in);
  Step(in);
}

void ZeroSession::Replay(const std::vector<int> &inputs) {
  replaying_ = true;
  const bool started = Start();
  if (started)
    for (int in : inputs) {
      Step(in);
      inputs_.push_back(in);
    }
  replaying_ = false;
  if (started) Sync();
}

// A click during a timed wait, a voice / SE wait or an in-game movie ends it;
// inside beginskip..endskip it skips the whole section.
bool ZeroSession::Skip() {
  if (!Playing()) return false;
  const auto b = game_->Blocked();
  if (b != Block::Time && b != Block::Voice && b != Block::Se && b != Block::Movie) return false;
  if (b == Block::Movie) movie_.reset();
  game_->FinishWait(true);
  RunCues();
  return true;
}

void ZeroSession::Frame() {
  if (!Playing()) return;
  if (pendingLoad_ >= 0) LoadSlot(std::exchange(pendingLoad_, -1));
  if (!Playing()) return;
  Idle();
  Sync();
  if (game_->MessageVisible()) {
    const std::string key = game_->Speaker() + "\n" + game_->Text();
    if (key != lastLogged_) {
      lastLogged_ = key;
      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[text] %s%s%s", game_->Speaker().c_str(),
                  game_->Speaker().empty() ? "" : ": ", game_->Text().c_str());
    }
  }
  Tick();
}

// Runs the game clock and ends the waits on sounds / movies the host plays.
void ZeroSession::Idle() {
  const Uint64 now = SDL_GetTicks();
  const Uint64 delta = lastTicks_ ? now - lastTicks_ : 0;
  lastTicks_ = now;
  // The in-game screens and the engine modals pause the story (and the play time).
  if (frontend_.InGame() || engine_.isStoryPaused()) return;
  playMs_ += int64_t(delta);
  game_->Advance(int64_t(std::min<Uint64>(delta, 100)));
  for (int guard = 0; guard < 8; ++guard) {
    RunCues();
    const auto b = game_->Blocked();
    if (b == Block::Voice && !voice_.Playing()) game_->FinishWait();
    else if (b == Block::Se && !se_.Playing()) game_->FinishWait();
    else if (b == Block::Movie && MovieFinished()) game_->FinishWait();
    else break;
  }
  if (movie_) {
    movie_->update(*Renderer());
    if (movie_->finished()) movie_.reset();
  }
}

void ZeroSession::Sync() {
  if (!replaying_) Remember();
  RecordHistory();
  // The command screen / roulette own the music while they show.
  const bool music = !replaying_ && !frontend_.InGame() && !roulette_.active;
  if (replaying_ || !game_->MessageVisible()) {
    if (music) PlayBgm(game_->Bgm());
    return;
  }
  const std::string key = game_->Speaker() + game_->Text();
  if (key != lastKey_) {
    lastKey_ = key;
    if (VoiceAllowed()) PlayVoiceIndex(game_->Voice());
    else voice_.Stop();
  }
  if (music) PlayBgm(game_->Bgm());
}

void ZeroSession::Tick() {
  if (!Playing()) return;
  // coffee tag: the command screen in coffee-break mode (NEXT goes on).
  if (!replaying_ && game_->Blocked() == Block::Coffee && !frontend_.InGame()) {
    voice_.Stop();
    SetAuto(false);
    frontend_.OpenCommand(true, int(game_->Flag("f.prm_tundere_eval")), MaxLike());
  }
  using R = Frontend::Result;
  switch (frontend_.Tick()) {
  case R::Save: SaveSlot(frontend_.Slot()); break;
  case R::LoadSlot: LoadRequest(frontend_.Slot()); break;
  case R::Title: titleRequest_ = true; break;
  case R::Next:
    StopMusic();
    game_->FinishWait();
    RunCues();
    break;
  default: break;
  }
  TickRoulette();
}

// While replaying, every timed / sound / movie wait ends at once and nothing plays.
void ZeroSession::Settle() {
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

void ZeroSession::RunCues() {
  for (const auto &cue : game_->TakeCues()) {
    if (replaying_) continue;
    switch (cue.kind) {
    case Game::Cue::Se:
      if (cue.name.empty()) se_.Stop();
      else PlaySe(cue.name);
      break;
    case Game::Cue::Voice:
      if (VoiceAllowed()) PlayVoiceIndex(cue.number);
      break;
    case Game::Cue::Movie: {
      movie_.reset();
      movie_ = OpenMovie(cue.number);
      if (movie_) voice_.Stop();
      break;
    }
    }
  }
}

// VoiceOptionState: a character's voice can be switched off (VOICES.NUT code).
bool ZeroSession::VoiceAllowed() const { return system_.VoiceOn(VoiceCode(game_->Speaker())); }

// System save: sf.* flags set by the scripts and the event CGs shown (CG gallery).
void ZeroSession::Remember() {
  bool changed = false;
  for (const auto &[name, value] : game_->Flags()) {
    if (name.compare(0, 3, "sf.") != 0) continue;
    auto it = system_.flags.find(name);
    if (it == system_.flags.end() || it->second != value) {
      system_.flags[name] = value;
      changed = true;
    }
  }
  const std::string ev = Upper(game_->Event());
  if (!ev.empty() && ev.compare(0, 2, "CG") == 0 && system_.seen.insert(ev).second) changed = true;
  if (changed && !systemPath_.empty()) system_.Save(systemPath_);
}

// getMaxLike(f): first maximum of f.cp_ru, cp_cu, cp_ta, cp_ch, cp_si, cp_an, cp_ri.
int ZeroSession::MaxLike() const {
  static const char *const kLikes[] = {"f.cp_ru", "f.cp_cu", "f.cp_ta", "f.cp_ch", "f.cp_si", "f.cp_an", "f.cp_ri"};
  int best = 0;
  for (int i = 1; i < 7; ++i)
    if (game_->Flag(kLikes[i]) > game_->Flag(kLikes[best])) best = i;
  return best;
}

void ZeroSession::LoadRequest(int slot) {
  SlotInfo info;
  if (!Slot(slot, &info)) return;
  pendingPlayMs_ = info.playMs;
  pendingLoad_ = slot;
}

void ZeroSession::MarkRead(const std::string &id) {
  if (id.empty() || !read_.insert(id).second || readPath_.empty()) return;
  std::ofstream(readPath_, std::ios::app) << id << '\n';
}

// HistoryState back log: one entry per page, speaker names through historyNameMap.
void ZeroSession::RecordHistory() {
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
  std::string name = Translate(game_->Speaker());
  if (name == game_->Speaker())
    if (auto it = kNames.find(name); it != kNames.end()) name = it->second;
  HistoryEntry entry{name, game_->Text(), game_->Voice()};
  // A page that continues the previous one (no er) replaces it.
  if (!history_.empty() && history_.back().name == entry.name &&
      entry.text.compare(0, history_.back().text.size(), history_.back().text) == 0)
    history_.back() = entry;
  else
    history_.push_back(entry);
  while (history_.size() > 200) history_.pop_front();
}

// sys_0027: getCharSpeed = (10 - charSpeed) * 10 ms, getAutoWait = (10 - autoWait) * 150 ms.
int ZeroSession::TextDelayMs() const { return (10 - std::clamp(system_.charSpeed, 0, 10)) * 10; }
int ZeroSession::AutoDelayMs() const { return (10 - std::clamp(system_.autoWait, 0, 10)) * 150; }

// skipMode 1 = すべて (everything), 0 = 既読 (pages already read).
bool ZeroSession::CanSkip() const {
  return Playing() && !frontend_.InGame() && (system_.skipMode == 1 || read_.count(game_->PageId()) != 0);
}

void ZeroSession::SetAuto(bool on) {
  if (on == auto_) return;
  auto_ = on;
  if (playing_) PlaySe(on ? "SYS_SE02" : "SYS_SE03");
}

// CommandState (Esc / right click) and HistoryState (wheel up / Page Up).
void ZeroSession::OpenCommand() {
  if (!Playing() || frontend_.InGame() || roulette_.active) return;
  SetAuto(false);
  frontend_.OpenCommand(false, int(game_->Flag("f.prm_tundere_eval")), MaxLike());
}

void ZeroSession::OpenHistory() {
  if (!Playing() || frontend_.InGame() || history_.empty()) return;
  SetAuto(false);
  frontend_.OpenHistory({history_.begin(), history_.end()});
}

// ---------------------------------------------------------------- saves

std::string ZeroSession::SaveLabel() const {
  SlotInfo info;
  info.title = game_->StringVar("f.title");
  info.playMs = playMs_;
  info.maxLike = MaxLike();
  return info.Label();
}

bool ZeroSession::SaveTo(const std::string &path) const {
  if (debugJumped_) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Cannot create a replay save after a debug jump; start or load a normal game first");
    return false;
  }
  if (!game_ || path.empty()) return false;
  std::ofstream out(path, std::ios::trunc);
  if (!out) return false;
  const std::string label = SaveLabel();
  out << (label.empty() ? "-" : label) << '\n';
  for (int v : inputs_) out << v << ' ';
  out << '\n';
  return bool(out);
}

bool ZeroSession::LoadFrom(const std::string &path) {
  std::ifstream in(path);
  std::string label, numbers;
  if (!in || !std::getline(in, label)) return false;
  std::getline(in, numbers);
  std::vector<int> inputs;
  std::istringstream values(numbers);
  for (int v; values >> v;) inputs.push_back(v);
  pendingPlayMs_ = SlotInfo::Parse(label).playMs;
  Replay(inputs);
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[load] %s (%zu inputs)", path.c_str(), inputs.size());
  return Playing();
}

bool ZeroSession::SaveSlot(int slot) {
  if (slot < 0 || slot >= kSaveSlots || !SaveTo(SlotPath(slot))) return false;
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[save] slot %d (%zu inputs)", slot + 1, inputs_.size());
  return true;
}

bool ZeroSession::LoadSlot(int slot) { return slot >= 0 && slot < kSaveSlots && LoadFrom(SlotPath(slot)); }

// ---------------------------------------------------------------- drawing

void ZeroSession::DrawNormal(SDL_Renderer *r, const std::string &name, float x, float y, float alpha) {
  if (alpha <= 0.0f) return;
  DrawPicture(r, Picture(name), x, y, std::min(alpha, 1.0f));
}

void ZeroSession::DrawElements(SDL_Renderer *r, const std::vector<Game::Element> &scene) {
  using E = Game::Element;
  const auto &catalog = disc_->Source().Catalog();
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
      if (ZeroNoTsukaima::Picture *p = SceneIndexPicture(catalog.ResolveSprite(e.image)))
        DrawPicture(r, p, std::floor((kW - p->Width()) / 2) + e.x, kH - p->Height() + e.y, e.alpha, e.tint);
      continue;
    }
    if (image == "black" || image == "white") {
      Fill(r, image == "white" ? 255 : 0, e.alpha);
      continue;
    }
    DrawPicture(r, index >= 0 ? SceneIndexPicture(index) : NamedScene(image), e.x, e.y, e.alpha);
  }
}

// The scene captured when a transition began, at the output resolution.
SDL_Texture *ZeroSession::Snapshot(SDL_Renderer *r, const Game::Transition &t) {
  const int w = std::max(1, int(std::lround(kW * pixelScale_)));
  const int h = std::max(1, int(std::lround(kH * pixelScale_)));
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
    SDL_SetRenderTarget(r, snapshot_);
    // Each render target keeps its own view: scale this one to 640x448.
    SDL_SetRenderViewport(r, nullptr);
    SDL_SetRenderScale(r, float(w) / kW, float(h) / kH);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    DrawElements(r, t.from);
    SDL_SetRenderTarget(r, target);
    snapSerial_ = t.serial;
  }
  return snapshot_;
}

void ZeroSession::DrawStage(SDL_Renderer *r) {
  SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
  const SDL_FRect all{0, 0, kW, kH};
  SDL_RenderFillRect(r, &all);
  if (!Playing()) return;
  DrawElements(r, game_->Scene());
  float progress = 1;
  if (const auto *t = game_->ActiveTransition(&progress)) {
    // Whole-screen crossfade: the scene captured when it began fades out.
    if (SDL_Texture *snap = Snapshot(r, *t)) {
      SDL_SetTextureAlphaModFloat(snap, 1.0f - progress);
      SDL_RenderTexture(r, snap, nullptr, &all);
    }
  }
  if (movie_) {
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderFillRect(r, &all);
    movie_->render(*r, all, Upscaler(), true);
    return;
  }
  if (const auto *m = game_->ActiveMeter()) DrawMeter(r, *m);
  if (auto_) DrawNormal(r, "MAIN_AUTOPLAY", 40, 45);
}

// MAIN_MESSAGEBASE frame and name plate picture (PR_00 msgwin: frame at 8,276;
// name plate centred in 160 px at +64,+5).
void ZeroSession::DrawMessageWindow(SDL_Renderer *r) {
  if (!HasMessage()) return;
  DrawNormal(r, "MAIN_MESSAGEBASE", kMsgX, kMsgY);
  if (const std::string *plate = disc_->Source().Catalog().NameImage(game_->Speaker()))
    if (ZeroNoTsukaima::Picture *name = NormalPicture(*plate))
      DrawPicture(r, name, kMsgX + 64 + (160 - name->Width()) / 2, kMsgY + 5, 1);
}

// deffont / msgwin margins: black 24 px text, 32 px lines at +44,+52, 532 px wide.
void ZeroSession::DrawMessageText(SDL_Renderer *r, const std::string &text) {
  constexpr float kX = kMsgX + 44, kY = kMsgY + 52, kWidth = 624 - 44 - 48, kSize = 24, kLine = 32;
  const SDL_Color black{0, 0, 0, 255};
  float y = kY;
  std::string line;
  auto flush = [&] {
    if (!line.empty()) DrawText(r, line, kX, y, kSize, black);
    line.clear();
    y += kLine;
  };
  for (size_t i = 0; i < text.size();) {
    const size_t next = Utf8Next(text, i);
    const std::string ch = text.substr(i, next - i);
    i = next;
    if (ch == "\n") {
      flush();
      continue;
    }
    if (!line.empty() && DrawText(r, line + ch, 0, 0, kSize, black, true).x > kWidth) flush();
    line += ch;
  }
  if (!line.empty()) DrawText(r, line, kX, y, kSize, black);
}

// Page cursor at +550,+122 of the frame, blinking while the page waits.
void ZeroSession::DrawMessageCursor(SDL_Renderer *r) {
  if (WaitingForClick() && (SDL_GetTicks() / 500) % 2 == 0) DrawNormal(r, "MAIN_MESSAGECURSOR", kMsgX + 550, kMsgY + 122);
}

// LikeMeter (好感度) and TndlMeter (ツンデレパラメータ), OVERRIDE.NUT.
void ZeroSession::DrawMeter(SDL_Renderer *r, const Game::Meter &m) {
  const float t = std::clamp(float(game_->Clock() - m.start) / 1000.0f, 0.0f, 1.0f);
  if (m.kind == Game::Meter::Like) {
    static const std::map<std::string, std::string> kIcons = {
        {"RU", "RUIZE"}, {"CU", "KYURUKE"}, {"TA", "TABASA"}, {"SI", "SIESTA"},
        {"AN", "ANRIETTA"}, {"CH", "KURISH"}, {"RI", "RISH"}, {"YRI", "YRISH"}};
    auto it = kIcons.find(Upper(m.name));
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

// ---------------------------------------------------------------- roulette

// RouletteState (tndlslot): two reels; CIRCLE stops the left, then the right one;
// f.prm_tundere_boot = left * 3 + rpoint[right] (+18 on the tsun slot).
void ZeroSession::TickRoulette() {
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
    SetAuto(false);
    PlaySe("SLOT00");
    PlayBgm("BGM23");
  }
  const double dt = double(now - a.last);
  a.last = now;
  if (a.state == 0 && now - a.start >= 1000) a.state = 1;
  if (a.state == 1 && now - a.start >= 2000) {
    a.state = 2;
    PlaySe("SLOT01");
  }
  constexpr double kSpeed = 5; // patterns per second
  if (a.state <= 2) a.shiftl += kSpeed * dt / 1000.0;
  if (a.state <= 3) a.shiftr += kSpeed * dt / 1000.0;
  if (a.state == 4) {
    if (!a.slot03 && now - a.doneAt >= 500) {
      a.slot03 = true;
      PlaySe("SLOT03");
      StopMusic();
    }
    if (!a.finished && now - a.doneAt >= 2500) {
      a.finished = true;
      Advance(a.pick);
    }
  }
}

void ZeroSession::RouletteEvent(const SDL_Event &e) {
  RouletteAnim &a = roulette_;
  const bool press = (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
                      (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER || e.key.key == SDLK_SPACE ||
                       e.key.key == SDLK_Z)) ||
                     (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT);
  if (!press || !a.active) return;
  if (a.state == 2) {
    PlaySe("SLOT02");
    a.state = 3;
    a.shiftl = std::ceil(a.shiftl);
  } else if (a.state == 3) {
    static constexpr int kPoint[] = {1, 3, 2, 3};
    PlaySe("SLOT02");
    a.state = 4;
    a.shiftr = std::ceil(a.shiftr);
    a.left = int(a.shiftl) % 6;
    a.right = int(a.shiftr) % 4;
    a.pick = a.left * 3 + kPoint[a.right] + (game_->RouletteTsun() ? 18 : 0);
    a.doneAt = SDL_GetTicks();
  }
}

void ZeroSession::DrawRoulette(SDL_Renderer *r) {
  const RouletteAnim &a = roulette_;
  if (!RouletteActive()) return;
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
int ZeroSession::MovePosition(const std::string &text) {
  static const char *const kPlaces[] = {
      "廊下", "図書室", "教室", "食堂", "厨房", "ルイズの部屋", "タバサの部屋", "キュルケの部屋",
      "アンリエッタの部屋", "クリスの部屋", "シエスタの部屋", "広場", "シエスタの教室", "屋上", "校庭",
      "体育館", "駅前", "通学路", "商店街", "公園", "生徒会室", "学校前", "劇場前", "市場", "裏街道", "街道"};
  for (int i = 0; i < int(sizeof(kPlaces) / sizeof(kPlaces[0])); ++i)
    if (text == kPlaces[i]) return i;
  return 0;
}

std::string ZeroSession::Translate(const std::string &text) const {
  return game_ ? engine_.GetTranslation().text(int(game_->SceneIndex()), text) : text;
}

} // namespace ZeroNoTsukaima
