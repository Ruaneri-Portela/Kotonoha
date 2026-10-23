// Snow Radish Vacation!! (Overflow ADV3) running on the Kotonoha engine.
// Usage: SnowRadish --snow <dir with SCRIPT.DAT and GRAPHIC.DAT> [--mcp [port]]
#include "SnowRadish/Game.hpp"

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
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
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

// One of the original mixer channels (0 voice, 1 BGM, 2 SE), played on the
// engine's Voice / BGM / Se channel: starting a sound replaces whatever the
// channel was playing.
struct Channel {
    Kotonoha::PcmPlayer player;
    const char* engineChannel = "";
};

struct SpriteTex {
    Kotonoha::CanvasImage img; // drawn through the engine upscaler
    float x = 0, y = 0; // cropped picture position on the screen canvas
};

struct State {
    std::unique_ptr<SnowRadish::Game> game;
    std::vector<uint8_t> soundPack, voicePack;
    std::map<std::string, SnowRadish::PackEntry> soundIndex, voiceIndex;
    Channel voice{{}, "Voice"}, bgm{{}, "BGM"}, se{{}, "Se"};
    std::string lastBgm;
    bool lastBgmLoop = true;
    // The title music keeps playing into the story when the boot script asks for it again.
    std::string resumeBgm;
    bool muted = false; // set while replaying a save
    std::vector<uint8_t> script;
    std::vector<uint8_t> gfx;
    std::map<std::string, SnowRadish::PackEntry> gfxIndex;
    std::map<std::string, Kotonoha::CanvasImage> textures;
    std::map<std::string, SpriteTex> spriteTextures;
    Kotonoha::CanvasImage* bg = nullptr;
    bool filled = false;
    uint32_t fillColor = 0;
    struct Shown {
        SpriteTex* pic = nullptr;
        int dx = 0, dy = 0;
        int tint[3] = {0x100, 0x100, 0x100};
    };
    std::vector<Shown> sprites;
    uint32_t seenVersion = ~0u;
    Kotonoha::GameplayFlow* gameplay = nullptr;
};
State* g_state = nullptr;

void stopChannel(Channel& c) { c.player.Stop(); }

bool playing(const Channel& c) { return c.player.Playing(); }

bool startChannel(Channel& c, std::vector<int16_t> first, const std::vector<int16_t>& loop, int channels, bool looping) {
    stopChannel(c);
    if (first.empty() && loop.empty()) return false;
    // Played once, the loop block simply follows the intro.
    Kotonoha::PcmPlayer::Clip clip;
    clip.loopStart = first.size() / size_t(channels);
    clip.samples = std::move(first);
    clip.samples.insert(clip.samples.end(), loop.begin(), loop.end());
    clip.channels = channels;
    clip.rate = kSampleRate;
    clip.loop = looping && !loop.empty();
    return c.player.Play(static_cast<Kotonoha::Sound*>(global.sound), c.engineChannel, std::move(clip));
}

bool startAdps(Channel& c, const SnowRadish::PackEntry& e, bool loop) {
    SnowRadish::AdpsTrack t;
    if (!SnowRadish::decodeAdpsFile(SnowRadish::extractEntry(g_state->soundPack, e), t)) return false;
    return startChannel(c, std::move(t.intro.samples), t.loop.samples, 2, loop);
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
    if (kind == "bgm" && !s.resumeBgm.empty()) {
        const bool same = name == s.resumeBgm && playing(s.bgm);
        s.resumeBgm.clear();
        if (same) {
            s.lastBgm = name;
            s.lastBgmLoop = loop;
            return;
        }
    }
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
        startChannel(s.voice, std::move(pcm.samples), rep, 1, loop);
    }
}

Kotonoha::CanvasImage textureFrom(SDL_Surface* s) {
    if (!s) return {};
    // Opaque RGB pictures become XRGB textures, which the canvas target would
    // blend as transparent; give them a real alpha channel.
    SDL_Surface* rgba = s->format == SDL_PIXELFORMAT_RGBA32 ? s : SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    if (rgba != s) SDL_DestroySurface(s);
    if (!rgba) return {};
    SDL_Texture* tex = SDL_CreateTextureFromSurface(global.render, rgba);
    SDL_DestroySurface(rgba);
    if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    return Kotonoha::CanvasImage(tex);
}

// Full-frame pictures (.EVD) and raw buttons (.IMG, type 0).
Kotonoha::CanvasImage* loadGraphic(const std::string& name, const char* ext) {
    auto key = lower(name + ext);
    auto cached = g_state->textures.find(key);
    if (cached != g_state->textures.end()) return cached->second ? &cached->second : nullptr;
    Kotonoha::CanvasImage tex;
    auto it = g_state->gfxIndex.find(key);
    SnowRadish::Image img;
    if (it != g_state->gfxIndex.end() && SnowRadish::decodeEvd(SnowRadish::extractEntry(g_state->gfx, it->second), img)) {
        SDL_Surface* s = SDL_CreateSurfaceFrom(int(img.width), int(img.height), SDL_PIXELFORMAT_RGB24, img.rgb.data(),
                                               int(img.width * 3));
        tex = textureFrom(s ? SDL_DuplicateSurface(s) : nullptr);
        if (s) SDL_DestroySurface(s);
    }
    auto& slot = g_state->textures[key] = std::move(tex);
    return slot ? &slot : nullptr;
}

// Character sprites (.CHD): full-screen canvases cropped to their visible part.
SpriteTex* loadSprite(const std::string& name) {
    auto key = lower(name);
    auto cached = g_state->spriteTextures.find(key);
    if (cached != g_state->spriteTextures.end()) return &cached->second;
    SpriteTex out;
    auto it = g_state->gfxIndex.find(key + ".chd");
    if (it == g_state->gfxIndex.end()) it = g_state->gfxIndex.find("dummy00.chd");
    SnowRadish::Sprite sp;
    if (it != g_state->gfxIndex.end() && SnowRadish::decodeChd(SnowRadish::extractEntry(g_state->gfx, it->second), sp)) {
        SDL_Surface* s = SDL_CreateSurfaceFrom(int(sp.width), int(sp.height), SDL_PIXELFORMAT_RGBA32, sp.rgba.data(),
                                               int(sp.width * 4));
        out.img = textureFrom(s ? SDL_DuplicateSurface(s) : nullptr);
        if (s) SDL_DestroySurface(s);
        out.x = float(sp.x);
        out.y = float(sp.y);
    } else {
        SDL_Log("[gfx] missing sprite %s", name.c_str());
    }
    return &(g_state->spriteTextures[key] = std::move(out));
}

void drawTex(Kotonoha::CanvasImage* img, float x, float y) {
    if (!img || !*img) return;
    SDL_FRect r{x, y, 0, 0};
    SDL_GetTextureSize(img->Source(), &r.w, &r.h);
    img->Draw(global.render, nullptr, &r);
}

// Tint as in the original (0x100 neutral per channel): below it darkens, above
// it adds the extra amount on top.
void drawTinted(Kotonoha::CanvasImage* img, float x, float y, const int tint[3]) {
    SDL_Texture* t = img ? img->Source() : nullptr;
    if (!t) return;
    auto mod = [](int v) { return Uint8(std::clamp(v, 0, 255)); };
    SDL_SetTextureColorMod(t, mod(tint[0]), mod(tint[1]), mod(tint[2]));
    drawTex(img, x, y);
    if (tint[0] > 0x100 || tint[1] > 0x100 || tint[2] > 0x100) {
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
        SDL_SetTextureColorMod(t, mod(tint[0] - 0x100), mod(tint[1] - 0x100), mod(tint[2] - 0x100));
        drawTex(img, x, y);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    }
    SDL_SetTextureColorMod(t, 255, 255, 255);
}

class SnowSession : public Kotonoha::GameSession {
public:
    explicit SnowSession(State& s) : s_(s) {}

    bool Start() override {
        // Started from the title: its music (sr001) goes on, as the original never stops it.
        s_.resumeBgm = s_.muted || !playing(s_.bgm) ? "" : s_.lastBgm;
        if (s_.resumeBgm.empty()) stopChannel(s_.bgm);
        stopChannel(s_.se);
        stopChannel(s_.voice);
        s_.lastBgm.clear();
        booting_ = true;
        s_.game = std::make_unique<SnowRadish::Game>(s_.script);
        s_.game->sound = playSound;
        if (SDL_getenv("SNOW_TRACE")) s_.game->trace = [](const std::string& t) { SDL_Log("[vm] %s", t.c_str()); };
        s_.game->voicePlaying = [this] { return playing(s_.voice); };
        s_.game->sePlaying = [this] { return playing(s_.se); };
        // The boot script (white screen, logo, title) already ran in the front end.
        s_.game->transition = [this](int, int msec) {
            if (!booting_) BeginTransition(msec);
        };
        s_.game->title = [this] { booting_ = false; };
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
        for (const auto& sp : s_.sprites)
            drawTinted(&sp.pic->img, sp.pic->x + float(sp.dx), sp.pic->y + float(sp.dy), sp.tint);
    }

    // Snapshot of what is on screen now (the scene before the script's changes are
    // synced), faded out over the new scene.
    void BeginTransition(int msec) {
        fadeMs_ = 0;
        if (s_.muted) return;
        // The first wipe after the title starts from the title picture.
        if (keepFrom_) keepFrom_ = false;
        else if (!Capture([this](SDL_Renderer* r) { DrawScene(r); })) return;
        fadeStart_ = SDL_GetTicks();
        fadeMs_ = uint64_t(msec);
    }
    // Renders a picture into from_, the "before" image of a wipe.
    bool Capture(const std::function<void(SDL_Renderer*)>& draw) {
        SDL_Renderer* r = global.render;
        if (!from_) {
            from_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, kW, kH);
            if (!from_) return false;
            SDL_SetTextureBlendMode(from_, SDL_BLENDMODE_BLEND);
        }
        SDL_Texture* old = SDL_GetRenderTarget(r);
        SDL_SetRenderTarget(r, from_);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        draw(r);
        SDL_SetRenderTarget(r, old);
        return true;
    }
    bool Fading() const { return fadeMs_ && SDL_GetTicks() - fadeStart_ < fadeMs_; }

    // ---- Original front end (OF_ADV3.EXE boot script) ----
    // start.obj: white screen, native 221 (Overflow logo), wipe 64(1,0,0) to white,
    // BGM 112(1,1) = sr001 looped, native 213 (title menu).
    bool HasFrontend() const override { return true; }
    void EnterFrontend(bool boot) override {
        stopChannel(s_.voice);
        stopChannel(s_.se);
        hover_ = -1;
        action_.reset();
        titleSaves_ = Kotonoha::SaveStore("snow-radish-vacation").AnySave();
        boot_ = boot;
        if (boot) {
            StartLogo();
            return;
        }
        // Back from the story: the title wipes in over the last scene (213 with mode 1: speed 1).
        Capture([this](SDL_Renderer* r) { DrawScene(r); });
        playSound("bgm", "sr001", true);
        BeginPhase(Phase::TitleIn, kWipeTick * 256 / 8);
    }
    void FrontendEvent(const SDL_Event& e) override {
        const bool press = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_KEY_DOWN;
        switch (phase_) {
        case Phase::Logo:
            if (press) EndLogo(); // any input breaks the logo loop
            break;
        case Phase::White:
        case Phase::TitleIn:
            if (press) phaseMs_ = 0; // a click ends a wipe
            break;
        case Phase::TitleWait: break; // input is flushed after the 500 ms wait
        case Phase::Title: TitleEvent(e); break;
        }
    }
    std::optional<TitleAction> FrontendTick() override {
        const uint64_t now = SDL_GetTicks();
        if (phase_ == Phase::Logo) {
            // The logo loop runs one frame every 48 ms (timeGetTime).
            if (now - logoClock_ > 1000) logoClock_ = now - kLogoFrameMs;
            while (phase_ == Phase::Logo && now - logoClock_ >= kLogoFrameMs) {
                logoClock_ += kLogoFrameMs;
                LogoFrame();
            }
        } else if (phase_ != Phase::Title && now - phaseStart_ >= phaseMs_) {
            if (phase_ == Phase::White) {
                playSound("bgm", "sr001", true);
                Capture([](SDL_Renderer* r) { FillWhite(r); });
                BeginPhase(Phase::TitleIn, kWipeTick * 256 / 2); // 213 mode 0: wipe speed 2...
            } else if (phase_ == Phase::TitleIn && boot_) {
                BeginPhase(Phase::TitleWait, 500); // ...then a 500 ms wait
            } else {
                BeginPhase(Phase::Title, 0);
            }
        }
        auto a = action_;
        action_.reset();
        return a;
    }
    void DrawFrontend(SDL_Renderer* r) override {
        switch (phase_) {
        case Phase::Logo: DrawLogo(r); break;
        case Phase::White: FillWhite(r); break;
        case Phase::TitleIn: {
            DrawTitleScreen(-1);
            const float t = phaseMs_ ? float(SDL_GetTicks() - phaseStart_) / float(phaseMs_) : 1.0f;
            if (from_ && t < 1.0f) {
                SDL_SetTextureAlphaModFloat(from_, 1.0f - t);
                SDL_RenderTexture(r, from_, nullptr, nullptr);
            }
            break;
        }
        case Phase::TitleWait: DrawTitleScreen(-1); break;
        case Phase::Title: DrawTitleScreen(hover_); break;
        }
    }

private:
    enum class Phase { Logo, White, TitleIn, TitleWait, Title };
    static constexpr uint64_t kLogoFrameMs = 0x30;
    static constexpr int kWipeTick = 10; // ms per wipe step, as in native 64
    static constexpr int kFadeFrames = 60; // the logo fades step (max-min)/120*2 per frame
    static constexpr int kMaxFlakes[2] = {20, 40}, kFlakeSpeed[2] = {2, 1};

    struct Flake {
        int type = 0; // 0 snow09, 1 snow11
        bool alive = false;
        int x = 0, y = 0;
        const std::vector<int>* path = nullptr; // x offset per y (SLINE0n.SNW)
    };

    static void FillWhite(SDL_Renderer* r) {
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderFillRect(r, nullptr);
    }
    void BeginPhase(Phase p, int ms) {
        phase_ = p;
        phaseStart_ = SDL_GetTicks();
        phaseMs_ = uint64_t(std::max(0, ms));
    }

    // Native 221 (FUN_0043d327): two groups of falling snowflakes over a white screen;
    // after 10 "seconds" (30 frames each) the white fades to logo00.evd, the voice
    // V0310000 plays, from 15 the logo fades back to white, and at 20 the loop ends.
    void StartLogo() {
        LoadPaths();
        for (auto& g : flakes_)
            for (auto& f : g) Spawn(f);
        count_[0] = count_[1] = 1;
        frame_ = sec_ = fade_ = logoState_ = 0;
        voiced_ = false;
        logoClock_ = SDL_GetTicks();
        BeginPhase(Phase::Logo, 0);
    }
    void EndLogo() {
        stopChannel(s_.voice);
        BeginPhase(Phase::White, kWipeTick * 256 / 4); // 64(1,0,0): speed 0
    }
    void LoadPaths() {
        if (!paths_[0].empty()) return;
        for (int i = 0; i < 3; ++i) {
            auto it = s_.gfxIndex.find("sline0" + std::to_string(i + 1) + ".snw");
            if (it == s_.gfxIndex.end()) continue;
            const auto d = SnowRadish::extractEntry(s_.gfx, it->second);
            auto u32 = [&](size_t o) {
                return uint32_t(d[o]) | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24;
            };
            if (d.size() < 12) continue;
            const size_t n = std::min<size_t>(u32(8), (d.size() - 12) / 4);
            for (size_t k = 0; k < n; ++k) paths_[i].push_back(int32_t(u32(12 + k * 4)));
        }
    }
    // FUN_0043d013: random flake image, path (sline01..03 x3, then sline01) and start x.
    void Spawn(Flake& f) {
        static const int kPath[10] = {0, 1, 2, 0, 1, 2, 0, 1, 2, 0};
        f.type = SDL_rand(2);
        f.path = &paths_[kPath[SDL_rand(10)]];
        f.x = SDL_rand(800) - 400;
        f.y = 0;
        f.alive = !f.path->empty();
    }
    void LogoFrame() {
        if (sec_ > 9 && logoState_ == 0) fade_ = std::min(fade_ + 1, kFadeFrames);
        if (sec_ >= 15 && logoState_ == 1) fade_ = std::min(fade_ + 1, kFadeFrames);
        if (++frame_ > 29) {
            frame_ = 0;
            for (int g = 0; g < 2; ++g) count_[g] = std::min(count_[g] + 1, kMaxFlakes[g]);
            if (++sec_ == 20) return EndLogo();
        }
        for (int g = 0; g < 2; ++g)
            for (int i = 0; i < count_[g]; ++i) {
                Flake& f = flakes_[g][size_t(i)];
                f.y += kFlakeSpeed[g];
                if (!f.alive || f.y >= int(f.path->size())) Spawn(f);
            }
        if (fade_ >= kFadeFrames && logoState_ == 1) logoState_ = 2;
        if (fade_ >= kFadeFrames && logoState_ == 0) {
            logoState_ = 1;
            fade_ = 0;
            if (!voiced_) playSound("voice", "V0310000", false);
            voiced_ = true;
        }
    }
    void DrawLogo(SDL_Renderer* r) {
        Kotonoha::CanvasImage* logo = loadGraphic("logo00", ".evd");
        const float t = float(fade_) / float(kFadeFrames);
        FillWhite(r);
        float logoAlpha = 0.0f;
        if (logoState_ == 0 && sec_ >= 10) logoAlpha = t;
        else if (logoState_ == 1) logoAlpha = sec_ >= 15 ? 1.0f - t : 1.0f;
        if (logo && logoAlpha > 0.0f) {
            SDL_SetTextureAlphaModFloat(logo->Source(), logoAlpha);
            drawTex(logo, 0, 0);
            SDL_SetTextureAlphaModFloat(logo->Source(), 1.0f);
        }
        // FUN_0043d155: drawn at x + path[y], shifted per image, while on screen.
        for (int g = 0; g < 2; ++g)
            for (int i = 0; i < count_[g]; ++i) {
                const Flake& f = flakes_[g][size_t(i)];
                if (!f.alive || f.y >= int(f.path->size())) continue;
                int x = f.x + (*f.path)[size_t(f.y)], y = f.y - 10;
                if (f.type == 0) x -= 4, y = f.y - 14;
                else x -= 5, y -= 5;
                if (x >= kW || y >= kH) continue;
                SpriteTex* pic = loadSprite(f.type == 0 ? "snow09" : "snow11");
                if (pic) drawTex(&pic->img, pic->x + float(x), pic->y + float(y));
            }
    }

    // Native 213 (FUN_004403de): T00b/T00c with the hovered button T0nb/T0nc at x=0x144.
    std::vector<TitleItem> Items() const {
        std::vector<TitleItem> v;
        if (titleSaves_) {
            v.push_back({{0x144, 0x159, 0xa5, 0x31}, TitleAction::NewGame});
            v.push_back({{0x144, 0x19f, 0xa5, 0x31}, TitleAction::Load});
            v.push_back({{0x144, 0x1e4, 0xa5, 0x31}, TitleAction::Quit});
        } else {
            v.push_back({{0x144, 0x170, 0xa5, 0x31}, TitleAction::NewGame});
            v.push_back({{0x144, 0x1b5, 0xa5, 0x31}, TitleAction::Quit});
        }
        return v;
    }
    void DrawTitleScreen(int hover) {
        const char suffix = titleSaves_ ? 'c' : 'b';
        drawTex(loadGraphic(std::string("T00") + suffix, ".evd"), 0, 0);
        const auto items = Items();
        if (hover < 0 || size_t(hover) >= items.size()) return;
        const std::string button = "T0" + std::to_string(hover + 1) + suffix;
        drawTex(loadGraphic(button, ".img"), items[size_t(hover)].rect.x, items[size_t(hover)].rect.y);
    }
    // Mouse hover picks the button; Up/Down move between buttons; a left click or Enter
    // on a button selects it (right click / Esc do nothing).
    void TitleEvent(const SDL_Event& e) {
        const auto items = Items();
        const int n = int(items.size());
        if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            const SDL_FPoint pt = e.type == SDL_EVENT_MOUSE_MOTION ? SDL_FPoint{e.motion.x, e.motion.y}
                                                                   : SDL_FPoint{e.button.x, e.button.y};
            hover_ = -1;
            for (int i = 0; i < n; ++i)
                if (SDL_PointInRectFloat(&pt, &items[size_t(i)].rect)) hover_ = i;
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT && hover_ >= 0)
                Select(items[size_t(hover_)].action);
        } else if (e.type == SDL_EVENT_KEY_DOWN) {
            if (e.key.key == SDLK_UP) hover_ = hover_ <= 0 ? n - 1 : hover_ - 1;
            else if (e.key.key == SDLK_DOWN) hover_ = hover_ < 0 || hover_ >= n - 1 ? 0 : hover_ + 1;
            else if ((e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) && hover_ >= 0)
                Select(items[size_t(hover_)].action);
        }
    }
    void Select(TitleAction a) {
        if (a == TitleAction::NewGame) {
            const int h = hover_;
            keepFrom_ = Capture([this, h](SDL_Renderer*) { DrawTitleScreen(h); });
        }
        action_ = a;
    }

    SDL_Texture* from_ = nullptr;
    uint64_t fadeStart_ = 0, fadeMs_ = 0;
    bool keepFrom_ = false, booting_ = false;
    State& s_;

    Phase phase_ = Phase::Logo;
    uint64_t phaseStart_ = 0, phaseMs_ = 0, logoClock_ = 0;
    bool boot_ = false, voiced_ = false, titleSaves_ = false;
    int hover_ = -1;
    std::optional<TitleAction> action_;
    std::vector<int> paths_[3];
    Flake flakes_[2][40];
    int count_[2] = {1, 1};
    int frame_ = 0, sec_ = 0, fade_ = 0, logoState_ = 0;
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
