// Snow Radish Vacation!! (Overflow ADV3) running on the Kotonoha engine.
// Usage: SnowRadish --snow <dir with SCRIPT.DAT and GRAPHIC.DAT> [--mcp [port]]
#include "SnowRadish/Game.hpp"

#include <Kotonoha/Main.hpp>
#include <Kotonoha/components/CanvasImage.hpp>
#include <Kotonoha/components/GameplayFlow.hpp>
#include <Kotonoha/components/PcmPlayer.hpp>
#include <Kotonoha/components/Panel.hpp>
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

struct AnimatedTex {
    std::vector<SpriteTex> frames;
    std::vector<SnowRadish::Animation::Region> regions;
    bool cursor = false;
    int shownFrame = -1;
};

struct State {
    std::unique_ptr<SnowRadish::Game> game;
    std::vector<uint8_t> soundPack, voicePack;
    std::map<std::string, SnowRadish::PackEntry> soundIndex, voiceIndex;
    Channel voice{{}, "Voice"}, bgm{{}, "BGM"}, se{{}, "Se"};
    std::string lastBgm;
    std::vector<int16_t> voiceSamples;
    uint64_t voiceStart = 0;
    int voiceActor = 0;
    bool lastBgmLoop = true;
    // The title music keeps playing into the story when the boot script asks for it again.
    std::string resumeBgm;
    bool muted = false; // set while replaying a save
    std::vector<uint8_t> script;
    std::vector<uint8_t> gfx;
    std::map<std::string, SnowRadish::PackEntry> gfxIndex;
    std::map<std::string, Kotonoha::CanvasImage> textures;
    std::map<std::string, SpriteTex> spriteTextures;
    std::map<std::string, AnimatedTex> animations;
    Kotonoha::CanvasImage* bg = nullptr;
    std::string backgroundName;
    bool filled = false;
    uint32_t fillColor = 0;
    struct Shown {
        SpriteTex* pic = nullptr;
        std::string name;
        int dx = 0, dy = 0, id = 0;
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
        s.voiceSamples = pcm.samples;
        s.voiceStart = SDL_GetTicks();
        s.voiceActor = s.game ? s.game->voiceActor() : 0;
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

AnimatedTex* loadAnimation(const std::string& name) {
    const auto key = lower(name);
    auto cached = g_state->animations.find(key);
    if (cached != g_state->animations.end()) return &cached->second;
    AnimatedTex out;
    auto it = g_state->gfxIndex.find(key + ".anm");
    SnowRadish::Animation anm;
    if (it != g_state->gfxIndex.end() && SnowRadish::decodeAnm(SnowRadish::extractEntry(g_state->gfx, it->second), anm)) {
        out.regions = std::move(anm.regions);
        out.cursor = anm.cursor;
        for (auto& frame : anm.frames) {
            SDL_Surface* surface = SDL_CreateSurfaceFrom(int(frame.width), int(frame.height), SDL_PIXELFORMAT_RGBA32,
                                                        frame.rgba.data(), int(frame.width * 4));
            SpriteTex tex;
            tex.img = textureFrom(surface ? SDL_DuplicateSurface(surface) : nullptr);
            if (surface) SDL_DestroySurface(surface);
            tex.x = float(frame.x); tex.y = float(frame.y);
            out.frames.push_back(std::move(tex));
        }
    }
    return &(g_state->animations[key] = std::move(out));
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
    explicit SnowSession(State& s) : s_(s) {
        char* pref = SDL_GetPrefPath("Kotonoha", "snow-radish-vacation");
        if (pref) { systemPath_ = std::filesystem::path(pref) / "system.dat"; SDL_free(pref); }
        const auto system = readFile(systemPath_);
        cleared_ = system == std::vector<uint8_t>{83,82,86,1,1};
    }
    ~SnowSession() override {
        if (from_) SDL_DestroyTexture(from_);
        if (wipeTexture_) SDL_DestroyTexture(wipeTexture_);
        if (to_) SDL_DestroyTexture(to_);
    }

    bool Start() override {
        // Started from the title: its music (sr001) goes on, as the original never stops it.
        s_.resumeBgm = s_.muted || !playing(s_.bgm) ? "" : s_.lastBgm;
        if (s_.resumeBgm.empty()) stopChannel(s_.bgm);
        stopChannel(s_.se);
        stopChannel(s_.voice);
        s_.lastBgm.clear();
        booting_ = true;
        ended_ = false;
        auto_ = false;
        fadeMs_ = 0;
        s_.game = std::make_unique<SnowRadish::Game>(s_.script);
        s_.game->sound = playSound;
        if (SDL_getenv("SNOW_TRACE")) s_.game->trace = [](const std::string& t) { SDL_Log("[vm] %s", t.c_str()); };
        s_.game->voicePlaying = [this] { return playing(s_.voice); };
        s_.game->sePlaying = [this] { return playing(s_.se); };
        // The boot script (white screen, logo, title) already ran in the front end.
        s_.game->transition = [this](int type, int pattern, int speed) {
            if (!booting_) BeginTransition(type, pattern, speed);
        };
        s_.game->title = [this] {
            if (booting_) booting_ = false;
            else { ended_ = true; s_.game->vm().block(); }
        };
        s_.game->cleared = [this](bool flag) {
            if (s_.muted || cleared_ == flag) return;
            const char system[] = {83,82,86,1,char(flag)};
            std::ofstream out(systemPath_, std::ios::binary | std::ios::trunc);
            out.write(system, sizeof system);
            out.flush();
            if (out) cleared_ = flag;
            else SDL_Log("[system] could not persist cleared flag: %s", systemPath_.string().c_str());
        };
        s_.game->transitionPlaying = [this] { return Fading(); };
        if (!s_.game->init()) return false;
        s_.game->setClearFlag(cleared_);
        s_.game->pump();
        s_.seenVersion = ~0u;
        return true;
    }
    void Step(int input) override {
        auto& g = *s_.game;
        if (g.choosing()) {
            if (input < 0) return;
            g.choose(size_t(input));
            mapHover_ = -1;
        } else {
            g.click();
        }
        g.pump();
    }
    bool Skip() override {
        auto& g = *s_.game;
        if (!g.waitingForAudio()) return false;
        if (!g.skipAudioWait()) return false;
        fadeMs_ = 0;
        return true;
    }
    void Idle() override {
        auto& g = *s_.game;
        if (!g.waitingForClick() && !g.choosing() && !g.finished()) g.pump();
    }
    bool Ended() const override { return ended_ || (s_.game && s_.game->finished()); }
    bool Choosing() const override { return s_.game->choosing(); }
    std::vector<std::string> Choices() const override { return s_.game->choices(); }
    int AutoDelayMs() const override { return 2000; }
    bool VoiceActive() const override { return playing(s_.voice); }
    void AutoChanged(bool enabled) override { auto_ = enabled; }
    bool HasMessage() const override { return s_.game->hasMessage(); }
    std::string Speaker() const override { return s_.game->message().name; }
    std::string Text() const override { return s_.game->message().text; }

    void Sync() override {
        auto& g = *s_.game;
        if (g.sceneVersion() == s_.seenVersion) return;
        s_.seenVersion = g.sceneVersion();
        s_.filled = g.filled();
        s_.fillColor = g.fillColor();
        s_.backgroundName = g.background();
        s_.bg = g.background().empty() ? nullptr : loadGraphic(g.background(), ".evd");
        s_.sprites.clear();
        for (const auto& sp : g.sprites()) {
            State::Shown v;
            v.name = sp.name;
            v.id = sp.id;
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
        } else {
            s_.game->setClearFlag(cleared_);
            if (!s_.lastBgm.empty()) playSound("bgm", s_.lastBgm, s_.lastBgmLoop);
        }
    }
    void Draw(SDL_Renderer* r) override {
        DrawScene(r);
        DrawWipe(r);
    }
    void DrawScene(SDL_Renderer* r) {
        if (s_.filled) {
            SDL_SetRenderDrawColor(r, Uint8(s_.fillColor >> 16), Uint8(s_.fillColor >> 8), Uint8(s_.fillColor), 255);
            SDL_RenderFillRect(r, nullptr);
        }
        if (s_.bg) drawTex(s_.bg, 0, 0);
        if (s_.game && !s_.filled) DrawAnimation(s_.backgroundName, 0, 0, nullptr);
        for (const auto& sp : s_.sprites) {
            float dx = float(sp.dx);
            if (fadeType_ == 11 && Fading() && sp.id == 1)
                dx += 256.0f * (1.0f - WipeProgress());
            drawTinted(&sp.pic->img, sp.pic->x + dx, sp.pic->y + float(sp.dy), sp.tint);
            DrawAnimation(sp.name, dx, float(sp.dy), sp.tint);
        }
    }

    // Snapshot of what is on screen now (the scene before the script's changes are
    // synced), faded out over the new scene.
    void BeginTransition(int type, int pattern, int speed) {
        static constexpr int steps[] = {4, 8, 2, 16, 1};
        fadeType_ = type; fadePattern_ = pattern;
        if (speed == 5 || type == 4) { fadeMs_ = 0; keepFrom_ = false; return; }
        maskEdge_ = (pattern & 0xff00) == 0x100 ? 64 : (pattern & 0xff00) == 0x200 ? 32 : 128;
        const int range = type == 2 || type == 3 ? 256 + maskEdge_ : type == 5 ? (pattern == 6 || pattern == 7 ? 1400 : 800) : 256;
        fadeRange_ = range;
        fadeStep_ = steps[std::clamp(speed, 0, 4)];
        const int msec = ((range + fadeStep_ - 1) / fadeStep_) * kWipeTick;
        wipeMask_ = {};
        auto mask = [&](std::string name, bool alternate, SnowRadish::AlphaMask& out) {
            auto it = s_.gfxIndex.find(lower(name));
            return it != s_.gfxIndex.end() && SnowRadish::decodeAlpha(SnowRadish::extractEntry(s_.gfx, it->second), alternate, out);
        };
        if (type == 2) {
            static const char* names[] = {"hart200", "maru200", "sima200", "star200", "suri200", "suri200r", "tako200"};
            const int index = pattern & 255;
            std::string name;
            if (index < 7) name = names[index];
            else if (index < 49) { char n[24]; std::snprintf(n, sizeof n, "pm%02d200", index - 7); name = n; }
            else if (index < 51) name = index == 49 ? "ap00200" : "ap01200";
            if (!name.empty()) mask(name + ".alpf", false, wipeMask_);
        } else if (type == 3) {
            mask("asabase.alpf", false, wipeMask_);
            mask("asaflash.alpa", true, flashMask_);
        }

        fadeMs_ = 0;
        if (s_.muted) return;
        // The first wipe after the title starts from the title picture.
        if (keepFrom_) keepFrom_ = false;
        else if (!Capture([this](SDL_Renderer* r) { DrawScene(r); })) return;
        if (!wipeMask_.alpha.empty()) {
            SDL_Texture* old = SDL_GetRenderTarget(global.render);
            SDL_SetRenderTarget(global.render, from_);
            SDL_Surface* pixels = SDL_RenderReadPixels(global.render, nullptr);
            SDL_SetRenderTarget(global.render, old);
            SDL_Surface* rgba = pixels ? SDL_ConvertSurface(pixels, SDL_PIXELFORMAT_RGBA32) : nullptr;
            if (pixels) SDL_DestroySurface(pixels);
            fromPixels_.clear();
            if (rgba) {
                fromPixels_.resize(kW * kH * 4);
                for (int y = 0; y < kH; ++y)
                    std::copy_n(static_cast<uint8_t*>(rgba->pixels) + y * rgba->pitch, kW * 4, fromPixels_.data() + y * kW * 4);
                SDL_DestroySurface(rgba);
            }
            if (!wipeTexture_) {
                wipeTexture_ = SDL_CreateTexture(global.render, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, kW, kH);
                if (wipeTexture_) SDL_SetTextureBlendMode(wipeTexture_, SDL_BLENDMODE_BLEND);
            }
        }
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
    float WipeProgress() const {
        return std::min(float((SDL_GetTicks() - fadeStart_) / kWipeTick) * fadeStep_ / fadeRange_, 1.0f);
    }

    void DrawAnimation(const std::string& name, float dx, float dy, const int* tint) {
        auto* animation = loadAnimation(name);
        if (!animation || animation->cursor || !playing(s_.voice)) return;
        // FUN_0040bf41 runs the mouth update every seventh 17 ms scheduler call.
        static constexpr uint8_t quiet[] = {1,1,2,0,1,1,0,2,1,1,1,0,2,1,1,0};
        static constexpr uint8_t loud[] = {2,0,2,1,2,1,0,2,0,1,2,1,2,0,2,1};
        const uint64_t elapsed = SDL_GetTicks() - s_.voiceStart;
        const size_t tick = size_t(elapsed / 119);
        const size_t sample = size_t((tick * 119) * kSampleRate / 1000);
        const int amplitude = sample < s_.voiceSamples.size() ? std::abs(int(s_.voiceSamples[sample])) : 0;
        const int frame = amplitude <= 64 ? 0 : amplitude > 16377 ? loud[(tick + 1) & 15] : quiet[(tick + 1) & 15];
        for (const auto& region : animation->regions) {
            if (region.actor != uint32_t(s_.voiceActor)) continue;
            if (SDL_getenv("SNOW_TRACE") && animation->shownFrame != frame) {
                SDL_Log("[anm] %s actor %u frame %d", name.c_str(), region.actor, frame);
                animation->shownFrame = frame;
            }
            auto& pic = animation->frames[region.first + size_t(frame)];
            if (tint) drawTinted(&pic.img, pic.x + dx, pic.y + dy, tint);
            else drawTex(&pic.img, pic.x + dx, pic.y + dy);
        }
    }
    void DrawWipe(SDL_Renderer* r) {
        if (!Fading() || !from_) return;
        const float t = WipeProgress();
        if (!wipeMask_.alpha.empty() && fromPixels_.size() == kW * kH * 4 && wipeTexture_) {
            const int phase = int(t * float(256 + maskEdge_)) - 256;
            for (int y = 0; y < kH; ++y) for (int x = 0; x < kW; ++x) {
                int mx = x / 4, my = (kH - 1 - y) / 4;
                if ((fadePattern_ & 0xff00) == 0x400) mx = 199 - mx;
                int alpha = wipeMask_.alpha[size_t(my) * 200 + mx];
                if ((fadePattern_ & 0xff00) == 0x800) alpha = uint8_t(128 - alpha);
                if (fadeType_ == 3 && !flashMask_.alpha.empty()) {
                    const size_t flash = size_t((phase + 256) / 8) * 249 + size_t(my) * 250 + mx + 47;
                    alpha = std::min(255, alpha + flashMask_.alpha[flash]);
                }
                const int reveal = std::clamp(alpha + phase, 0, maskEdge_) * 255 / maskEdge_;
                fromPixels_[(size_t(y) * kW + x) * 4 + 3] = uint8_t(255 - reveal);
            }
            SDL_UpdateTexture(wipeTexture_, nullptr, fromPixels_.data(), kW * 4);
            SDL_RenderTexture(r, wipeTexture_, nullptr, nullptr);
        } else if (fadeType_ == 5) {
            if (!to_) to_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, kW, kH);
            if (!to_) return;
            if (toClock_ != fadeStart_) {
                auto* target = SDL_GetRenderTarget(r);
                SDL_SetRenderTarget(r, to_);
                SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
                SDL_RenderClear(r);
                DrawScene(r);
                SDL_SetRenderTarget(r, target);
                toClock_ = fadeStart_;
            }
            SDL_SetTextureAlphaMod(from_, 255);
            const float progress = t * kW, remaining = kW - progress;
            auto part = [&](SDL_Texture* texture, float sourceX, float sourceW, float destX, float destW) {
                if (sourceW <= 0 || destW <= 0) return;
                SDL_FRect source{sourceX, 0, sourceW, kH}, dest{destX, 0, destW, kH};
                SDL_RenderTexture(r, texture, &source, &dest);
            };
            part(from_, 0, kW, 0, kW);
            auto row = [&](SDL_Texture* texture, float sx, float sw, float dx, float dw, int y) {
                if (sw <= 0 || dw <= 0) return;
                SDL_FRect source{sx, float(y), sw, 1}, dest{dx, float(y), dw, 1};
                SDL_RenderTexture(r, texture, &source, &dest);
            };
            switch (fadePattern_) {
            case 0: part(from_, progress, remaining, 0, remaining); part(to_, 0, progress, remaining, progress); break;
            case 1: part(from_, 0, kW, 0, kW); break;
            case 2: part(from_, 0, remaining, 0, remaining); part(to_, 0, progress, remaining, progress); break;
            case 3: part(to_, remaining, progress, 0, progress); part(from_, progress, remaining, progress, remaining); break;
            case 4: {
                part(from_, 0, kW, 0, remaining);
                part(to_, 0, kW, remaining, progress);
                // FUN_00417450 builds byte ramps, then applies SubSaturation.
                SDL_BlendMode previous;
                SDL_GetRenderDrawBlendMode(r, &previous);
                const auto subtract = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE,
                    SDL_BLENDOPERATION_REV_SUBTRACT, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
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
            case 5: part(to_, 0, kW, 0, progress); part(from_, 0, kW, progress, remaining); break;
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
            SDL_RenderTexture(r, from_, nullptr, nullptr);
        }
    }
    void DrawMessageWindow(SDL_Renderer* renderer) override {
        Kotonoha::DrawPanel(renderer, {20, 440, kW - 40, 150});
        if (!HasMessage() || Choosing() || Fading()) return;
        auto* animation = loadAnimation(auto_ ? "cursor02" : "cursor01");
        if (animation->frames.empty()) return;
        auto& pic = animation->frames[(SDL_GetTicks() / 170) % animation->frames.size()];
        drawTex(&pic.img, 715 + pic.x, 555 + pic.y);
    }
    bool OwnsChoices() const override { return s_.game && s_.game->mapChoosing(); }
    SDL_FRect Hotspot(size_t index) {
        char name[24]; std::snprintf(name, sizeof name, "map%04d", s_.game->destination(index));
        auto* pic = loadSprite(name);
        float w = 0, h = 0;
        if (pic->img) SDL_GetTextureSize(pic->img.Source(), &w, &h);
        return {pic->x, pic->y, w, h};
    }
    int ChoiceEvent(const SDL_Event& e) override {
        const int n = int(s_.game->choices().size());
        if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            SDL_FPoint p = e.type == SDL_EVENT_MOUSE_MOTION ? SDL_FPoint{e.motion.x, e.motion.y} : SDL_FPoint{e.button.x, e.button.y};
            mapHover_ = -1;
            for (int i = 0; i < n; ++i) { const auto rect = Hotspot(size_t(i)); if (SDL_PointInRectFloat(&p, &rect)) mapHover_ = i; }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) return PickMap(mapHover_);
        } else if (e.type == SDL_EVENT_KEY_DOWN && n) {
            if (e.key.key == SDLK_UP || e.key.key == SDLK_LEFT) mapHover_ = mapHover_ <= 0 ? n - 1 : mapHover_ - 1;
            else if (e.key.key == SDLK_DOWN || e.key.key == SDLK_RIGHT) mapHover_ = (mapHover_ + 1) % n;
            else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) return PickMap(mapHover_);
        }
        return -1;
    }
    int PickMap(int index) {
        if (index >= 0 && size_t(index) < s_.game->choices().size())
            keepFrom_ = Capture([this](SDL_Renderer* r) { DrawScene(r); DrawChoices(r); });
        return index;
    }
    void DrawChoices(SDL_Renderer*) override {
        if (!OwnsChoices()) return;
        const auto& args = s_.game->mapArgs();
        const int minutes = args.size() > 7 ? args[7] : 720;
        const int hour = minutes / 60 % 24;
        auto sprite = [](const std::string& name, float dx = 0, float dy = 0) { auto* pic = loadSprite(name); drawTex(&pic->img, pic->x + dx, pic->y + dy); };
        sprite(hour < 12 ? "map_am" : "map_pm");
        sprite("map_d" + std::to_string(std::clamp(minutes / 1440, 0, 9)), 454, 310);
        int h = hour % 12; if (!h) h = 12;
        if (h >= 10) sprite("map_t1", 635, 311);
        sprite("map_t" + std::to_string(h % 10), 658, 311);
        sprite("map_t" + std::to_string(minutes % 60 / 10), 707, 311);
        sprite("map_t" + std::to_string(minutes % 10), 730, 311);
        const bool hovered = mapHover_ >= 0 && size_t(mapHover_) < s_.game->choices().size();
        char name[24];
        if (hovered) {
            std::snprintf(name, sizeof name, "map%04d", s_.game->destination(size_t(mapHover_)));
            sprite(name);
        }
        for (size_t i = 0; i < s_.game->choices().size(); ++i) {
            const int dest = s_.game->destination(i);
            const auto rect = Hotspot(i);
            static const char* actors[] = {"meg", "alc", "asg", "tme", "htk"};
            static constexpr uint32_t affection[] = {0x312e, 0x3131, 0x312f, 0x3130, 0x3132};
            for (size_t a = 0; a < 5 && a < args.size(); ++a) {
                if (args[a] != dest) continue;
                sprite(std::string("map_") + actors[a] + (mapHover_ == int(i) ? "1" : "0"), rect.x - 6, rect.y - 51);
                if (mapHover_ == int(i)) {
                    const int hearts = std::clamp(int32_t(s_.game->vm().var(affection[a])), 0, 19) / 4;
                    if (hearts) sprite("map_h0" + std::to_string(hearts), rect.x + 69, rect.y + 56);
                }
            }
        }
        if (mapHover_ < 0 || size_t(mapHover_) >= s_.game->choices().size()) return;
        const int dest = s_.game->destination(size_t(mapHover_));
        static constexpr int bg[] = {0,8,7,4,3,9,6,10,5,2,1};
        const int hhmm = hour * 100 + minutes % 60;
        char suffix = hour < 5 || hour >= 18 ? 'e' : hour >= 15 ? 'c' : 'b';
        const int day = minutes / 1440;
        if ((dest == 10 && suffix == 'e') || ((day == 1 || day == 2) && dest == 2 && hhmm > 129 && hhmm < 431) ||
            ((day == 1 ? hhmm > 329 : day == 2 && hhmm > 129) && dest == 5 && hhmm < 431)) suffix = 'd';
        std::snprintf(name, sizeof name, "bg%02d%c", bg[dest], suffix);
        drawTex(loadGraphic(name, ".img"), 464, 364);
    }

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
    static constexpr int kWipeTick = 15; // GrpWipeWait default at 0x458af4
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

    // Native 213 (FUN_004403de): T00/T00a after clearing, T00b/T00c otherwise.
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
        const std::string suffix = SnowRadish::titleSuffix(cleared_, titleSaves_);
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
    SDL_Texture* wipeTexture_ = nullptr;
    SDL_Texture* to_ = nullptr;
    uint64_t toClock_ = ~uint64_t(0);
    std::vector<uint8_t> fromPixels_;
    SnowRadish::AlphaMask wipeMask_, flashMask_;
    int fadeType_ = 1, fadePattern_ = 0, maskEdge_ = 128, fadeRange_ = 256, fadeStep_ = 4;
    int mapHover_ = -1;
    uint64_t fadeStart_ = 0, fadeMs_ = 0;
    bool keepFrom_ = false, booting_ = false, ended_ = false, cleared_ = false, auto_ = false;
    std::filesystem::path systemPath_;
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
    cfg.textBox.frame = false;
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
