#include "DdlcSession.hpp"
#include "Ddlc/Runtime.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace Ddlc;

namespace {
constexpr float kW = 1280, kH = 720;

bool headerSize(const std::vector<uint8_t>& d, int& w, int& h) {
    auto be16 = [&](size_t i) { return int(d[i]) << 8 | d[i + 1]; };
    auto be32 = [&](size_t i) { return int(d[i]) << 24 | int(d[i + 1]) << 16 | int(d[i + 2]) << 8 | d[i + 3]; };
    if (d.size() > 24 && d[0] == 0x89 && d[1] == 'P') {
        w = be32(16), h = be32(20);
        return true;
    }
    if (d.size() > 4 && d[0] == 0xFF && d[1] == 0xD8) {
        size_t i = 2;
        while (i + 9 < d.size()) {
            if (d[i] != 0xFF) {
                ++i;
                continue;
            }
            const uint8_t m = d[i + 1];
            if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7) || m == 0xFF) {
                ++i;
                continue;
            }
            if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
                h = be16(i + 5), w = be16(i + 7);
                return true;
            }
            i += 2 + size_t(be16(i + 2));
        }
    }
    return false;
}

struct Pcm {
    std::vector<int16_t> samples; // stereo
    int rate = 44100;
    double seconds() const { return rate ? double(samples.size() / 2) / rate : 0; }
};

struct AudioSpec {
    std::string file;
    double from = 0, loop = -1;
};
AudioSpec parseSpec(const std::string& spec) {
    AudioSpec a;
    a.file = spec;
    if (!spec.empty() && spec[0] == '<') {
        const size_t e = spec.find('>');
        if (e != std::string::npos) {
            std::istringstream in(spec.substr(1, e - 1));
            std::string k;
            double v = 0;
            while (in >> k >> v) {
                if (k == "from") a.from = v;
                else if (k == "loop") a.loop = v;
            }
            a.file = spec.substr(e + 1);
        }
    }
    return a;
}

double realNow() { return double(SDL_GetTicksNS()) / 1e9; }

// ---- text -------------------------------------------------------------------

SDL_Color sdlColor(uint32_t c) { return {Uint8(c >> 24), Uint8(c >> 16), Uint8(c >> 8), Uint8(c)}; }

uint32_t utf8Code(const std::string& s) {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    if (s.empty()) return 0;
    if (p[0] < 0x80) return p[0];
    if (p[0] < 0xE0 && s.size() >= 2) return (p[0] & 0x1F) << 6 | (p[1] & 0x3F);
    if (p[0] < 0xF0 && s.size() >= 3) return (p[0] & 0x0F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F);
    if (s.size() >= 4) return (p[0] & 0x07) << 18 | (p[1] & 0x3F) << 12 | (p[2] & 0x3F) << 6 | (p[3] & 0x3F);
    return '?';
}

// Laid-out text: glyph positions in style pixels (unscaled).
struct Laid {
    RichText rich;
    std::vector<float> x;
    std::vector<int> line;
    float w = 0, h = 0, lineH = 0;
};

struct TextTex {
    SDL_Texture* tex = nullptr;
    float pad = 0;    // texture pixels around the layout box (outlines)
    float scale = 1;  // texture pixels per style pixel
    float w = 0, h = 0;
    uint64_t used = 0;
};

class TextEngine {
public:
    const GameAssets* assets = nullptr;
    uint64_t frame = 0;

    ~TextEngine() {
        clear();
        for (auto& [_, f] : fonts_) TTF_CloseFont(f);
    }
    void clear() {
        for (auto& [_, t] : cache_) SDL_DestroyTexture(t.tex);
        cache_.clear();
    }

    TTF_Font* font(const std::string& file, int px, int outline, int style) {
        auto key = std::make_tuple(file, px, outline, style);
        auto it = fonts_.find(key);
        if (it != fonts_.end()) return it->second;
        auto& data = data_[file];
        if (data.empty() && assets->contains(file)) data = assets->read(file);
        if (data.empty() && assets->contains("gui/font/Aller_Rg.ttf"))
            data = assets->read("gui/font/Aller_Rg.ttf");
        TTF_Font* f = data.empty() ? nullptr
                                   : TTF_OpenFontIO(SDL_IOFromConstMem(data.data(), data.size()), true, float(px));
        if (f) {
            if (outline > 0) TTF_SetFontOutline(f, outline);
            if (style) TTF_SetFontStyle(f, TTF_FontStyleFlags(style));
        } else {
            SDL_Log("DDLC: font %s: %s", file.c_str(), SDL_GetError());
        }
        fonts_[key] = f;
        return f;
    }

    static int styleBits(const Glyph& g) { return (g.bold ? TTF_STYLE_BOLD : 0) | (g.italic ? TTF_STYLE_ITALIC : 0); }

    float advance(TTF_Font* f, const std::string& ch) {
        if (!f) return 0;
        const uint32_t cp = utf8Code(ch);
        auto key = std::make_pair(f, cp);
        auto it = adv_.find(key);
        if (it != adv_.end()) return it->second;
        int a = 0;
        if (!TTF_GetGlyphMetrics(f, cp, nullptr, nullptr, nullptr, nullptr, &a)) a = 0;
        return adv_[key] = float(a);
    }

    Laid layout(const RichText& rich, const TextStyle& st) {
        Laid L;
        L.rich = rich;
        const int px = std::max(1, int(std::lround(st.size)));
        TTF_Font* base = font(st.font, px, 0, 0);
        const float fh = base ? float(TTF_GetFontHeight(base)) : st.size;
        L.lineH = fh + st.lineSpacing;
        const auto& gs = rich.glyphs;
        L.x.assign(gs.size(), 0);
        L.line.assign(gs.size(), 0);
        std::vector<float> lineW;
        float x = 0;
        int line = 0;
        long lastSpace = -1;
        size_t lineStart = 0;
        for (size_t i = 0; i < gs.size(); ++i) {
            const Glyph& g = gs[i];
            if (g.ch == "\n") {
                L.x[i] = x, L.line[i] = line;
                lineW.push_back(x);
                ++line, x = 0, lastSpace = -1, lineStart = i + 1;
                continue;
            }
            x += g.space;
            const float a = advance(font(st.font, px, 0, styleBits(g)), g.ch);
            if (st.maxWidth > 0 && x + a > st.maxWidth && g.ch != " " && lastSpace >= long(lineStart)) {
                lineW.push_back(L.x[size_t(lastSpace)]);
                ++line;
                const size_t first = size_t(lastSpace) + 1;
                const float shift = first < i ? L.x[first] : x;
                for (size_t j = first; j < i; ++j) L.x[j] -= shift, L.line[j] = line;
                x -= shift;
                lineStart = first;
                lastSpace = -1;
            }
            L.x[i] = x, L.line[i] = line;
            x += a;
            if (g.ch == " ") lastSpace = long(i);
        }
        lineW.push_back(x);
        for (float w : lineW) L.w = std::max(L.w, w);
        L.h = float(lineW.size()) * fh + float(lineW.size() - 1) * st.lineSpacing;
        if (st.align > 0)
            for (size_t i = 0; i < gs.size(); ++i) L.x[i] += (L.w - lineW[size_t(L.line[i])]) * st.align;
        return L;
    }

    void measure(const std::string& text, const TextStyle& st, float& w, float& h) {
        Laid L = layout(parseRich(text, 0), st);
        w = L.w, h = L.h;
    }

    // The texture of `visible` glyphs of the text at `scale` texture pixels per style pixel.
    const TextTex* get(SDL_Renderer* r, const RichText& rich, size_t visible, const TextStyle& st, float scale,
                       const std::string& id) {
        std::ostringstream k;
        k << id << '\x1f' << st.font << '|' << st.size << '|' << st.color << '|' << st.maxWidth << '|' << st.align
          << '|' << st.lineSpacing << '|' << visible << '|' << int(scale * 100);
        for (auto& o : st.outlines) k << '|' << o.first << ':' << o.second;
        const std::string key = k.str();
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            it->second.used = frame;
            return &it->second;
        }
        TextTex t = build(r, layout(rich, st), visible, st, scale);
        t.used = frame;
        return &(cache_[key] = t);
    }

    void evict() {
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (frame - it->second.used > 3) {
                SDL_DestroyTexture(it->second.tex);
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    // Non-premultiplied "over" of an RGBA32 surface onto the canvas.
    static void over(std::vector<uint8_t>& dst, int dw, int dh, SDL_Surface* src, int ox, int oy) {
        SDL_Surface* s = SDL_ConvertSurface(src, SDL_PIXELFORMAT_RGBA32);
        if (!s) return;
        for (int y = 0; y < s->h; ++y) {
            const int dy = y + oy;
            if (dy < 0 || dy >= dh) continue;
            const auto* row = static_cast<const uint8_t*>(s->pixels) + size_t(y) * s->pitch;
            for (int x = 0; x < s->w; ++x) {
                const int dx = x + ox;
                if (dx < 0 || dx >= dw) continue;
                const uint8_t* p = row + x * 4;
                if (p[3] == 0) continue;
                uint8_t* q = dst.data() + (size_t(dy) * dw + dx) * 4;
                const float sa = p[3] / 255.f, da = q[3] / 255.f, oa = sa + da * (1 - sa);
                for (int c = 0; c < 3; ++c) q[c] = Uint8(std::lround((p[c] * sa + q[c] * da * (1 - sa)) / oa));
                q[3] = Uint8(std::lround(oa * 255));
            }
        }
        SDL_DestroySurface(s);
    }

    TextTex build(SDL_Renderer* r, const Laid& L, size_t visible, const TextStyle& st, float scale) {
        TextTex t;
        float maxOutline = 0;
        for (auto& o : st.outlines) maxOutline = std::max(maxOutline, o.first);
        t.scale = scale;
        t.pad = std::ceil(maxOutline * scale) + 2;
        const int W = std::max(1, int(std::ceil(L.w * scale + 2 * t.pad)));
        const int H = std::max(1, int(std::ceil(L.h * scale + 2 * t.pad)));
        t.w = float(W), t.h = float(H);
        std::vector<uint8_t> px(size_t(W) * H * 4, 0);
        const int fpx = std::max(1, int(std::lround(st.size * scale)));
        const auto& gs = L.rich.glyphs;
        const size_t n = std::min(visible, gs.size());
        auto layer = [&](int outlinePx, const uint32_t* outlineColor) {
            size_t i = 0;
            while (i < n) {
                if (gs[i].ch == "\n") {
                    ++i;
                    continue;
                }
                size_t j = i + 1;
                while (j < n && gs[j].ch != "\n" && L.line[j] == L.line[i] && gs[j].italic == gs[i].italic &&
                       gs[j].bold == gs[i].bold && gs[j].hasColor == gs[i].hasColor && gs[j].color == gs[i].color &&
                       gs[j].space == 0)
                    ++j;
                std::string s;
                for (size_t q = i; q < j; ++q) s += gs[q].ch;
                const uint32_t c = outlineColor ? *outlineColor : gs[i].hasColor ? gs[i].color : st.color;
                if (TTF_Font* f = font(st.font, fpx, outlinePx, styleBits(gs[i]))) {
                    if (SDL_Surface* surf = TTF_RenderText_Blended(f, s.data(), s.size(), sdlColor(c))) {
                        const int ox = int(std::lround(t.pad + L.x[i] * scale)) - outlinePx;
                        const int oy = int(std::lround(t.pad + float(L.line[i]) * L.lineH * scale)) - outlinePx;
                        over(px, W, H, surf, ox, oy);
                        SDL_DestroySurface(surf);
                    }
                }
                i = j;
            }
        };
        for (auto& o : st.outlines) {
            const int opx = std::max(1, int(std::lround(o.first * scale)));
            layer(opx, &o.second);
        }
        layer(0, nullptr);
        SDL_Surface* surf = SDL_CreateSurfaceFrom(W, H, SDL_PIXELFORMAT_RGBA32, px.data(), W * 4);
        t.tex = surf ? SDL_CreateTextureFromSurface(r, surf) : nullptr;
        if (surf) SDL_DestroySurface(surf);
        if (t.tex) {
            SDL_SetTextureBlendMode(t.tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t.tex, SDL_SCALEMODE_LINEAR);
        }
        return t;
    }

    std::map<std::string, std::vector<uint8_t>> data_;
    std::map<std::tuple<std::string, int, int, int>, TTF_Font*> fonts_;
    std::map<std::pair<TTF_Font*, uint32_t>, float> adv_;
    std::map<std::string, TextTex> cache_;
};


struct Channel {
    std::unique_ptr<Kotonoha::PcmPlayer> player;
    std::string spec, queued;
    bool queuedLoop = false, loop = false;
    AudioSpec parsed;
    double duration = 0;
    double fadeFrom = 1, fadeTo = 1, fadeStart = 0, fadeLen = 0;
};

struct Fading {
    std::unique_ptr<Kotonoha::PcmPlayer> player;
    double gain = 1, start = 0, len = 1;
};

class Host : public Backend {
public:
    Host(Kotonoha::Engine& engine, const GameAssets& assets, TextEngine& text)
        : engine_(engine), assets_(assets), text_(text) {
        directory_ = userDirectory();
        seedCharacters(directory_, assets.source().dir(), &assets.source());
        persistentPath_ = directory_ / "persistent.txt";
    }

    const std::filesystem::path& directory() const { return directory_; }

    // PauseAudio(channel): the channel keeps its position but is silent.
    void mute(const std::string& channel, bool on) {
        if (on) muted_.insert(channel);
        else muted_.erase(channel);
    }

    // The pointer as the script sees it (renpy.get_mouse_pos / set_mouse_pos).
    void mousePosition(double& x, double& y) override {
        std::lock_guard<std::mutex> lock(mouseMutex_);
        x = mouseX_;
        y = mouseY_;
    }
    void setMousePosition(double x, double y) override {
        std::lock_guard<std::mutex> lock(mouseMutex_);
        mouseX_ = x;
        mouseY_ = y;
        warp_ = true;
    }
    void trackMouse(double x, double y) {
        std::lock_guard<std::mutex> lock(mouseMutex_);
        mouseX_ = x;
        mouseY_ = y;
    }
    bool takeWarp(double& x, double& y) {
        std::lock_guard<std::mutex> lock(mouseMutex_);
        x = mouseX_;
        y = mouseY_;
        return std::exchange(warp_, false);
    }

    bool imageSize(const std::string& file, int& w, int& h) override {
        auto it = sizes_.find(file);
        if (it == sizes_.end()) {
            int iw = 0, ih = 0;
            try {
                if (!headerSize(assets_.read(file), iw, ih)) {
                    auto surface = assets_.loadSurface(file);
                    iw = surface->w;
                    ih = surface->h;
                }
            } catch (const std::exception& error) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "DDLC image %s: %s", file.c_str(), error.what());
            }
            it = sizes_.emplace(file, std::make_pair(iw, ih)).first;
        }
        w = it->second.first;
        h = it->second.second;
        return w > 0 && h > 0;
    }

    void textSize(const std::string& value, const TextStyle& style, float& w, float& h) override {
        text_.measure(value, style, w, h);
    }

    std::shared_ptr<Pcm> pcm(const std::string& file) {
        if (auto found = pcm_.find(file); found != pcm_.end()) return found->second;
        try {
            const auto source = assets_.loadAudio(file);
            if (source.spec.freq <= 0 || source.data.size() > size_t(INT_MAX))
                throw std::runtime_error("Invalid decoded audio dimensions");
            const SDL_AudioSpec destination{SDL_AUDIO_S16, 2, source.spec.freq};
            Uint8* converted = nullptr;
            int length = 0;
            if (!SDL_ConvertAudioSamples(&source.spec, source.data.data(), int(source.data.size()),
                                         &destination, &converted, &length))
                throw std::runtime_error(SDL_GetError());
            std::unique_ptr<Uint8, decltype(&SDL_free)> owner(converted, SDL_free);
            if (length <= 0 || length % int(sizeof(int16_t)) != 0)
                throw std::runtime_error("Audio decoded to an invalid PCM buffer");
            auto result = std::make_shared<Pcm>();
            result->rate = destination.freq;
            result->samples.resize(size_t(length) / sizeof(int16_t));
            std::memcpy(result->samples.data(), converted, size_t(length));
            pcm_[file] = result;
            return result;
        } catch (const std::exception& error) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DDLC audio %s: %s", file.c_str(), error.what());
            pcm_[file] = nullptr;
            return nullptr;
        }
    }

    void play(const std::string& channel, const std::string& spec, double fade, bool loop) override {
        std::lock_guard<std::mutex> lock(commandsMutex_);
        requested_[channel] = spec;
        commands_.push_back({Command::Play, channel, spec, fade, loop});
    }
    void queue(const std::string& channel, const std::string& spec, bool loop) override {
        std::lock_guard<std::mutex> lock(commandsMutex_);
        if (requested_[channel].empty()) requested_[channel] = spec;
        commands_.push_back({Command::Queue, channel, spec, 0, loop});
    }
    void stop(const std::string& channel, double fade) override {
        std::lock_guard<std::mutex> lock(commandsMutex_);
        requested_.erase(channel);
        commands_.push_back({Command::Stop, channel, {}, fade, false});
    }
    std::string playing(const std::string& channel) override {
        std::lock_guard<std::mutex> lock(commandsMutex_);
        const auto found = requested_.find(channel);
        return found == requested_.end() ? std::string() : found->second;
    }
    double position(const std::string& channel) override {
        std::lock_guard<std::mutex> lock(commandsMutex_);
        const auto found = positions_.find(channel);
        return found == positions_.end() ? 0 : found->second;
    }
    void savePersistent(const std::string& value) override {
        Kotonoha::writeTextFileAtomically(persistentPath_.string(), value);
    }
    void log(const std::string& value) override {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "DDLC: %s", value.c_str());
    }
    std::string loadPersistent() const {
        std::error_code error;
        if (!std::filesystem::exists(persistentPath_, error)) {
            if (error) throw std::runtime_error("Cannot inspect DDLC persistent data: " + error.message());
            return {};
        }
        std::ifstream input(persistentPath_, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot read DDLC persistent data: " + persistentPath_.string());
        return {std::istreambuf_iterator<char>(input), {}};
    }

    // The VM only enqueues audio; mixer and SDL device operations stay on the engine thread.
    void tick() {
        std::vector<Command> commands;
        {
            std::lock_guard<std::mutex> lock(commandsMutex_);
            commands.swap(commands_);
        }
        for (const Command& command : commands) {
            Channel& channel = channels_[command.channel];
            if (command.kind == Command::Play) {
                channel.queued.clear();
                if (channel.player) channel.player->Stop();
                start(command.channel, channel, command.spec, command.fade, command.loop);
            } else if (command.kind == Command::Queue) {
                if (channel.player && channel.player->Playing() && !channel.spec.empty()) {
                    channel.queued = command.spec;
                    channel.queuedLoop = command.loop;
                } else {
                    start(command.channel, channel, command.spec, 0, command.loop);
                }
            } else {
                channel.queued.clear();
                channel.spec.clear();
                if (channel.player && command.fade > 0 && channel.player->Playing())
                    fading_.push_back({std::move(channel.player), currentGain(channel), realNow(), command.fade});
                else if (channel.player)
                    channel.player->Stop();
            }
        }
        const double now = realNow();
        for (auto& [name, channel] : channels_) {
            if (!channel.player) {
                std::lock_guard<std::mutex> lock(commandsMutex_);
                requested_.erase(name);
                positions_.erase(name);
                continue;
            }
            channel.player->SetGain(muted_.count(name) ? 0.f : float(currentGain(channel)));
            if (!channel.queued.empty() &&
                (!channel.player->Playing() || channel.player->Position() >= channel.duration)) {
                const std::string next = std::exchange(channel.queued, {});
                const bool loop = channel.queuedLoop;
                channel.player->Stop();
                start(name, channel, next, 0, loop);
                std::lock_guard<std::mutex> lock(commandsMutex_);
                requested_[name] = channel.spec;
            }
            std::lock_guard<std::mutex> lock(commandsMutex_);
            if (!channel.player->Playing() && channel.queued.empty()) {
                requested_.erase(name);
                positions_.erase(name);
            } else {
                double position = channel.player->Position();
                if (channel.loop && position > channel.duration) {
                    const double begin = std::max(0.0, channel.parsed.loop);
                    const double span = channel.duration - begin;
                    if (span > 0) position = begin + std::fmod(position - channel.duration, span);
                }
                positions_[name] = position;
            }
        }
        for (auto it = fading_.begin(); it != fading_.end();) {
            const double progress = std::clamp((now - it->start) / it->len, 0.0, 1.0);
            it->player->SetGain(float(it->gain * (1 - progress)));
            if (progress >= 1 || !it->player->Playing()) {
                it->player->Stop();
                it = fading_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void stopAll() {
        {
            std::lock_guard<std::mutex> lock(commandsMutex_);
            commands_.clear();
            requested_.clear();
            positions_.clear();
        }
        for (auto& [_, channel] : channels_) if (channel.player) channel.player->Stop();
        channels_.clear();
        for (auto& fading : fading_) fading.player->Stop();
        fading_.clear();
    }

private:
    std::set<std::string> muted_;
    std::mutex mouseMutex_;
    double mouseX_ = 0, mouseY_ = 0;
    bool warp_ = false;

    struct Command {
        enum Kind { Play, Queue, Stop } kind;
        std::string channel, spec;
        double fade;
        bool loop;
    };

    static double currentGain(const Channel& channel) {
        if (channel.fadeLen <= 0) return channel.fadeTo;
        const double progress = std::clamp((realNow() - channel.fadeStart) / channel.fadeLen, 0.0, 1.0);
        return channel.fadeFrom + (channel.fadeTo - channel.fadeFrom) * progress;
    }
    void start(const std::string& name, Channel& channel, const std::string& spec, double fade, bool loop) {
        const AudioSpec parsed = parseSpec(spec);
        const auto data = pcm(parsed.file);
        channel.spec.clear();
        if (!data) return;
        if (!channel.player) channel.player = std::make_unique<Kotonoha::PcmPlayer>();
        Kotonoha::PcmPlayer::Clip clip;
        clip.samples = data->samples;
        clip.channels = 2;
        clip.rate = data->rate;
        clip.loop = loop;
        clip.loopStart = parsed.loop > 0 ? size_t(parsed.loop * data->rate) : 0;
        clip.startFrame = parsed.from > 0 ? size_t(parsed.from * data->rate) : 0;
        if (!channel.player->Play(engine_.GetAudio().channel("DDLC " + name), std::move(clip))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DDLC: cannot start audio channel %s", name.c_str());
            return;
        }
        channel.spec = spec;
        channel.parsed = parsed;
        channel.loop = loop;
        channel.duration = data->seconds();
        channel.fadeFrom = fade > 0 ? 0 : 1;
        channel.fadeTo = 1;
        channel.fadeStart = realNow();
        channel.fadeLen = fade;
        channel.player->SetGain(float(channel.fadeFrom));
    }

    Kotonoha::Engine& engine_;
    const GameAssets& assets_;
    TextEngine& text_;
    std::filesystem::path directory_, persistentPath_;
    std::map<std::string, std::pair<int, int>> sizes_;
    std::map<std::string, std::shared_ptr<Pcm>> pcm_;
    std::map<std::string, Channel> channels_;
    std::vector<Fading> fading_;
    std::mutex commandsMutex_;
    std::vector<Command> commands_;
    std::map<std::string, std::string> requested_;
    std::map<std::string, double> positions_;
};


struct Rgba {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
};

struct Tex {
    SDL_Texture* texture = nullptr; // owned by the asset provider
    int w = 0, h = 0, logicalW = 0, logicalH = 0;
    float factor = 0;
    uint64_t used = 0;
    bool failed = false;
};

struct Button {
    SDL_FRect r;
    std::function<void()> action;
};

enum class Phase { Boot, Splash, Menu, Play };

// GUI metrics of the original (gui.rpy / screens.rpy).
constexpr float kWinH = 182, kWinTop = (kH - kWinH) * .99f;

TextStyle uiStyle(const char* font, float size, uint32_t color) {
    TextStyle s;
    s.font = font;
    s.size = size;
    s.color = color;
    s.outlines.clear();
    return s;
}
constexpr const char* kAller = "gui/font/Aller_Rg.ttf";
constexpr const char* kRiffic = "gui/font/RifficFree-Bold.ttf";

TextStyle navStyle(bool hover, bool insensitive) {
    TextStyle s = uiStyle(kRiffic, 24, 0xffffffff);
    const uint32_t o = insensitive ? 0xffcceeff : hover ? 0xffaaccff : 0xbb5599ff;
    s.outlines = {{4.f, o}, {2.f, o}};
    return s;
}

struct Game {
    Kotonoha::Engine& engine;
    std::shared_ptr<GameAssets> assets;
    const Script& script;
    TextEngine text;
    Host host;
    std::unique_ptr<Vm> vm;
    Phase phase = Phase::Boot;
    bool quit = false;

    // Clock of the VM; frozen while the pause menu shows.
    double pausedAt = -1, pausedTotal = 0;
    double now() const { return (pausedAt >= 0 ? pausedAt : realNow()) - pausedTotal; }
    void freeze(bool on) {
        if (on && pausedAt < 0) pausedAt = realNow();
        if (!on && pausedAt >= 0) pausedTotal += realNow() - pausedAt, pausedAt = -1;
    }

    // Input and GUI state.
    float mx = -1, my = -1;
    bool ctrl = false, skipToggle = false, autoMode = SDL_getenv("DDLC_TMP_AUTO") != nullptr; // TMP-SMOKE
    bool pauseMenu = false, nameInput = false;
    enum class SubMenu { None, Save, Load, History };
    SubMenu subMenu = SubMenu::None;
    std::string nameText, notice;
    std::vector<Button> buttons, drawn;
    std::string hoverKey;
    float poemScroll = 0, poemMax = 0, historyScroll = 0, historyMax = 0;

    // Screen effects of effects.rpy: Tear strips and the Invert flash.
    struct TearPiece {
        float startY = 0, endY = 0;
        double offTime = 0, onTime = 0;
        int offset = 0;
    };
    double tearShownAt = -1, invertShownAt = -1;
    std::vector<TearPiece> tearPieces;
    bool invertStarted = false;
    std::mt19937 effectRandom{std::random_device{}()};
    SDL_Texture* fxTex = nullptr;
    int fxW = 0, fxH = 0;
    double rigClock = 0;
    std::unique_ptr<Kotonoha::PcmPlayer> uiSfx;

    // Drawing state.
    std::map<std::string, Tex> images;
    std::map<std::string, Rgba> masks;
    uint64_t frame = 0;
    float scale = 1; // canvas pixels per logical pixel
    SDL_Texture* target = nullptr;
    SDL_Texture* previousTarget = nullptr;
    SDL_Texture* canvas = nullptr;
    SDL_Texture* rt = nullptr;
    SDL_Texture* maskTex = nullptr;
    int canvasW = 0, canvasH = 0, rtW = 0, rtH = 0, maskW = 0, maskH = 0;

    Game(Kotonoha::Engine& engine, std::shared_ptr<GameAssets> assets)
        : engine(engine), assets(std::move(assets)), script(this->assets->script()),
          host(engine, *this->assets, text) {
        text.assets = this->assets.get();
        persistentText = host.loadPersistent();
    }

    ~Game() {
        if (vm) {
            try {
                host.savePersistent(vm->savePersistentText());
            } catch (const std::exception& error) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DDLC: cannot save persistent data: %s", error.what());
            }
        }
        vm.reset();
        host.stopAll();
        if (uiSfx) uiSfx->Stop();
        images.clear();
        assets->clearCache();
        text.clear();
        if (canvas) SDL_DestroyTexture(canvas);
        if (rt) SDL_DestroyTexture(rt);
        if (maskTex) SDL_DestroyTexture(maskTex);
        if (fxTex) SDL_DestroyTexture(fxTex);
    }

    bool skipAllowed() const { return !vm || vm->py.getAttr(vm->config, "allow_skipping").truthy(); }
    bool skipping() const { return (ctrl || skipToggle) && skipAllowed(); }

    void sfx(const char* file) {
        auto p = host.pcm(file);
        if (!p) return;
        if (!uiSfx) uiSfx = std::make_unique<Kotonoha::PcmPlayer>();
        Kotonoha::PcmPlayer::Clip c;
        c.samples = p->samples;
        c.channels = 2;
        c.rate = p->rate;
        if (!uiSfx->Play(engine.GetAudio().channel("DDLC ui"), std::move(c)))
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DDLC: cannot play UI sound %s", file);
    }

    // ---- primitives (logical 1280x720 coordinates, render scale set) ----

    Tex* image(SDL_Renderer* r, const std::string& file) {
        Tex& t = images[file];
        t.used = frame;
        if (t.failed) return nullptr;
        const auto& settings = engine.GetUpscaler().getSettings();
        const float factor = settings.enabled() ? settings.factor : 1;
        if (t.texture && t.factor == factor) return &t;
        try {
            if (!host.imageSize(file, t.logicalW, t.logicalH))
                throw std::runtime_error("Cannot determine image dimensions");
            SDL_Texture& texture = factor > 1
                ? assets->texture(*r, file, factor, &engine.GetUpscaler())
                : assets->texture(*r, file);
            float w = 0, h = 0;
            if (!SDL_GetTextureSize(&texture, &w, &h)) throw std::runtime_error(SDL_GetError());
            t.texture = &texture;
            t.w = int(w);
            t.h = int(h);
            t.factor = factor;
            SDL_SetTextureBlendMode(t.texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t.texture, SDL_SCALEMODE_LINEAR);
        } catch (const std::exception& error) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "DDLC image %s: %s", file.c_str(), error.what());
            t.failed = true;
            return nullptr;
        }
        return &t;
    }

    void evictImages() {
        const bool stale = std::any_of(images.begin(), images.end(), [&](const auto& entry) {
            return entry.second.texture && frame - entry.second.used > 900;
        });
        if (stale || assets->cachedTextures() > 96) {
            images.clear();
            assets->clearCache();
        }
    }

    // A textured quad: `src` in texture pixels, `local` the source-space rectangle mapped by m.
    void quad(SDL_Renderer* r, SDL_Texture* tex, float tw, float th, const SDL_FRect& src,
              const SDL_FRect& local, const float* m, float alpha) {
        if (!tex) return;
        if (m[1] == 0 && m[3] == 0 && m[0] > 0 && m[4] > 0) {
            const SDL_FRect dst{m[0] * local.x + m[2], m[4] * local.y + m[5], m[0] * local.w, m[4] * local.h};
            SDL_SetTextureAlphaModFloat(tex, alpha);
            SDL_RenderTexture(r, tex, &src, &dst);
            return;
        }
        SDL_SetTextureAlphaModFloat(tex, 1);
        const float lx[4] = {local.x, local.x + local.w, local.x + local.w, local.x};
        const float ly[4] = {local.y, local.y, local.y + local.h, local.y + local.h};
        const float u[4] = {src.x / tw, (src.x + src.w) / tw, (src.x + src.w) / tw, src.x / tw};
        const float v[4] = {src.y / th, src.y / th, (src.y + src.h) / th, (src.y + src.h) / th};
        SDL_Vertex vx[4];
        for (int i = 0; i < 4; ++i) {
            vx[i].position = {m[0] * lx[i] + m[1] * ly[i] + m[2], m[3] * lx[i] + m[4] * ly[i] + m[5]};
            vx[i].color = {1, 1, 1, alpha};
            vx[i].tex_coord = {u[i], v[i]};
        }
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(r, tex, vx, 4, idx, 6);
    }

    void drawOp(SDL_Renderer* r, const DrawOp& op) {
        if (op.alpha <= 0.002f) return;
        float cx = 0, cy = 0, cw = op.w, ch = op.h;
        if (op.crop[2] > 0) cx = op.crop[0], cy = op.crop[1], cw = op.crop[2], ch = op.crop[3];
        switch (op.k) {
        case DrawOp::K::Image: {
            Tex* t = image(r, op.image);
            if (!t) return;
            const float sx = op.w > 0 ? float(t->w) / op.w : 1, sy = op.h > 0 ? float(t->h) / op.h : 1;
            quad(r, t->texture, float(t->w), float(t->h), {cx * sx, cy * sy, cw * sx, ch * sy},
                 {0, 0, cw, ch}, op.m, op.alpha);
            break;
        }
        case DrawOp::K::Solid: {
            const uint32_t c = op.color;
            const float a = float(c & 255) / 255.f * op.alpha;
            const SDL_FColor col{float(c >> 24) / 255.f, float((c >> 16) & 255) / 255.f, float((c >> 8) & 255) / 255.f, a};
            const float* m = op.m;
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            if (m[1] == 0 && m[3] == 0) {
                SDL_FRect d{m[2], m[5], m[0] * cw, m[4] * ch};
                if (d.w < 0) d.x += d.w, d.w = -d.w;
                if (d.h < 0) d.y += d.h, d.h = -d.h;
                SDL_SetRenderDrawColorFloat(r, col.r, col.g, col.b, col.a);
                SDL_RenderFillRect(r, &d);
            } else {
                const float lx[4] = {0, cw, cw, 0}, ly[4] = {0, 0, ch, ch};
                SDL_Vertex vx[4];
                for (int i = 0; i < 4; ++i) {
                    vx[i].position = {m[0] * lx[i] + m[1] * ly[i] + m[2], m[3] * lx[i] + m[4] * ly[i] + m[5]};
                    vx[i].color = col;
                    vx[i].tex_coord = {0, 0};
                }
                const int idx[6] = {0, 1, 2, 0, 2, 3};
                SDL_RenderGeometry(r, nullptr, vx, 4, idx, 6);
            }
            break;
        }
        case DrawOp::K::Text: {
            const float* m = op.m;
            const float sc = scale * std::max(std::fabs(m[0]), std::fabs(m[4]));
            drawRich(r, parseRich(op.text, 0), SIZE_MAX, op.style, op.m, op.alpha, op.text, sc);
            break;
        }
        }
    }

    void drawRich(SDL_Renderer* r, const RichText& rich, size_t visible, const TextStyle& st, const float* m,
                  float alpha, const std::string& id, float sc = 0) {
        if (sc <= 0) sc = scale;
        sc = std::clamp(sc, .25f, 8.f);
        const TextTex* t = text.get(r, rich, visible, st, sc, id);
        if (!t || !t->tex) return;
        const float p = t->pad / t->scale;
        quad(r, t->tex, t->w, t->h, {0, 0, t->w, t->h}, {-p, -p, t->w / t->scale, t->h / t->scale}, m,
             alpha);
    }

    void drawText(SDL_Renderer* r, const std::string& s, const TextStyle& st, float x, float y, float alpha = 1) {
        const float m[6] = {1, 0, x, 0, 1, y};
        drawRich(r, parseRich(s, 0), SIZE_MAX, st, m, alpha, s);
    }
    SDL_FPoint textSize(const std::string& s, const TextStyle& st) {
        float w = 0, h = 0;
        text.measure(s, st, w, h);
        return {w, h};
    }

    void drawImage(SDL_Renderer* r, const std::string& file, float x, float y, float w = -1, float h = -1,
                   float alpha = 1) {
        Tex* t = image(r, file);
        if (!t) return;
        if (w < 0) w = float(t->logicalW);
        if (h < 0) h = float(t->logicalH);
        const float m[6] = {w / float(t->logicalW), 0, x, 0, h / float(t->logicalH), y};
        quad(r, t->texture, float(t->w), float(t->h), {0, 0, float(t->w), float(t->h)},
             {0, 0, float(t->logicalW), float(t->logicalH)}, m, alpha);
    }

    // Ren'Py Frame(): a 9-slice picture.
    void drawFrame(SDL_Renderer* r, const std::string& file, const SDL_FRect& dst, float l, float t, float rr, float b) {
        Tex* tx = image(r, file);
        if (!tx) return;
        SDL_SetTextureAlphaModFloat(tx->texture, 1);
        const float sx = float(tx->w) / float(tx->logicalW);
        const float sy = float(tx->h) / float(tx->logicalH);
        SDL_RenderTexture9Grid(r, tx->texture, nullptr, l * sx, rr * sx, t * sy, b * sy, 1.f / sx, &dst);
    }

    void drawRender(SDL_Renderer* r, const Render& rd, float dx = 0, float dy = 0) {
        for (const DrawOp& op : rd.ops) {
            if (dx == 0 && dy == 0) {
                drawOp(r, op);
                continue;
            }
            DrawOp o = op;
            o.m[2] += dx, o.m[5] += dy;
            drawOp(r, o);
        }
    }

    bool inside(const SDL_FRect& b) const { return mx >= b.x && my >= b.y && mx < b.x + b.w && my < b.y + b.h; }
    // Registers a clickable area for the next click; true when hovered.
    bool button(const SDL_FRect& b, std::function<void()> action, const std::string& key, bool hoverSound) {
        drawn.push_back({b, std::move(action)});
        const bool h = inside(b);
        if (h && hoverKey != key) {
            hoverKey = key;
            if (hoverSound) sfx("gui/sfx/hover.ogg");
        }
        return h;
    }

    // ---- screens ----

    void drawWindow(SDL_Renderer* r, const Window& w, double t) {
        const std::string box = w.monika ? "gui/textbox_monika.png" : "gui/textbox.png";
        if (Tex* tb = image(r, box))
            drawImage(r, box, (kW - float(tb->logicalW)) / 2, kWinTop + kWinH - float(tb->logicalH));
        if (!w.who.empty()) {
            drawImage(r, "gui/namebox.png", 350 - 84, kWinTop - 3);
            const TextStyle ls = vm->styleNamed("say_label");
            const SDL_FPoint sz = textSize(w.who, ls);
            drawText(r, w.who, ls, 350 - sz.x / 2, kWinTop - 3 + (39 - sz.y) / 2);
        }
        if (w.rich.glyphs.empty()) return;
        TextStyle ds = uiStyle(kAller, 24, 0xffffffff);
        ds.outlines = {{2.f, 0x000000aa}};
        ds.maxWidth = 744;
        const float m[6] = {1, 0, 268, 0, 1, kWinTop + 62};
        drawRich(r, w.rich, w.visibleAt(t), ds, m, 1, w.what);
    }

    void drawQuickMenu(SDL_Renderer* r) {
        static const char* labels[] = {"History", "Skip", "Auto", "Save", "Load", "Settings"};
        const TextStyle base = uiStyle(kAller, 14, 0x552222ff);
        float total = 0, h = 0;
        std::vector<SDL_FPoint> sizes;
        for (const char* l : labels) {
            sizes.push_back(textSize(l, base));
            total += sizes.back().x + 8;
            h = std::max(h, sizes.back().y + 8);
        }
        float x = (kW - total) / 2;
        const float y = (kH - h) * .995f;
        for (size_t i = 0; i < 6; ++i) {
            const std::string l = labels[i];
            const SDL_FRect b{x, y, sizes[i].x + 8, h};
            const bool hov = button(b, [this, l] { quickAction(l); }, "quick" + l, false);
            const bool sel = (l == "Skip" && skipToggle) || (l == "Auto" && autoMode);
            TextStyle s = base;
            s.color = hov ? 0xffccccff : sel ? 0xffffffff : 0x552222ff;
            drawText(r, l, s, x + 4, y + 4);
            x += b.w;
        }
    }
    void quickAction(const std::string& l) {
        sfx("gui/sfx/select.ogg");
        if (l == "Skip") skipToggle = !skipToggle;
        else if (l == "Auto") autoMode = !autoMode;
        else if (l == "Save") { subMenu = SubMenu::Save; openPause(true); }
        else if (l == "Load") { subMenu = SubMenu::Load; openPause(true); }
        else if (l == "Settings") engine.openSettings();
        else {
            subMenu = SubMenu::History;
            historyScroll = 0;
            openPause(true);
        }
    }

    void drawChoices(SDL_Renderer* r, const Interaction& in) {
        TextStyle st = uiStyle(kAller, 24, 0x000000ff);
        std::vector<float> hs;
        float total = 0;
        for (auto& it : in.items) {
            hs.push_back(textSize(it.text, st).y + 10);
            total += hs.back();
        }
        total += 22 * float(in.items.size() > 0 ? in.items.size() - 1 : 0);
        float y = 270 - total / 2;
        for (size_t i = 0; i < in.items.size(); ++i) {
            const SDL_FRect b{kW / 2 - 210, y, 420, hs[i]};
            const auto& it = in.items[i];
            bool hov = false;
            if (it.enabled)
                hov = button(b, [this, i] { sfx("gui/sfx/select.ogg"); resume(Value::integer(int64_t(i))); },
                             "choice" + std::to_string(i), true);
            drawFrame(r, hov ? "gui/button/choice_hover_background.png" : "gui/button/choice_idle_background.png", b,
                      100, 5, 100, 5);
            TextStyle s = st;
            s.color = hov ? 0xffaa99ff : 0x000000ff;
            const SDL_FPoint sz = textSize(it.text, s);
            drawText(r, it.text, s, kW / 2 - sz.x / 2, y + 5);
            y += hs[i] + 22;
        }
    }

    // A navigation-style text button (main menu, pause menu, dialogs).
    void navButton(SDL_Renderer* r, const std::string& label, float x, float y, float w, std::function<void()> act) {
        const SDL_FRect b{x, y, w, 36};
        const bool hov = button(b, [this, act] { sfx("gui/sfx/select.ogg"); act(); }, "nav" + label, true);
        const TextStyle s = navStyle(hov, false);
        const SDL_FPoint sz = textSize(label, s);
        drawText(r, label, s, x + 4, y + (36 - sz.y) / 2);
    }

    void navigation(SDL_Renderer* r, const std::vector<std::pair<std::string, std::function<void()>>>& items) {
        float w = 0;
        for (auto& [l, _] : items) w = std::max(w, textSize(l, navStyle(false, false)).x + 8);
        const float h = float(items.size()) * 36 + float(items.size() - 1) * 6;
        float y = (kH - h) * .8f;
        for (auto& [l, a] : items) {
            navButton(r, l, 80, y, w, a);
            y += 42;
        }
    }

    // screen confirm / dialog / name_input.
    void drawConfirm(SDL_Renderer* r, const std::string& msg,
                     const std::vector<std::pair<std::string, std::function<void()>>>& btns, const std::string* input) {
        drawImage(r, "gui/overlay/confirm.png", 0, 0, kW, kH);
        TextStyle ps = uiStyle(kAller, 24, 0x000000ff);
        ps.align = .5f;
        ps.maxWidth = 520;
        const SDL_FPoint psz = textSize(msg, ps);
        const TextStyle is = uiStyle(kAller, 24, 0x000000ff);
        const float ih = input ? textSize("Ay", is).y : 0;
        float bw = 0;
        std::vector<float> widths;
        for (auto& [l, _] : btns) {
            widths.push_back(textSize(l, navStyle(false, false)).x + 8);
            bw += widths.back();
        }
        bw += 100 * float(btns.size() - 1);
        const float cw = std::max(psz.x, bw), ch = psz.y + 30 + (input ? ih + 30 : 0) + 36;
        const float fw = std::max(cw + 80, 420.f), fh = ch + 80;
        const SDL_FRect f{(kW - fw) / 2, (kH - fh) / 2, fw, fh};
        drawFrame(r, "gui/frame.png", f, 40, 40, 40, 40);
        float y = f.y + 40;
        drawText(r, msg, ps, (kW - psz.x) / 2, y);
        y += psz.y + 30;
        if (input) {
            const bool caret = std::fmod(realNow(), 1.0) < .5;
            const std::string shown = *input + (caret ? "|" : " ");
            const SDL_FPoint sz = textSize(*input + "|", is);
            drawText(r, shown, is, (kW - sz.x) / 2, y);
            y += ih + 30;
        }
        float x = (kW - bw) / 2;
        for (size_t i = 0; i < btns.size(); ++i) {
            navButton(r, btns[i].first, x, y, widths[i], btns[i].second);
            x += widths[i] + 100;
        }
    }

    static Value screenArg(const Interaction& in, size_t i, const char* name) {
        for (auto& [k, v] : in.kw)
            if (k == name) return v;
        return i < in.args.size() ? in.args[i] : Value();
    }

    void screenAction(const Value& action) {
        if (action.cls() == "Quit") quit = true;
        else resume(action);
    }

    void drawScreenCall(SDL_Renderer* r, const Interaction& in) {
        if (in.screen == "confirm") {
            const Value yes = screenArg(in, 1, "yes_action"), no = screenArg(in, 2, "no_action");
            drawConfirm(r, in.message, {{"Yes", [this, yes] { screenAction(yes); }}, {"No", [this, no] { screenAction(no); }}},
                        nullptr);
        } else {
            const Value ok = screenArg(in, 1, "ok_action");
            drawConfirm(r, in.message, {{"OK", [this, ok] { screenAction(ok.isNone() ? Value::boolean(true) : ok); }}},
                        nullptr);
        }
    }

    void drawPoem(SDL_Renderer* r, const ScreenState& s, double t) {
        Value paper;
        for (auto& [k, v] : s.kw)
            if (k == "paper") paper = v;
        if (paper.isNone() && s.args.size() > 1) paper = s.args[1];
        if (DispP pd = vm->toDisp(paper.isNone() ? Value::str("paper") : paper)) {
            const double st = std::max(0.0, t - s.shownAt);
            const Render pr = pd->render(*vm, st, st);
            drawRender(r, pr, float((kW - pr.w) / 2), 0);
        }
        if (s.args.empty()) return;
        const Value& poem = s.args[0];
        std::string author = poem.get("author").text(), style = author + "_text";
        if (author == "yuri" && poem.get("yuri_2").truthy()) style = "yuri_text_2";
        if (author == "yuri" && poem.get("yuri_3").truthy()) style = "yuri_text_3";
        TextStyle ts = vm->styleNamed(style);
        ts.maxWidth = 710;
        const std::string body = poem.get("title").text() + "\n\n" + poem.get("text").text();
        const SDL_FPoint sz = textSize(body, ts);
        const float content = 40 + sz.y + 100;
        poemMax = std::max(0.f, content - kH);
        poemScroll = std::clamp(poemScroll, 0.f, poemMax);
        drawText(r, body, ts, 280, 40 - poemScroll);
        if (poemMax > 0) {
            const float barH = 700, thumbH = std::max(40.f, barH * kH / content);
            drawFrame(r, "gui/scrollbar/vertical_poem_bar.png", {1000, 10, 18, barH}, 4, 4, 4, 4);
            drawFrame(r, "gui/scrollbar/vertical_poem_thumb.png",
                      {1000, 10 + (barH - thumbH) * poemScroll / poemMax, 18, thumbH}, 4, 4, 4, 4);
        }
    }

    void drawPoemGame(SDL_Renderer* r, const Interaction& in) {
        for (auto& tx : in.texts) drawText(r, tx.text, vm->styleNamed(tx.value.text()), tx.x, tx.y);
        const TextStyle base = vm->styleNamed("poemgame_text");
        for (size_t i = 0; i < in.items.size(); ++i) {
            const auto& it = in.items[i];
            const SDL_FPoint sz = textSize(it.text, base);
            const Value v = it.value;
            const bool hov =
                button({it.x, it.y, sz.x, sz.y}, [this, v] { resume(v); }, "word" + std::to_string(i), false);
            TextStyle s = base;
            if (hov) s.outlines = {{3.f, 0xffeeffff}, {2.f, 0xffccffff}, {1.f, 0xffaaffff}};
            drawText(r, it.text, s, it.x - (hov ? 3.f : 0.f), it.y);
        }
    }

    void drawBase(SDL_Renderer* r, const Scene& sc, double t) {
        drawRender(r, vm->renderLayer(sc.master, t));
        for (auto& s : sc.screens)
            if (s.name == "poem") drawPoem(r, s, t);
    }

    void drawScene(SDL_Renderer* r, const Scene& sc, double t, bool live) {
        drawBase(r, sc, t);
        if (sc.window.visible) drawWindow(r, sc.window, live ? t : 1e18);
        if (live && phase == Phase::Play && sc.window.visible && vm->py.lookup("quick_menu").truthy())
            drawQuickMenu(r);
    }

    // ---- transitions ----

    using Painter = std::function<void()>;

    void renderTo(SDL_Renderer* r, const Painter& paint) {
        float w = 0, h = 0;
        SDL_GetTextureSize(target, &w, &h);
        if (!rt || rtW != int(w) || rtH != int(h)) {
            if (rt) SDL_DestroyTexture(rt);
            rt = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, int(w), int(h));
            rtW = int(w), rtH = int(h);
            if (rt) SDL_SetTextureBlendMode(rt, SDL_BLENDMODE_BLEND);
        }
        if (!rt) throw std::runtime_error("Cannot create DDLC transition target: " + std::string(SDL_GetError()));
        SDL_SetRenderTarget(r, rt);
        SDL_SetRenderScale(r, scale, scale);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        paint();
        SDL_SetRenderTarget(r, target);
        SDL_SetRenderScale(r, scale, scale);
    }

    void applyMask(SDL_Renderer* r, const Transition& tr, double p) {
        auto it = masks.find(tr.mask);
        if (it == masks.end()) {
            Rgba m;
            std::string file = assets->source().resolveImage(tr.mask);
            if (file.empty()) file = tr.mask;
            try {
                auto decoded = assets->loadSurface(file);
                Kotonoha::SurfacePtr rgba(SDL_ConvertSurface(decoded.get(), SDL_PIXELFORMAT_RGBA32));
                if (!rgba) throw std::runtime_error(SDL_GetError());
                m.w = rgba->w;
                m.h = rgba->h;
                m.px.resize(size_t(m.w) * size_t(m.h) * 4);
                for (int y = 0; y < m.h; ++y)
                    std::memcpy(m.px.data() + size_t(y) * size_t(m.w) * 4,
                                static_cast<const uint8_t*>(rgba->pixels) + size_t(y) * size_t(rgba->pitch),
                                size_t(m.w) * 4);
            } catch (const std::exception& error) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "DDLC mask %s: %s", tr.mask.c_str(), error.what());
            }
            it = masks.emplace(tr.mask, std::move(m)).first;
        }
        const Rgba& m = it->second;
        if (m.w == 0) {
            SDL_SetTextureAlphaModFloat(rt, float(p));
            return;
        }
        if (!maskTex || maskW != m.w || maskH != m.h) {
            if (maskTex) SDL_DestroyTexture(maskTex);
            maskTex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, m.w, m.h);
            if (!maskTex) throw std::runtime_error("Cannot create DDLC transition mask: " + std::string(SDL_GetError()));
            maskW = m.w, maskH = m.h;
            SDL_SetTextureBlendMode(maskTex, SDL_ComposeCustomBlendMode(
                                                 SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
                                                 SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD));
        }
        static std::vector<uint8_t> buf;
        buf.resize(size_t(m.w) * m.h * 4);
        const double ramp = std::max(1.0, tr.ramp), lead = p * (256 + ramp);
        uint8_t lut[256];
        for (int v = 0; v < 256; ++v) {
            const double mm = tr.reverse ? v : 255 - v;
            lut[v] = Uint8(std::lround(std::clamp((lead - mm) / ramp, 0.0, 1.0) * 255));
        }
        for (size_t i = 0, n = size_t(m.w) * m.h; i < n; ++i) {
            buf[i * 4] = buf[i * 4 + 1] = buf[i * 4 + 2] = 255;
            buf[i * 4 + 3] = lut[m.px[i * 4]];
        }
        SDL_UpdateTexture(maskTex, nullptr, buf.data(), m.w * 4);
        SDL_SetRenderTarget(r, rt);
        SDL_SetRenderScale(r, scale, scale);
        const SDL_FRect all{0, 0, kW, kH};
        SDL_RenderTexture(r, maskTex, nullptr, &all);
        SDL_SetRenderTarget(r, target);
        SDL_SetRenderScale(r, scale, scale);
        SDL_SetTextureAlphaModFloat(rt, 1);
    }

    void blend(SDL_Renderer* r, const Painter& from, const Painter& to, const Transition& tr, double t) {
        const double p = tr.time > 0 ? std::clamp(t / tr.time, 0.0, 1.0) : 1;
        if (tr.k == Transition::K::Pause || tr.k == Transition::K::Multiple || p >= 1) return to();
        from();
        renderTo(r, to);
        if (!rt) return;
        if (tr.k == Transition::K::ImageDissolve) applyMask(r, tr, p);
        else SDL_SetTextureAlphaModFloat(rt, float(p));
        const SDL_FRect all{0, 0, kW, kH};
        SDL_RenderTexture(r, rt, nullptr, &all);
        SDL_SetTextureAlphaModFloat(rt, 1);
    }

    void drawTransition(SDL_Renderer* r, const Interaction& in, double t) {
        const Transition& tr = *in.trans;
        const double el = t - in.start;
        const Painter oldScene = [&] { drawScene(r, in.from, t, false); };
        const Painter newScene = [&] { drawScene(r, vm->scene, t, true); };
        if (tr.k != Transition::K::Multiple) return blend(r, oldScene, newScene, tr, el);
        auto ref = [&](size_t i) -> Painter {
            if (i >= tr.scenes.size()) return newScene;
            const auto& s = tr.scenes[i];
            if (s.kind == 0) return oldScene;
            if (s.kind == 1) return newScene;
            DispP d = s.disp;
            return [this, r, d, el] {
                if (d) drawRender(r, d->render(*vm, el, el));
            };
        };
        double acc = 0;
        for (size_t i = 0; i < tr.steps.size(); ++i) {
            const double d = tr.steps[i] ? tr.steps[i]->duration() : 0;
            if (el < acc + d || i + 1 == tr.steps.size()) {
                if (tr.steps[i]) return blend(r, ref(i), ref(i + 1), *tr.steps[i], el - acc);
                return ref(i + 1)();
            }
            acc += d;
        }
        newScene();
    }

    // ---- flow ----

    std::string persistentText;

    void reportErrors() {
        if (!vm) return;
        for (auto& e : vm->errors) SDL_Log("DDLC: %s", e.c_str());
        vm->errors.clear();
    }

    void newVm() {
        if (vm) {
            persistentText = vm->savePersistentText();
            vm.reset();
        }
        vm = std::make_unique<Vm>(assets->source(), script, host);
        vm->userDirectory = host.directory();
        vm->clock = [this] { return now(); };
        vm->translate = [this](int route, const std::string& original) {
            return engine.GetTranslation().text(route, original);
        };
        vm->init(persistentText);
        reportErrors();
    }

    void startSplash() {
        phase = Phase::Splash;
        newVm();
        vm->start("splashscreen");
        reportErrors();
    }

    void enterMenu() {
        phase = Phase::Menu;
        pauseMenu = nameInput = false;
        skipToggle = autoMode = false;
        freeze(false);
        persistentText = vm->savePersistentText();
        host.savePersistent(persistentText);
        vm->scene = Scene{};
        vm->committed = Scene{};
        const double pt = vm->py.getAttr(vm->persistent, "playthrough").num();
        std::vector<std::pair<const char*, std::string>> show = {
            {"mm_bg", "menu_bg"}, {"mm_y", "menu_art_y"}, {"mm_n", "menu_art_n"}, {"mm_nav", "menu_nav"},
            {"mm_p1", "menu_particles"}, {"mm_p2", "menu_particles"}, {"mm_logo", "menu_logo"},
            {"mm_s", pt == 1 || pt == 2 ? "menu_art_s_glitch" : "menu_art_s"}, {"mm_p3", "menu_particles"}};
        if (pt != 4) show.push_back({"mm_m", "menu_art_m"});
        show.push_back({"mm_fade", "menu_fade"});
        for (auto& [tag, img] : show) {
            try {
                vm->py.eval("renpy.show('" + img + "', tag='" + tag + "')");
            } catch (const std::exception& e) {
                SDL_Log("DDLC: main menu %s: %s", img.c_str(), e.what());
            }
        }
        if (host.playing("music").empty()) {
            const Value t1 = vm->py.getAttr(vm->audio, "t1");
            if (t1.isStr()) host.play("music", t1.s, 0, true);
        }
        reportErrors();
    }

    void startGame() {
        nameInput = false;
        subMenu = SubMenu::None;
        if (SDL_TextInputActive(&engine.GetWindow()) && !SDL_StopTextInput(&engine.GetWindow()))
            throw std::runtime_error(SDL_GetError());
        host.stop("music", 1.0);
        newVm();
        phase = Phase::Play;
        vm->start("start");
        reportErrors();
    }

    void returnToMenu() {
        pauseMenu = false;
        subMenu = SubMenu::None;
        freeze(false);
        host.stopAll();
        newVm();
        enterMenu();
    }

    // ---- effect screens (effects.rpy, screens.rpy) ----

    static Value screenParam(const ScreenState& screen, size_t i, const char* name) {
        for (auto& [k, v] : screen.kw)
            if (k == name) return v;
        return i < screen.args.size() ? screen.args[i] : Value();
    }
    static double screenNumber(const ScreenState& screen, size_t i, const char* name, double fallback) {
        const Value v = screenParam(screen, i, name);
        return v.isNone() ? fallback : v.num();
    }
    const ScreenState* screenNamed(const std::string& name) const {
        if (!vm) return nullptr;
        for (auto& s : vm->scene.screens)
            if (s.name == name) return &s;
        return nullptr;
    }

    // screen invert(length, delay): at `delay` the music pauses and
    // sfx/glitch1.ogg plays; the screen hides itself after length + delay.
    void updateInvert() {
        const ScreenState* screen = screenNamed("invert");
        if (!screen) {
            if (invertShownAt >= 0) endInvert();
            return;
        }
        if (screen->shownAt != invertShownAt) {
            invertShownAt = screen->shownAt;
            invertStarted = false;
        }
        const double length = screenNumber(*screen, 0, "length", 0), delay = screenNumber(*screen, 1, "delay", 0);
        const double el = now() - screen->shownAt;
        if (!invertStarted && el >= delay) {
            invertStarted = true;
            host.mute("music", true);
            host.play("sound", "sfx/glitch1.ogg", 0, false);
        }
        if (el >= length + delay) {
            auto& screens = vm->scene.screens;
            screens.erase(std::remove_if(screens.begin(), screens.end(),
                                         [](const ScreenState& x) { return x.name == "invert"; }),
                          screens.end());
            endInvert();
        }
    }
    void endInvert() {
        invertShownAt = -1;
        if (invertStarted) {
            host.mute("music", false);
            host.stop("sound", 0);
        }
        invertStarted = false;
    }

    // screen rigged_choice: RigMouse pulls the pointer toward the choices at 30 Hz.
    void updateRiggedChoice() {
        double x = 0, y = 0;
        if (vm->interaction().k == Interaction::K::Menu && vm->interaction().screen == "rigged_choice" &&
            mx >= 0 && my >= 0) {
            const double t = realNow();
            if (t - rigClock >= 1.0 / 30.0) {
                rigClock = t;
                if (my < 345) host.setMousePosition((mx * 9 + 640) / 10.0, (my * 9 + 345) / 10.0);
            }
        }
        if (!host.takeWarp(x, y)) return;
        mx = float(x);
        my = float(y);
        float wx = 0, wy = 0;
        if (SDL_RenderCoordinatesToWindow(&engine.GetRenderer(), float(x), float(y), &wx, &wy))
            SDL_WarpMouseInWindow(&engine.GetWindow(), wx, wy);
    }

    // Copies the canvas into fxTex.
    SDL_Texture* snapshot(SDL_Renderer* r) {
        if (fxTex && (fxW != canvasW || fxH != canvasH)) {
            SDL_DestroyTexture(fxTex);
            fxTex = nullptr;
        }
        if (!fxTex) {
            fxTex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, canvasW, canvasH);
            if (!fxTex) throw std::runtime_error("Cannot create DDLC effect target: " + std::string(SDL_GetError()));
            SDL_SetTextureBlendMode(fxTex, SDL_BLENDMODE_NONE);
            fxW = canvasW;
            fxH = canvasH;
        }
        SDL_Texture* tex = fxTex;
        SDL_BlendMode mode = SDL_BLENDMODE_BLEND;
        SDL_GetTextureBlendMode(canvas, &mode);
        SDL_SetTextureBlendMode(canvas, SDL_BLENDMODE_NONE);
        SDL_SetRenderTarget(r, tex);
        SDL_SetRenderScale(r, 1, 1);
        SDL_RenderTexture(r, canvas, nullptr, nullptr);
        SDL_SetRenderTarget(r, canvas);
        SDL_SetRenderScale(r, scale, scale);
        SDL_SetTextureBlendMode(canvas, mode);
        return tex;
    }

    // Tear(number, offtimeMult, ontimeMult, offsetMin, offsetMax): horizontal
    // strips of the screen that jump sideways on their own timers.
    void drawTear(SDL_Renderer* r, const ScreenState& screen, double t) {
        const int number = std::max(0, int(screenNumber(screen, 0, "number", 10)));
        const double offMult = screenNumber(screen, 1, "offtimeMult", 1), onMult = screenNumber(screen, 2, "ontimeMult", 1);
        const int offsetMin = int(screenNumber(screen, 3, "offsetMin", 0));
        const int offsetMax = std::max(offsetMin, int(screenNumber(screen, 4, "offsetMax", 50)));
        if (screen.shownAt != tearShownAt) {
            tearShownAt = screen.shownAt;
            std::vector<float> points{0, kH};
            std::uniform_int_distribution<int> y(10, int(kH) - 10);
            for (int i = 0; i < number; ++i) points.push_back(float(y(effectRandom)));
            std::sort(points.begin(), points.end());
            std::uniform_real_distribution<double> unit(0, 1);
            tearPieces.clear();
            for (size_t i = 0; i + 1 < points.size(); ++i)
                tearPieces.push_back({points[i], points[i + 1], (unit(effectRandom) * .2 + .2) * offMult,
                                      (unit(effectRandom) * .2 + .2) * onMult, 0});
        }
        SDL_Texture* source = snapshot(r);
        const SDL_FRect all{0, 0, kW, kH};
        SDL_RenderTexture(r, source, nullptr, &all);
        const double st = std::max(0.0, t - screen.shownAt);
        for (auto& piece : tearPieces) {
            const double cycle = piece.offTime + piece.onTime;
            const double at = cycle > 0 ? std::fmod(st, cycle) : 0;
            if (at > piece.offTime && piece.offset == 0)
                piece.offset = std::uniform_int_distribution<int>(offsetMin, offsetMax)(effectRandom);
            else if (at <= piece.offTime && piece.offset != 0)
                piece.offset = 0;
            const float top = std::max(0.f, piece.startY - 1), height = std::max(0.f, piece.endY - piece.startY);
            if (height <= 0) continue;
            const SDL_FRect src{0, top * scale, float(canvasW), height * scale};
            const SDL_FRect dst{float(piece.offset), piece.startY, kW, height};
            SDL_RenderTexture(r, source, &src, &dst);
        }
    }

    // Invert(delay): the screen in negative colours.
    void drawInverted(SDL_Renderer* r) {
        Kotonoha::SurfacePtr pixels(SDL_RenderReadPixels(r, nullptr));
        if (!pixels) throw std::runtime_error("Cannot read the DDLC canvas: " + std::string(SDL_GetError()));
        Kotonoha::SurfacePtr rgba(SDL_ConvertSurface(pixels.get(), SDL_PIXELFORMAT_RGBA32));
        if (!rgba) throw std::runtime_error(SDL_GetError());
        for (int y = 0; y < rgba->h; ++y) {
            auto* row = static_cast<uint8_t*>(rgba->pixels) + size_t(y) * size_t(rgba->pitch);
            for (int x = 0; x < rgba->w; ++x) {
                row[x * 4] = uint8_t(255 - row[x * 4]);
                row[x * 4 + 1] = uint8_t(255 - row[x * 4 + 1]);
                row[x * 4 + 2] = uint8_t(255 - row[x * 4 + 2]);
                row[x * 4 + 3] = 255;
            }
        }
        Kotonoha::TexturePtr tex(SDL_CreateTextureFromSurface(r, rgba.get()));
        if (!tex) throw std::runtime_error(SDL_GetError());
        SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_NONE);
        const SDL_FRect all{0, 0, kW, kH};
        SDL_RenderTexture(r, tex.get(), nullptr, &all);
    }

    void drawScreenEffects(SDL_Renderer* r) {
        if (!vm || (phase != Phase::Splash && phase != Phase::Play)) {
            tearShownAt = -1;
            return;
        }
        const double t = now();
        if (const ScreenState* tear = screenNamed("tear")) {
            drawTear(r, *tear, t);
            static int shots = 0; // TMP-SMOKE
            if (const char* d = SDL_getenv("DDLC_TMP_SHOT"); d && shots < 3 && t - tear->shownAt > .5 * (shots + 1)) { // TMP-SMOKE
                Kotonoha::SurfacePtr px(SDL_RenderReadPixels(r, nullptr)); // TMP-SMOKE
                SDL_SaveBMP(px.get(), (std::string(d) + "/tear" + std::to_string(shots++) + ".bmp").c_str()); // TMP-SMOKE
            } // TMP-SMOKE
        }
        else tearShownAt = -1;
        if (const ScreenState* invert = screenNamed("invert"))
            if (t - invert->shownAt >= screenNumber(*invert, 1, "delay", 0)) drawInverted(r);
    }

    // screen skip_indicator (also shown by the script as fake_skip_indicator).
    void drawSkipIndicator(SDL_Renderer* r) {
        if (!vm || phase != Phase::Play || pauseMenu) return;
        const bool fake = screenNamed("fake_skip_indicator") != nullptr;
        if (!fake && !skipping()) return;
        const TextStyle st = uiStyle(kAller, 16, 0xffffffff);
        const SDL_FPoint size = textSize("Skipping", st);
        const float tri = 10, gap = 6;
        const float width = 16 + size.x + 3 * (gap + tri) + 50, height = 5 + size.y + 5;
        drawFrame(r, "gui/skip.png", {0, 10, width, height}, 16, 5, 50, 5);
        drawText(r, "Skipping", st, 16, 15);
        // delayed_blink(delay, 1.0): alpha .5, then .2s up to 1, hold .2s, .2s down, rest.
        const double t = realNow();
        for (int i = 0; i < 3; ++i) {
            const double at = std::fmod(std::max(0.0, t - .2 * i), 1.0);
            const double alpha = at < .2 ? .5 + at * 2.5 : at < .4 ? 1 : at < .6 ? 1 - (at - .4) * 2.5 : .5;
            const float x = 16 + size.x + gap + float(i) * (gap + tri), cy = 15 + size.y / 2;
            const SDL_FColor c{1, 1, 1, float(alpha)};
            SDL_Vertex v[3] = {{{x, cy - tri / 2}, c, {0, 0}}, {{x + tri * .8f, cy}, c, {0, 0}},
                               {{x, cy + tri / 2}, c, {0, 0}}};
            SDL_RenderGeometry(r, nullptr, v, 3, nullptr, 0);
        }
    }

    // screen history: the dialogue the script recorded in _history_list.
    void drawHistory(SDL_Renderer* r) {
        drawImage(r, "gui/overlay/game_menu.png", 0, 0, kW, kH);
        drawText(r, "History", navStyle(false, false), 80, 30);
        struct Entry {
            std::string who, what;
        };
        std::vector<Entry> entries;
        try {
            const Value list = vm->py.lookup("_history_list");
            if (!list.isNone())
                for (const Value& entry : vm->py.iterate(list)) {
                    const Value who = vm->py.getAttr(entry, "who"), what = vm->py.getAttr(entry, "what");
                    entries.push_back({who.isNone() ? "" : who.text(), what.isNone() ? "" : what.text()});
                }
        } catch (const PyError& error) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "DDLC history: %s", error.what());
        }
        const float top = 90, bottom = kH - 70;
        TextStyle whoStyle = uiStyle(kRiffic, 22, 0xffffffff);
        whoStyle.outlines = {{3.f, 0xbb5599ff}};
        TextStyle whatStyle = uiStyle(kAller, 22, 0xffffffff);
        whatStyle.outlines = {{2.f, 0x523140ff}};
        whatStyle.maxWidth = 760;
        std::vector<float> heights;
        float total = 0;
        for (auto& e : entries) {
            heights.push_back(std::max(textSize(e.what, whatStyle).y, 30.f) + 16);
            total += heights.back();
        }
        historyMax = std::max(0.f, total - (bottom - top));
        historyScroll = std::clamp(historyScroll, 0.f, historyMax);
        // Newest at the bottom; the scroll offset moves back in time.
        float y = bottom + historyScroll;
        SDL_Rect clip{int(std::lround(0 * scale)), int(std::lround(top * scale)), int(std::lround(kW * scale)),
                      int(std::lround((bottom - top) * scale))};
        SDL_SetRenderClipRect(r, &clip);
        for (size_t i = entries.size(); i-- > 0;) {
            y -= heights[i];
            if (y > bottom) continue;
            if (y + heights[i] < top) break;
            if (!entries[i].who.empty()) {
                const SDL_FPoint w = textSize(entries[i].who, whoStyle);
                drawText(r, entries[i].who, whoStyle, 330 - w.x, y);
            }
            drawText(r, entries[i].what, whatStyle, 360, y);
        }
        SDL_SetRenderClipRect(r, nullptr);
        if (entries.empty()) drawText(r, "The dialogue history is empty.", whatStyle, 360, top);
        navButton(r, "Return", 80, kH - 60, textSize("Return", navStyle(false, false)).x + 8, [this] {
            subMenu = SubMenu::None;
            openPause(false);
        });
    }

    void openPause(bool on) {
        if (phase != Phase::Play) return;
        pauseMenu = on;
        if (!on) subMenu = SubMenu::None;
        freeze(on);
    }

    void resume(Value v = {}) {
        if (!vm || !vm->running() || vm->finished()) return;
        const bool poem = vm->scene.hasScreen("poem");
        const bool skip = phase == Phase::Play && skipping();
        vm->py.setAttr(vm->config, "skipping", Value::boolean(skip));
        vm->resume(std::move(v));
        // The script stops skipping by clearing config.skipping (Act 3).
        if (skip && vm->running() && !vm->finished() && !vm->py.getAttr(vm->config, "skipping").truthy())
            skipToggle = false;
        if (poem != vm->scene.hasScreen("poem")) poemScroll = 0;
        reportErrors();
    }

    void onFinished() {
        const bool restart = vm->interaction().k == Interaction::K::Restart;
        if (phase == Phase::Splash && !restart) return enterMenu();
        persistentText = vm->savePersistentText();
        host.savePersistent(persistentText);
        if (restart) {
            host.stopAll();
            vm.reset();
            return startSplash();
        }
        returnToMenu();
    }

    static double revealDone(const Window& w) {
        if (w.from >= w.to || w.from >= w.rich.glyphs.size()) return w.t0;
        const size_t last = std::min(w.to, w.rich.glyphs.size()) - 1;
        return w.t0 + (w.rich.glyphs[last].t - w.rich.glyphs[w.from].t);
    }

    void update() {
        host.tick();
        if (phase == Phase::Boot) startSplash();
        if (!vm) return;
        if (phase == Phase::Menu) return vm->collectHidden();
        if (vm->finished() || !vm->running()) return onFinished();
        if (pauseMenu) return;
        updateInvert();
        updateRiggedChoice();
        const Interaction& in = vm->interaction();
        const double t = now(), el = t - in.start;
        switch (in.k) {
        case Interaction::K::Say: {
            const Window& w = vm->scene.window;
            const double done = revealDone(w);
            if (skipping() && el > .03) resume();
            else if (in.sayWait == 2 && t >= done) resume();
            else if (in.sayWait == 1 && t >= done + in.sayTime) resume();
            else if (in.sayWait == 0 && autoMode && t >= done + 1.0 + .02 * double(w.to - w.from)) resume();
            break;
        }
        case Interaction::K::Pause:
            if (in.deadline >= 0 && el >= in.deadline) resume();
            else if (skipping() && in.dismiss && el > .03) resume();
            break;
        case Interaction::K::Transition:
            if ((in.deadline >= 0 && el >= in.deadline) || (skipping() && el > .03)) resume();
            break;
        case Interaction::K::None: resume(); break;
        default: break;
        }
    }

    // A click (or Enter) that hit no button.
    void advance() {
        if (!vm || pauseMenu || phase == Phase::Menu || !vm->running() || vm->finished()) return;
        const Interaction& in = vm->interaction();
        const double t = now();
        switch (in.k) {
        case Interaction::K::Say: {
            Window& w = vm->scene.window;
            if (w.visibleAt(t) < w.to) {
                w.t0 -= revealDone(w) - t;
            } else if (in.sayWait != 2) {
                resume();
            }
            break;
        }
        case Interaction::K::Pause:
        case Interaction::K::Transition:
            if (in.dismiss) resume();
            break;
        default: break;
        }
    }

    void submitName() {
        if (nameText.empty()) return;
        vm->py.setAttr(vm->persistent, "playername", Value::str(nameText));
        startGame();
    }

    void drawSaveLoadMenu(SDL_Renderer* r) {
        drawn.clear();
        drawImage(r, "gui/overlay/game_menu.png", 0, 0, kW, kH);
        drawConfirm(r, "Save and load are not available in this port yet.",
                    {{"Back", [this] { subMenu = SubMenu::None; }}}, nullptr);
    }

    void drawMainMenu(SDL_Renderer* r) {
        const double t = now();
        const auto& m = vm->scene.master;
        const size_t below = std::min<size_t>(4, m.size());
        drawRender(r, vm->renderLayer(std::vector<Shown>(m.begin(), m.begin() + long(below)), t));
        const double pt = vm->py.getAttr(vm->persistent, "playthrough").num();
        const std::string newGame = pt == 1 ? "\xC5\x94\xC5\x97\xC3\xB1\xC4\xAE\xC2\xBC\xC2\xBB\xC5\xA7\xC3\xBE\xC5\x80\xC3"
                                              "\x82\xC5\xBB\xC5\x95\xC4\x9B\xC5\x8D\xC3\xAC\xC2\xAB"
                                            : "New Game";
        if (subMenu == SubMenu::Load) {
            drawSaveLoadMenu(r);
        } else if (!nameInput && notice.empty()) {
            navigation(r, {{newGame,
                            [this] {
                                if (vm->py.getAttr(vm->persistent, "playername").truthy()) return startGame();
                                nameInput = true;
                                nameText.clear();
                                if (!SDL_StartTextInput(&engine.GetWindow()))
                                    throw std::runtime_error(SDL_GetError());
                            }},
                           {"Load Game", [this] { subMenu = SubMenu::Load; }},
                           {"Settings", [this] { engine.openSettings(); }},
                           {"Help", [this] { notice = "Doki Doki Literature Club! is a free game by Team Salvato."; }},
                           {"Quit", [this] { quit = true; }}});
        }
        drawRender(r, vm->renderLayer(std::vector<Shown>(m.begin() + long(below), m.end()), t));
        if (nameInput) {
            drawn.clear();
            drawConfirm(r, "Please enter your name", {{"OK", [this] { submitName(); }}}, &nameText);
        } else if (!notice.empty()) {
            drawn.clear();
            drawConfirm(r, notice, {{"OK", [this] { notice.clear(); }}}, nullptr);
        }
    }

    void drawPauseMenu(SDL_Renderer* r) {
        if (subMenu == SubMenu::History) {
            drawn.clear();
            drawHistory(r);
            return;
        }
        if (subMenu != SubMenu::None) {
            drawSaveLoadMenu(r);
            return;
        }
        drawn.clear();
        drawImage(r, "gui/overlay/game_menu.png", 0, 0, kW, kH);
        if (!notice.empty()) {
            drawConfirm(r, notice, {{"OK", [this] { notice.clear(); }}}, nullptr);
            return;
        }
        navigation(r, {{"Return", [this] { openPause(false); }},
                       {"Save Game", [this] { subMenu = SubMenu::Save; }},
                       {"Load Game", [this] { subMenu = SubMenu::Load; }},
                       {"Main Menu", [this] { returnToMenu(); }},
                       {"Quit", [this] { quit = true; }}});
    }

    void beginFrame(SDL_Renderer* r, float pixelScale) {
        scale = std::clamp(pixelScale, .25f, 4.f);
        const int width = std::max(1, int(std::lround(kW * scale)));
        const int height = std::max(1, int(std::lround(kH * scale)));
        if (!canvas || canvasW != width || canvasH != height) {
            if (canvas) SDL_DestroyTexture(canvas);
            canvas = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, width, height);
            if (!canvas) throw std::runtime_error("Cannot create DDLC canvas: " + std::string(SDL_GetError()));
            canvasW = width;
            canvasH = height;
        }
        scale = float(width) / kW;
        previousTarget = SDL_GetRenderTarget(r);
        target = canvas;
        if (!SDL_SetRenderTarget(r, canvas) ||
            !SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED) ||
            !SDL_SetRenderScale(r, scale, scale))
            throw std::runtime_error("Cannot prepare DDLC canvas: " + std::string(SDL_GetError()));
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        text.frame = ++frame;
        drawn.clear();
    }

    void drawStage(SDL_Renderer* r) {
        if (!vm || (phase != Phase::Splash && phase != Phase::Play)) return;
        const Interaction& interaction = vm->interaction();
        if (interaction.k == Interaction::K::Transition && interaction.trans)
            drawTransition(r, interaction, now());
        else
            drawBase(r, vm->scene, now());
    }

    void drawDialogue(SDL_Renderer* r) {
        if (!vm || (phase != Phase::Splash && phase != Phase::Play) ||
            vm->interaction().k == Interaction::K::Transition || !vm->scene.window.visible) return;
        drawWindow(r, vm->scene.window, now());
        if (phase == Phase::Play && vm->py.lookup("quick_menu").truthy()) drawQuickMenu(r);
    }

    void drawChoices(SDL_Renderer* r) {
        drawScreenEffects(r);
        drawSkipIndicator(r);
        if (!vm || (phase != Phase::Splash && phase != Phase::Play)) return;
        const Interaction& interaction = vm->interaction();
        if (interaction.k == Interaction::K::Menu ||
            (interaction.k == Interaction::K::Say && !interaction.items.empty()))
            drawChoices(r, interaction);
        else if (interaction.k == Interaction::K::Screen) drawScreenCall(r, interaction);
        else if (interaction.k == Interaction::K::Ui) drawPoemGame(r, interaction);
    }

    void endFrame(SDL_Renderer* r) {
        buttons.swap(drawn);
        bool any = false;
        for (auto& b : buttons) any |= inside(b.r);
        if (!any) hoverKey.clear();
        if (!SDL_SetRenderTarget(r, previousTarget))
            throw std::runtime_error("Cannot restore render target: " + std::string(SDL_GetError()));
        const SDL_FRect destination{0, 0, kW, kH};
        if (!SDL_RenderTexture(r, canvas, nullptr, &destination))
            throw std::runtime_error("Cannot present DDLC canvas: " + std::string(SDL_GetError()));
        text.evict();
        if (frame % 300 == 0) evictImages();
    }

    void click() {
        for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
            if (inside(it->r)) {
                auto act = it->action;
                act();
                return;
            }
        }
        if (phase == Phase::Play || phase == Phase::Splash) advance();
    }

    bool handleEvent(const SDL_Event& event) {
        const SDL_Event* e = &event;
        switch (e->type) {
        case SDL_EVENT_MOUSE_MOTION:
            mx = e->motion.x;
            my = e->motion.y;
            host.trackMouse(mx, my);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            mx = e->button.x;
            my = e->button.y;
            if (e->button.button == SDL_BUTTON_LEFT) click();
            else if (e->button.button == SDL_BUTTON_RIGHT) openPause(!pauseMenu);
            break;
        case SDL_EVENT_FINGER_DOWN:
            mx = e->tfinger.x;
            my = e->tfinger.y;
            if (mx >= 0 && mx < kW && my >= 0 && my < kH) click();
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (pauseMenu && subMenu == SubMenu::History)
                historyScroll = std::clamp(historyScroll + e->wheel.y * 60.f, 0.f, historyMax);
            else
                poemScroll = std::clamp(poemScroll - e->wheel.y * 60.f, 0.f, poemMax);
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (nameInput) {
                std::string input = nameText;
                for (const char* c = e->text.text; *c; ++c)
                    if (static_cast<unsigned char>(*c) >= 0x20 && *c != 0x7f) input += *c;
                nameText = input.substr(0, Kotonoha::utf8Prefix(input, 12));
            }
            break;
        case SDL_EVENT_KEY_DOWN: {
            SDL_Keycode k = e->key.key;
            if (k == SDLK_AC_BACK) k = SDLK_ESCAPE;
            if (k == SDLK_LCTRL || k == SDLK_RCTRL) ctrl = true;
            if (nameInput) {
                if (k == SDLK_BACKSPACE && !nameText.empty()) Kotonoha::eraseLastCharacter(nameText);
                else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) submitName();
                else if (k == SDLK_ESCAPE) {
                    nameInput = false;
                    if (!SDL_StopTextInput(&engine.GetWindow())) throw std::runtime_error(SDL_GetError());
                }
                break;
            }
            if (phase == Phase::Splash) break;
            if (k == SDLK_ESCAPE) {
                if (!notice.empty()) notice.clear();
                else if (phase == Phase::Menu) quit = true;
                else openPause(!pauseMenu);
            } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) {
                if (!notice.empty()) notice.clear();
                else if (!e->key.repeat) advance();
            } else if (k == SDLK_TAB && phase == Phase::Play) {
                skipToggle = !skipToggle;
            } else if (k == SDLK_A && phase == Phase::Play) {
                autoMode = !autoMode;
            }
            break;
        }
        case SDL_EVENT_KEY_UP:
            if (e->key.key == SDLK_LCTRL || e->key.key == SDLK_RCTRL) ctrl = false;
            break;
        default: break;
        }
        if (quit) {
            SDL_Event request{};
            request.type = SDL_EVENT_QUIT;
            if (!SDL_PushEvent(&request)) throw std::runtime_error(SDL_GetError());
            quit = false;
        }
        return true;
    }
};


} // namespace

namespace Ddlc {

struct GameSession::Impl {
    Game game;
    Impl(Kotonoha::Engine& engine, std::shared_ptr<GameAssets> assets)
        : game(engine, std::move(assets)) {}
};

GameSession::GameSession(Kotonoha::Engine& engine, std::shared_ptr<GameAssets> assets)
    : impl_(std::make_unique<Impl>(engine, std::move(assets))) {}

GameSession::~GameSession() = default;

void GameSession::update(bool suspended) {
    auto& game = impl_->game;
    game.freeze(suspended || game.pauseMenu);
    if (suspended && !game.pauseMenu) game.host.tick();
    else game.update();
}

bool GameSession::event(Kotonoha::EventRef event) { return impl_->game.handleEvent(event); }
bool GameSession::inMenu() const { return impl_->game.phase == Phase::Menu; }
bool GameSession::playing() const { return impl_->game.phase == Phase::Play; }
bool GameSession::paused() const { return impl_->game.pauseMenu; }
bool GameSession::choosing() const {
    const auto& game = impl_->game;
    if (!game.vm || game.phase == Phase::Menu) return false;
    const auto kind = game.vm->interaction().k;
    return kind == Interaction::K::Menu || kind == Interaction::K::Screen || kind == Interaction::K::Ui;
}
bool GameSession::showingDialogue() const {
    const auto& game = impl_->game;
    return game.vm && game.phase != Phase::Menu && game.vm->scene.window.visible;
}

std::string GameSession::debugNode() const {
    const auto& game = impl_->game;
    if (!game.vm || !game.vm->currentNode()) return {};
    const Node& node = *game.vm->currentNode();
    return node.file + ":" + std::to_string(node.line) + " | route " + std::to_string(node.route);
}

void GameSession::debugJump(const std::string& label) {
    auto& game = impl_->game;
    if (!game.vm || game.phase != Phase::Play)
        throw std::logic_error("Start gameplay before jumping");
    if (!game.script.hasLabel(label))
        throw std::invalid_argument("Unknown DDLC label: " + label);
    game.pauseMenu = game.nameInput = false;
    game.subMenu = Game::SubMenu::None;
    game.skipToggle = game.autoMode = game.ctrl = false;
    game.host.stopAll();
    game.vm->start(label);
    game.reportErrors();
}

void GameSession::beginFrame(Kotonoha::RendererRef renderer, float pixelScale) {
    impl_->game.beginFrame(&renderer, pixelScale);
}
void GameSession::drawStage(Kotonoha::RendererRef renderer) { impl_->game.drawStage(&renderer); }
void GameSession::drawDialogue(Kotonoha::RendererRef renderer) { impl_->game.drawDialogue(&renderer); }
void GameSession::drawChoices(Kotonoha::RendererRef renderer) { impl_->game.drawChoices(&renderer); }
void GameSession::drawMenu(Kotonoha::RendererRef renderer) {
    if (inMenu() && impl_->game.vm) impl_->game.drawMainMenu(&renderer);
}
void GameSession::drawPause(Kotonoha::RendererRef renderer) {
    if (paused()) impl_->game.drawPauseMenu(&renderer);
}
void GameSession::endFrame(Kotonoha::RendererRef renderer) { impl_->game.endFrame(&renderer); }
void GameSession::abortFrame(Kotonoha::RendererRef renderer) noexcept {
    if (!SDL_SetRenderTarget(&renderer, impl_->game.previousTarget))
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DDLC: cannot restore render target: %s", SDL_GetError());
}

} // namespace Ddlc
