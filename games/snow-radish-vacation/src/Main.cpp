// Snow Radish Vacation!! (Overflow ADV3) running on the Kotonoha engine.
// Usage: SnowRadish --snow <dir with SCRIPT.DAT and GRAPHIC.DAT> [--mcp [port]]
#include "SnowRadish/Game.hpp"

#include <Kotonoha/Main.hpp>
#include <Kotonoha/components/GameplayFlow.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>

using Kotonoha::global;
namespace {

constexpr int kW = 800, kH = 600;

std::string g_dir;

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

std::vector<uint8_t> readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

// Taken from the original executable: WAVEFORMATEX is stereo, 16-bit, 22050 Hz x1.
constexpr int kSampleRate = 22050;

// One of the original mixer channels (0 voice, 1 BGM, 2 SE): starting a sound
// replaces whatever the channel was playing.
struct Channel {
    SDL_AudioStream* stream = nullptr;
    std::vector<int16_t> loop; // repeated while looping
    int channels = 2;
    bool looping = false;
};

struct SpriteTex {
    SDL_Texture* tex = nullptr;
    float x = 0, y = 0; // cropped picture position on the screen canvas
};

struct State {
    std::unique_ptr<SnowRadish::Game> game;
    std::vector<uint8_t> soundPack, voicePack;
    std::map<std::string, SnowRadish::PackEntry> soundIndex, voiceIndex;
    Channel voice, bgm, se;
    std::string lastBgm;
    bool lastBgmLoop = true;
    bool muted = false; // set while replaying a save
    std::vector<uint8_t> script;
    std::vector<uint8_t> gfx;
    std::map<std::string, SnowRadish::PackEntry> gfxIndex;
    std::map<std::string, SDL_Texture*> textures;
    std::map<std::string, SpriteTex> spriteTextures;
    SDL_Texture* bg = nullptr;
    bool filled = false;
    uint32_t fillColor = 0;
    struct Shown {
        SpriteTex pic;
        int dx = 0, dy = 0;
        int tint[3] = {0x100, 0x100, 0x100};
    };
    std::vector<Shown> sprites;
    uint32_t seenVersion = ~0u;
    Kotonoha::GameplayFlow* gameplay = nullptr;
};
State* g_state = nullptr;

void stopChannel(Channel& c) {
    if (c.stream) SDL_DestroyAudioStream(c.stream);
    c = Channel{};
}

bool playing(const Channel& c) {
    return c.stream && (c.looping || SDL_GetAudioStreamQueued(c.stream) > 0);
}

bool startChannel(Channel& c, const std::vector<int16_t>& first, std::vector<int16_t> loop, int channels, bool looping) {
    stopChannel(c);
    if (first.empty() && loop.empty()) return false;
    const SDL_AudioSpec spec{SDL_AUDIO_S16, channels, kSampleRate};
    SDL_AudioStream* st = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!st) return false;
    if (!first.empty()) SDL_PutAudioStreamData(st, first.data(), int(first.size() * sizeof(int16_t)));
    // Played once, the loop block simply follows the intro.
    if (!looping && !loop.empty()) SDL_PutAudioStreamData(st, loop.data(), int(loop.size() * sizeof(int16_t)));
    // Without a flush the resampler keeps a few bytes back and the sound never "ends".
    if (!looping || loop.empty()) SDL_FlushAudioStream(st);
    SDL_ResumeAudioStreamDevice(st);
    c.stream = st;
    c.channels = channels;
    c.looping = looping && !loop.empty();
    if (c.looping) c.loop = std::move(loop);
    return true;
}

bool startAdps(Channel& c, const SnowRadish::PackEntry& e, bool loop) {
    SnowRadish::AdpsTrack t;
    if (!SnowRadish::decodeAdpsFile(SnowRadish::extractEntry(g_state->soundPack, e), t)) return false;
    return startChannel(c, t.intro.samples, std::move(t.loop.samples), 2, loop);
}

void playSound(const std::string& kind, const std::string& name, bool loop) {
    State& s = *g_state;
    if (kind == "bgmstop") {
        s.lastBgm.clear();
        stopChannel(s.bgm);
        return;
    }
    if (kind == "sestop") return stopChannel(s.se);
    if (kind == "voicestop") return stopChannel(s.voice);
    if (!s.muted) SDL_Log("[sound] %s %s%s", kind.c_str(), name.c_str(), loop ? " (loop)" : "");
    if (kind == "bgm") {
        s.lastBgm = name;
        s.lastBgmLoop = loop;
        if (s.muted) return;
        auto it = s.soundIndex.find(lower(name));
        if (it != s.soundIndex.end()) startAdps(s.bgm, it->second, loop);
        return;
    }
    if (s.muted) return;
    if (kind == "se") {
        auto it = s.soundIndex.find(lower(name));
        if (it != s.soundIndex.end()) startAdps(s.se, it->second, loop);
    } else if (kind == "voice") {
        auto it = s.voiceIndex.find(lower(name));
        if (it == s.voiceIndex.end()) return;
        SnowRadish::Pcm pcm = SnowRadish::decodeAdp4(SnowRadish::extractEntry(s.voicePack, it->second));
        std::vector<int16_t> rep;
        if (loop) rep = pcm.samples;
        startChannel(s.voice, pcm.samples, std::move(rep), 1, loop);
    }
}

void updateAudio() {
    State& s = *g_state;
    for (Channel* c : {&s.voice, &s.bgm, &s.se}) {
        if (!c->stream) continue;
        const int queued = SDL_GetAudioStreamQueued(c->stream);
        if (c->looping) {
            // Keep at least half a second queued so the loop restarts seamlessly.
            if (queued < kSampleRate * c->channels)
                SDL_PutAudioStreamData(c->stream, c->loop.data(), int(c->loop.size() * sizeof(int16_t)));
        } else if (queued == 0) {
            stopChannel(*c);
        }
    }
}

SDL_Texture* textureFrom(SDL_Surface* s) {
    if (!s) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(global.render, s);
    SDL_DestroySurface(s);
    if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    return tex;
}

// Full-frame pictures (.EVD) and raw buttons (.IMG, type 0).
SDL_Texture* loadGraphic(const std::string& name, const char* ext) {
    auto key = lower(name + ext);
    auto cached = g_state->textures.find(key);
    if (cached != g_state->textures.end()) return cached->second;
    SDL_Texture* tex = nullptr;
    auto it = g_state->gfxIndex.find(key);
    SnowRadish::Image img;
    if (it != g_state->gfxIndex.end() && SnowRadish::decodeEvd(SnowRadish::extractEntry(g_state->gfx, it->second), img)) {
        SDL_Surface* s = SDL_CreateSurfaceFrom(int(img.width), int(img.height), SDL_PIXELFORMAT_RGB24, img.rgb.data(),
                                               int(img.width * 3));
        tex = textureFrom(s ? SDL_DuplicateSurface(s) : nullptr);
        if (s) SDL_DestroySurface(s);
    }
    g_state->textures[key] = tex;
    return tex;
}

// Character sprites (.CHD): full-screen canvases cropped to their visible part.
SpriteTex loadSprite(const std::string& name) {
    auto key = lower(name);
    auto cached = g_state->spriteTextures.find(key);
    if (cached != g_state->spriteTextures.end()) return cached->second;
    SpriteTex out;
    auto it = g_state->gfxIndex.find(key + ".chd");
    if (it == g_state->gfxIndex.end()) it = g_state->gfxIndex.find("dummy00.chd");
    SnowRadish::Sprite sp;
    if (it != g_state->gfxIndex.end() && SnowRadish::decodeChd(SnowRadish::extractEntry(g_state->gfx, it->second), sp)) {
        SDL_Surface* s = SDL_CreateSurfaceFrom(int(sp.width), int(sp.height), SDL_PIXELFORMAT_RGBA32, sp.rgba.data(),
                                               int(sp.width * 4));
        out.tex = textureFrom(s ? SDL_DuplicateSurface(s) : nullptr);
        if (s) SDL_DestroySurface(s);
        out.x = float(sp.x);
        out.y = float(sp.y);
    } else {
        SDL_Log("[gfx] missing sprite %s", name.c_str());
    }
    g_state->spriteTextures[key] = out;
    return out;
}

void drawTex(SDL_Texture* t, float x, float y) {
    if (!t) return;
    SDL_FRect r{x, y, 0, 0};
    SDL_GetTextureSize(t, &r.w, &r.h);
    SDL_RenderTexture(global.render, t, nullptr, &r);
}

// Tint as in the original (0x100 neutral per channel): below it darkens, above
// it adds the extra amount on top.
void drawTinted(SDL_Texture* t, float x, float y, const int tint[3]) {
    if (!t) return;
    auto mod = [](int v) { return Uint8(std::clamp(v, 0, 255)); };
    SDL_SetTextureColorMod(t, mod(tint[0]), mod(tint[1]), mod(tint[2]));
    drawTex(t, x, y);
    if (tint[0] > 0x100 || tint[1] > 0x100 || tint[2] > 0x100) {
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
        SDL_SetTextureColorMod(t, mod(tint[0] - 0x100), mod(tint[1] - 0x100), mod(tint[2] - 0x100));
        drawTex(t, x, y);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    }
    SDL_SetTextureColorMod(t, 255, 255, 255);
}

class SnowSession : public Kotonoha::GameSession {
public:
    explicit SnowSession(State& s) : s_(s) {}

    bool Start() override {
        stopChannel(s_.bgm);
        stopChannel(s_.se);
        stopChannel(s_.voice);
        s_.lastBgm.clear();
        s_.game = std::make_unique<SnowRadish::Game>(s_.script);
        s_.game->sound = playSound;
        if (SDL_getenv("SNOW_TRACE")) s_.game->trace = [](const std::string& t) { SDL_Log("[vm] %s", t.c_str()); };
        s_.game->voicePlaying = [this] { return playing(s_.voice); };
        s_.game->sePlaying = [this] { return playing(s_.se); };
        s_.game->transition = [this](int, int msec) { BeginTransition(msec); };
        s_.game->transitionPlaying = [this] { return Fading(); };
        if (!s_.game->init()) return false;
        s_.game->pump();
        s_.seenVersion = ~0u;
        return true;
    }
    void Step(int input) override {
        auto& g = *s_.game;
        if (g.choosing()) {
            if (input < 0) return;
            g.choose(size_t(input));
        } else {
            g.click();
        }
        g.pump();
    }
    bool Skip() override {
        auto& g = *s_.game;
        if (!g.waitingForAudio()) return false;
        g.skipAudioWait();
        fadeMs_ = 0;
        return true;
    }
    void Idle() override {
        auto& g = *s_.game;
        if (!g.waitingForClick() && !g.choosing() && !g.finished()) g.pump();
    }
    bool Choosing() const override { return s_.game->choosing(); }
    std::vector<std::string> Choices() const override { return s_.game->choices(); }
    bool HasMessage() const override { return s_.game->hasMessage(); }
    std::string Speaker() const override { return s_.game->message().name; }
    std::string Text() const override { return s_.game->message().text; }

    void Sync() override {
        auto& g = *s_.game;
        if (g.sceneVersion() == s_.seenVersion) return;
        s_.seenVersion = g.sceneVersion();
        s_.filled = g.filled();
        s_.fillColor = g.fillColor();
        s_.bg = g.background().empty() ? nullptr : loadGraphic(g.background(), ".evd");
        s_.sprites.clear();
        for (const auto& sp : g.sprites()) {
            State::Shown v;
            v.pic = loadSprite(sp.name);
            v.dx = sp.x;
            v.dy = sp.y;
            std::copy(std::begin(sp.tint), std::end(sp.tint), v.tint);
            s_.sprites.push_back(v);
        }
    }
    void Tick() override { updateAudio(); }
    void SetReplaying(bool r) override {
        s_.muted = r;
        if (r) {
            stopChannel(s_.bgm);
            stopChannel(s_.se);
            stopChannel(s_.voice);
        } else if (!s_.lastBgm.empty()) {
            playSound("bgm", s_.lastBgm, s_.lastBgmLoop);
        }
    }
    void Draw(SDL_Renderer* r) override {
        DrawScene(r);
        if (!Fading()) return;
        const float t = float(SDL_GetTicks() - fadeStart_) / float(fadeMs_);
        SDL_SetTextureAlphaModFloat(from_, 1.0f - t);
        SDL_RenderTexture(r, from_, nullptr, nullptr);
    }
    void DrawScene(SDL_Renderer* r) {
        if (s_.filled) {
            SDL_SetRenderDrawColor(r, Uint8(s_.fillColor >> 16), Uint8(s_.fillColor >> 8), Uint8(s_.fillColor), 255);
            SDL_RenderFillRect(r, nullptr);
        }
        if (s_.bg) drawTex(s_.bg, 0, 0);
        for (const auto& sp : s_.sprites) drawTinted(sp.pic.tex, sp.pic.x + float(sp.dx), sp.pic.y + float(sp.dy), sp.tint);
    }

    // Snapshot of what is on screen now (the scene before the script's changes are
    // synced), faded out over the new scene.
    void BeginTransition(int msec) {
        fadeMs_ = 0;
        if (s_.muted) return;
        SDL_Renderer* r = global.render;
        if (!from_) {
            from_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 800, 600);
            if (!from_) return;
            SDL_SetTextureBlendMode(from_, SDL_BLENDMODE_BLEND);
        }
        SDL_Texture* old = SDL_GetRenderTarget(r);
        SDL_SetRenderTarget(r, from_);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        DrawScene(r);
        SDL_SetRenderTarget(r, old);
        fadeStart_ = SDL_GetTicks();
        fadeMs_ = uint64_t(msec);
    }
    bool Fading() const { return fadeMs_ && SDL_GetTicks() - fadeStart_ < fadeMs_; }

    // The original title (T00b/T00c + highlighted buttons T0nb/T0nc at x=0x144).
    std::vector<TitleItem> TitleItems(bool hasSaves) const override {
        titleSaves_ = hasSaves;
        std::vector<TitleItem> v;
        if (hasSaves) {
            v.push_back({{0x144, 0x159, 0xa5, 0x31}, TitleAction::NewGame});
            v.push_back({{0x144, 0x19f, 0xa5, 0x31}, TitleAction::Load});
            v.push_back({{0x144, 0x1e4, 0xa5, 0x31}, TitleAction::Quit});
        } else {
            v.push_back({{0x144, 0x170, 0xa5, 0x31}, TitleAction::NewGame});
            v.push_back({{0x144, 0x1b5, 0xa5, 0x31}, TitleAction::Quit});
        }
        return v;
    }
    void DrawTitle(SDL_Renderer*, int hover) override {
        const char suffix = titleSaves_ ? 'c' : 'b';
        drawTex(loadGraphic(std::string("T00") + suffix, ".evd"), 0, 0);
        const auto items = TitleItems(titleSaves_);
        if (hover < 0 || size_t(hover) >= items.size()) return;
        const std::string button = "T0" + std::to_string(hover + 1) + suffix;
        drawTex(loadGraphic(button, ".img"), items[size_t(hover)].rect.x, items[size_t(hover)].rect.y);
    }

private:
    SDL_Texture* from_ = nullptr;
    uint64_t fadeStart_ = 0, fadeMs_ = 0;
    mutable bool titleSaves_ = false;
    State& s_;
};

} // namespace

SDL_AppResult LocalArgs(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (argv[i] && std::string(argv[i]) == "--snow") {
            g_dir = argv[i + 1];
            argv[i][0] = '!';
            argv[i + 1][0] = '!';
            break;
        }
    }
    if (g_dir.empty())
        if (const char* e = std::getenv("SNOW_DIR")) g_dir = e;
    return SDL_APP_CONTINUE;
}

SDL_AppResult LocalInit(void**, int, char*[]) {
    if (g_dir.empty()) {
        SDL_Log("Snow Radish: use --snow <dir>");
        return SDL_APP_FAILURE;
    }
    // Game ports show only the game UI; F7 or KOTONOHA_DEBUG_UI=1 brings the debug panels back.
    global.config.enableDebugUi = std::getenv("KOTONOHA_DEBUG_UI") != nullptr;
    auto* s = new State();
    g_state = s;
    s->script = readFile(std::filesystem::path(g_dir) / "SCRIPT.DAT");
    s->gfx = readFile(std::filesystem::path(g_dir) / "GRAPHIC.DAT");
    if (s->script.empty() || s->gfx.empty()) {
        SDL_Log("Snow Radish: SCRIPT.DAT/GRAPHIC.DAT not found in %s", g_dir.c_str());
        return SDL_APP_FAILURE;
    }
    for (auto& e : SnowRadish::readDirectory(s->gfx)) s->gfxIndex[lower(e.name)] = e;
    auto stem = [](const std::string& n) { return lower(n.substr(0, n.find('.'))); };
    s->soundPack = readFile(std::filesystem::path(g_dir) / "SOUND.DAT");
    for (auto& e : SnowRadish::readDirectory(s->soundPack)) s->soundIndex[stem(e.name)] = e;
    s->voicePack = readFile(std::filesystem::path(g_dir) / "VOICE.DAT");
    for (auto& e : SnowRadish::readDirectory(s->voicePack)) s->voiceIndex[stem(e.name)] = e;
    Kotonoha::GameplayFlow::Config cfg;
    cfg.id = "snow-radish-vacation";
    cfg.title = "Snow Radish Vacation!!";
    cfg.width = kW;
    cfg.height = kH;
    cfg.textBox.box = {20, 440, kW - 40, 150};
    s->gameplay = new Kotonoha::GameplayFlow(cfg);
    s->muted = true; // the opening runs behind the title screen
    const bool attached = s->gameplay->Attach(std::make_unique<SnowSession>(*s));
    s->muted = false;
    s->lastBgm.clear();
    if (!attached) {
        SDL_Log("Snow Radish: script init failed");
        return SDL_APP_FAILURE;
    }
    SDL_SetWindowTitle(global.window, "Snow Radish Vacation!!");
    return SDL_APP_CONTINUE;
}

SDL_AppResult LocalEvent(void*, SDL_Event* e) {
    if (g_state && g_state->gameplay) g_state->gameplay->HandleEvent(e, &global);
    return SDL_APP_CONTINUE;
}

SDL_AppResult LocalIterate(void* appstate) {
    if (!g_state) return SDL_APP_CONTINUE;
    Kotonoha::Kotonoha* app = Kotonoha::GetApp(appstate);
    TTF_Font* font = nullptr;
    if (app) {
        auto it = app->fonts.find("NotoSansJP-Regular");
        if (it != app->fonts.end()) font = it->second;
    }
    Kotonoha::MarkFrameHandled();
    return g_state->gameplay->RunFrame(app, &global, font);
}

void LocalQuit(void*, SDL_AppResult) {
    if (g_state) {
        stopChannel(g_state->bgm);
        stopChannel(g_state->se);
        stopChannel(g_state->voice);
        delete g_state->gameplay;
    }
    delete g_state;
    g_state = nullptr;
}
