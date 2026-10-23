// Zero no Tsukaima (PS2) scene scripts running on the Kotonoha engine.
// Usage: ZeroNoTsukaima --zero <disc.bin (MODE1/2048)> [--mcp [port]]
// Backgrounds, character sprites and event CGs are drawn from SCENEDAT.
#include <ZeroNoTsukaima/Game.hpp>
#include <ZeroNoTsukaima/Source.hpp>
#include <ZeroNoTsukaima/Vag.hpp>

#include <Kotonoha/Main.hpp>
#include <Kotonoha/components/GameplayFlow.hpp>

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

// Headerless streams: rates are estimates; override with ZERO_VOICE_RATE / ZERO_BGM_RATE.
int rateFromEnv(const char *name, int fallback) {
  const char *v = std::getenv(name);
  const int r = v ? std::atoi(v) : 0;
  return r >= 8000 && r <= 96000 ? r : fallback;
}
// Live-tunable: F2/F3 lower/raise the voice rate, F4/F5 the BGM rate (applied to what is playing too).
int kVoiceRate = rateFromEnv("ZERO_VOICE_RATE", 24000), kBgmRate = rateFromEnv("ZERO_BGM_RATE", 24000);
constexpr int kRatePresets[] = {11025, 16000, 22050, 24000, 32000, 44100, 48000};

struct Stream {
  SDL_AudioStream *stream = nullptr;
  std::vector<int16_t> pcm; // kept for looping
  bool loop = false;
  int rate = 0; // rate the stream was opened with
};

struct Picture {
  SDL_Texture *tex = nullptr;
  int w = 0, h = 0;
};

struct State {
  ZeroNoTsukaima::SceneSource source;
  std::map<int, Picture> pictures;
  int bgIndex = -1, cgIndex = -1;
  std::vector<std::pair<int, int>> sprites; // (SCENEDAT index, centre offset), back to front
  Stream voice, bgm;
  std::string lastBgm;
  Kotonoha::GameplayFlow *gameplay = nullptr;
};
State *g_state = nullptr;

void stopStream(Stream &s) {
  if (s.stream) SDL_DestroyAudioStream(s.stream);
  s = Stream{};
}

bool playStream(Stream &out, const std::vector<uint8_t> &raw, int rate, bool loop) {
  stopStream(out);
  std::vector<int16_t> pcm;
  int channels = 1;
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
    pcm.resize(n * 2);
    for (size_t i = 0; i < n; ++i) {
      pcm[2 * i] = l[i];
      pcm[2 * i + 1] = r[i];
    }
    channels = 2;
  } else {
    pcm = ZeroNoTsukaima::DecodeVag(raw.data(), raw.size());
  }
  if (pcm.size() < 256) return false;
  const SDL_AudioSpec spec{SDL_AUDIO_S16, channels, rate};
  SDL_AudioStream *st = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (!st) return false;
  SDL_PutAudioStreamData(st, pcm.data(), int(pcm.size() * sizeof(int16_t)));
  SDL_ResumeAudioStreamDevice(st);
  SDL_Log("[audio] %s %zu samples @%d Hz", loop ? "bgm" : "voice", pcm.size(), rate);
  out.stream = st;
  out.loop = loop;
  out.rate = rate;
  if (loop) out.pcm = std::move(pcm);
  return true;
}

void stepRate(int &rate, Stream &live, int dir, const char *what) {
  constexpr int n = int(sizeof(kRatePresets) / sizeof(kRatePresets[0]));
  int i = 0;
  while (i < n - 1 && kRatePresets[i] < rate) ++i;
  rate = kRatePresets[std::clamp(i + dir, 0, n - 1)];
  if (live.stream && live.rate > 0) SDL_SetAudioStreamFrequencyRatio(live.stream, float(rate) / float(live.rate));
  SDL_Log("[audio] %s rate -> %d Hz", what, rate);
}

void updateAudio() {
  State &s = *g_state;
  if (s.voice.stream && SDL_GetAudioStreamQueued(s.voice.stream) == 0) stopStream(s.voice);
  if (s.bgm.stream && s.bgm.loop && SDL_GetAudioStreamQueued(s.bgm.stream) < int(s.bgm.pcm.size()))
    SDL_PutAudioStreamData(s.bgm.stream, s.bgm.pcm.data(), int(s.bgm.pcm.size() * sizeof(int16_t)));
}

void playVoice(int index) {
  stopStream(g_state->voice);
  std::vector<uint8_t> raw;
  if (index > 0 && g_state->source.LoadVoice(size_t(index), &raw)) playStream(g_state->voice, raw, kVoiceRate, false);
}

// "bgm04" -> SOUND_ID entry 4; series with other prefixes are not mapped yet.
void playBgm(const std::string &name) {
  State &s = *g_state;
  if (name == s.lastBgm) return;
  s.lastBgm = name;
  stopStream(s.bgm);
  if (name.size() < 4 || name.compare(0, 3, "bgm") != 0 || !std::isdigit((unsigned char)name[3])) return;
  std::vector<uint8_t> raw;
  if (s.source.LoadSound(size_t(std::atoi(name.c_str() + 3)), &raw)) playStream(s.bgm, raw, kBgmRate, true);
}

const Picture *picture(int index) {
  State &s = *g_state;
  if (index < 0) return nullptr;
  auto it = s.pictures.find(index);
  if (it == s.pictures.end()) {
    Picture p;
    ZeroNoTsukaima::Image img;
    if (s.source.LoadPicture(index, &img)) {
      SDL_Surface *surf = SDL_CreateSurfaceFrom(img.width, img.height, SDL_PIXELFORMAT_RGBA32, img.rgba.data(), img.width * 4);
      if (surf) {
        p.tex = SDL_CreateTextureFromSurface(global.render, surf);
        p.w = img.width;
        p.h = img.height;
        SDL_DestroySurface(surf);
      }
    }
    it = s.pictures.emplace(index, p).first;
  }
  return it->second.tex ? &it->second : nullptr;
}

void drawPicture(int index) {
  const Picture *p = picture(index);
  if (!p) return;
  // Tall pictures are scroll images; show the top part.
  SDL_FRect src{0, 0, float(p->w), float(std::min(p->h, p->w * kH / kW))};
  SDL_FRect dst{0, 0, kW, kH};
  SDL_RenderTexture(global.render, p->tex, &src, &dst);
}

// Sprites are bottom-aligned canvases (640x448, a few 800x448) centred on the screen plus xpos.
void drawSprite(int index, int xpos) {
  const Picture *p = picture(index);
  if (!p) return;
  SDL_FRect dst{float((kW - p->w) / 2 + xpos), float(kH - p->h), float(p->w), float(p->h)};
  SDL_RenderTexture(global.render, p->tex, nullptr, &dst);
}

class ZeroSession : public Kotonoha::GameSession {
public:
  explicit ZeroSession(State &s) : s_(s) {}

  bool Start() override {
    game_ = std::make_unique<ZeroNoTsukaima::Game>(
        s_.source.Names(), [this](size_t i, std::vector<uint8_t> *o) { return s_.source.LoadScene(i, o); });
    game_->SetCharacterTable(&s_.source.Catalog().Characters());
    if (!game_->Start("START.KS")) return false;
    game_->Pump();
    s_.lastBgm.clear();
    stopStream(s_.voice);
    stopStream(s_.bgm);
    lastKey_.clear();
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
  }

  void Idle() override {
    if (!game_->WaitingForClick() && !game_->Choosing() && !game_->Finished()) game_->Pump();
  }

  bool Choosing() const override { return game_->Choosing(); }
  std::vector<std::string> Choices() const override { return game_->Choices(); }
  bool HasMessage() const override { return game_->MessageVisible(); }
  std::string Speaker() const override { return game_->Speaker(); }
  std::string Text() const override { return game_->Text(); }
  std::string Caption() const override {
    return game_->Stage() + (game_->Event().empty() ? "" : "  /  " + game_->Event());
  }

  void SetReplaying(bool r) override { replaying_ = r; }

  void Sync() override {
    s_.bgIndex = s_.source.Catalog().ResolveStage(game_->Stage());
    s_.cgIndex = game_->Event().empty() ? -1 : s_.source.Catalog().ResolveEvent(game_->Event());
    s_.sprites.clear();
    for (const auto &sp : game_->Sprites()) {
      const int index = s_.source.Catalog().ResolveSprite(sp.image);
      if (index >= 0) s_.sprites.emplace_back(index, sp.xpos);
    }
    if (replaying_ || !game_->MessageVisible()) return;
    const std::string key = game_->Speaker() + game_->Text();
    if (key != lastKey_) {
      lastKey_ = key;
      playVoice(game_->Voice());
    }
    playBgm(game_->Bgm());
  }

  void Tick() override { updateAudio(); }

  void Draw(SDL_Renderer *) override {
    drawPicture(s_.bgIndex);
    for (const auto &[index, xpos] : s_.sprites) drawSprite(index, xpos);
    drawPicture(s_.cgIndex);
  }

private:
  State &s_;
  std::unique_ptr<ZeroNoTsukaima::Game> game_;
  std::string lastKey_;
  bool replaying_ = false;
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
  Kotonoha::GameplayFlow::Config cfg;
  cfg.id = "zero-no-tsukaima";
  cfg.title = "Zero no Tsukaima";
  cfg.width = kW;
  cfg.height = kH;
  cfg.textBox.box = {10, 300, kW - 20, 140};
  cfg.textBox.fontSize = 22.0f;
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
  Kotonoha::MarkFrameHandled();
  return g_state->gameplay->RunFrame(app, &global, font);
}

void LocalQuit(void *, SDL_AppResult) {
  if (g_state) {
    stopStream(g_state->voice);
    stopStream(g_state->bgm);
    delete g_state->gameplay;
  }
  delete g_state;
  g_state = nullptr;
}
