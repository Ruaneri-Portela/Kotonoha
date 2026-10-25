// Doki Doki Literature Club! (Ren'Py 6.99) running on the Kotonoha engine.
// The original archives (.rpa) and compiled scripts (.rpyc) are read at run
// time and executed by the port's own Python/ATL runtime (Ddlc::Vm); this
// file is the host: decoding, audio, text, drawing and the GUI screens.
// Usage: DokiDoki --ddlc <DDLC game dir>   (or DDLC_DIR)
#include "Ddlc/Runtime.hpp"

#include <Kotonoha/Main.hpp>
#include <Kotonoha/components/Canvas.hpp>
#include <Kotonoha/components/CanvasImage.hpp>
#include <Kotonoha/components/GameplayFlow.hpp>
#include <Kotonoha/components/PcmPlayer.hpp>
#include <Kotonoha/components/Sound.hpp>
#include <Kotonoha/debug/DebugUi.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

using Kotonoha::global;
using namespace Ddlc;

namespace {

constexpr float kW = 1280, kH = 720;

std::string g_dir;

// ---- decoding ---------------------------------------------------------------

struct Rgba {
    int w = 0, h = 0;
    std::vector<uint8_t> px; // RGBA8
};

bool decodeImage(const std::vector<uint8_t>& data, Rgba& out) {
    AVCodecID id = AV_CODEC_ID_NONE;
    if (data.size() > 8 && data[0] == 0x89 && data[1] == 'P') id = AV_CODEC_ID_PNG;
    else if (data.size() > 3 && data[0] == 0xFF && data[1] == 0xD8) id = AV_CODEC_ID_MJPEG;
    else if (data.size() > 12 && std::memcmp(data.data() + 8, "WEBP", 4) == 0) id = AV_CODEC_ID_WEBP;
    const AVCodec* codec = id == AV_CODEC_ID_NONE ? nullptr : avcodec_find_decoder(id);
    if (!codec) return false;
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool ok = false;
    if (ctx && pkt && frame && avcodec_open2(ctx, codec, nullptr) == 0 && av_new_packet(pkt, int(data.size())) == 0) {
        std::memcpy(pkt->data, data.data(), data.size());
        int rc = avcodec_send_packet(ctx, pkt);
        if (rc == 0) {
            rc = avcodec_receive_frame(ctx, frame);
            if (rc == AVERROR(EAGAIN)) {
                avcodec_send_packet(ctx, nullptr);
                rc = avcodec_receive_frame(ctx, frame);
            }
        }
        if (rc == 0 && frame->width > 0 && frame->height > 0) {
            SwsContext* sws = sws_getContext(frame->width, frame->height, AVPixelFormat(frame->format), frame->width,
                                             frame->height, AV_PIX_FMT_RGBA, SWS_POINT, nullptr, nullptr, nullptr);
            if (sws) {
                out.w = frame->width, out.h = frame->height;
                out.px.assign(size_t(out.w) * out.h * 4, 0);
                uint8_t* dst[1] = {out.px.data()};
                int stride[1] = {out.w * 4};
                sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, stride);
                sws_freeContext(sws);
                ok = true;
            }
        }
    }
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    return ok;
}

// Picture size from the PNG IHDR / JPEG SOF header (the VM thread asks sizes;
// it must not touch the renderer).
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

struct MemIo {
    const std::vector<uint8_t>* d;
    size_t pos = 0;
};
int memRead(void* op, uint8_t* buf, int size) {
    auto* m = static_cast<MemIo*>(op);
    const size_t n = std::min(size_t(size), m->d->size() - m->pos);
    if (n == 0) return AVERROR_EOF;
    std::memcpy(buf, m->d->data() + m->pos, n);
    m->pos += n;
    return int(n);
}
int64_t memSeek(void* op, int64_t off, int whence) {
    auto* m = static_cast<MemIo*>(op);
    if (whence & AVSEEK_SIZE) return int64_t(m->d->size());
    int64_t p = (whence & 3) == SEEK_SET ? off : (whence & 3) == SEEK_CUR ? int64_t(m->pos) + off : int64_t(m->d->size()) + off;
    if (p < 0 || p > int64_t(m->d->size())) return -1;
    m->pos = size_t(p);
    return p;
}

bool decodeAudio(const std::vector<uint8_t>& data, Pcm& out) {
    MemIo mem{&data};
    const int bufSize = 1 << 15;
    auto* buf = static_cast<uint8_t*>(av_malloc(bufSize));
    AVIOContext* io = avio_alloc_context(buf, bufSize, 0, &mem, memRead, nullptr, memSeek);
    AVFormatContext* fmt = avformat_alloc_context();
    fmt->pb = io;
    bool ok = false;
    AVCodecContext* ctx = nullptr;
    SwrContext* swr = nullptr;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (avformat_open_input(&fmt, nullptr, nullptr, nullptr) == 0) {
        avformat_find_stream_info(fmt, nullptr);
        const AVCodec* codec = nullptr;
        const int si = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
        if (si >= 0 && codec) {
            ctx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(ctx, fmt->streams[si]->codecpar);
            if (avcodec_open2(ctx, codec, nullptr) == 0) {
                AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
                out.rate = ctx->sample_rate;
                if (swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_S16, out.rate, &ctx->ch_layout, ctx->sample_fmt,
                                        ctx->sample_rate, 0, nullptr) == 0 &&
                    swr_init(swr) == 0) {
                    auto drain = [&] {
                        while (avcodec_receive_frame(ctx, frame) == 0) {
                            const int cap = swr_get_out_samples(swr, frame->nb_samples) + 64;
                            const size_t at = out.samples.size();
                            out.samples.resize(at + size_t(cap) * 2);
                            uint8_t* o[1] = {reinterpret_cast<uint8_t*>(out.samples.data() + at)};
                            const int n = swr_convert(swr, o, cap, const_cast<const uint8_t**>(frame->extended_data),
                                                      frame->nb_samples);
                            out.samples.resize(at + size_t(std::max(0, n)) * 2);
                        }
                    };
                    while (av_read_frame(fmt, pkt) == 0) {
                        if (pkt->stream_index == si && avcodec_send_packet(ctx, pkt) == 0) drain();
                        av_packet_unref(pkt);
                    }
                    avcodec_send_packet(ctx, nullptr);
                    drain();
                    ok = !out.samples.empty();
                }
            }
        }
    }
    swr_free(&swr);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    if (io) {
        av_freep(&io->buffer);
        avio_context_free(&io);
    }
    return ok;
}

// "<from 1.5 loop 22.073>bgm/1.ogg" -> file and options.
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

} // namespace

namespace {

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
    const Assets* assets = nullptr;
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
        if (data.empty()) data = assets->read(file);
        if (data.empty()) data = assets->read("gui/font/Aller_Rg.ttf");
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

// ---- host services ----------------------------------------------------------

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
    const Assets* assets = nullptr;
    TextEngine* text = nullptr;
    std::string persistentPath;
    std::map<std::string, Channel> channels;
    std::vector<Fading> fading;

    static const char* engineChannel(const std::string& ch) {
        return ch == "music" || ch == "music_poem" ? "BGM" : "Se";
    }

    bool imageSize(const std::string& file, int& w, int& h) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = sizes_.find(file);
        if (it == sizes_.end()) {
            int iw = 0, ih = 0;
            if (!headerSize(assets->read(file), iw, ih)) iw = ih = 0;
            it = sizes_.emplace(file, std::make_pair(iw, ih)).first;
        }
        w = it->second.first, h = it->second.second;
        return w > 0;
    }
    void textSize(const std::string& t, const TextStyle& style, float& w, float& h) override {
        text->measure(t, style, w, h);
    }

    std::shared_ptr<Pcm> pcm(const std::string& file) {
        auto it = pcm_.find(file);
        if (it != pcm_.end()) return it->second;
        auto p = std::make_shared<Pcm>();
        if (!decodeAudio(assets->read(file), *p)) {
            SDL_Log("DDLC: cannot decode audio %s", file.c_str());
            p.reset();
        }
        return pcm_[file] = p;
    }

    bool start(const std::string& ch, Channel& c, const std::string& spec, double fadein, bool loop) {
        const AudioSpec a = parseSpec(spec);
        auto p = pcm(a.file);
        c.spec.clear();
        if (!p) return false;
        if (!c.player) c.player = std::make_unique<Kotonoha::PcmPlayer>();
        Kotonoha::PcmPlayer::Clip clip;
        clip.samples = p->samples;
        clip.channels = 2;
        clip.rate = p->rate;
        clip.loop = loop;
        clip.loopStart = a.loop > 0 ? size_t(a.loop * p->rate) : 0;
        clip.startFrame = size_t(a.from * p->rate);
        clip.gain = fadein > 0 ? 0.f : 1.f;
        if (!c.player->Play(static_cast<Kotonoha::Sound*>(global.sound), engineChannel(ch), std::move(clip)))
            return false;
        c.spec = spec;
        c.parsed = a;
        c.loop = loop;
        c.duration = p->seconds();
        c.fadeFrom = fadein > 0 ? 0 : 1, c.fadeTo = 1, c.fadeStart = realNow(), c.fadeLen = fadein;
        return true;
    }

    void play(const std::string& ch, const std::string& spec, double fadein, bool loop) override {
        Channel& c = channels[ch];
        c.queued.clear();
        if (c.player) c.player->Stop();
        start(ch, c, spec, fadein, loop);
    }
    void queue(const std::string& ch, const std::string& spec, bool loop) override {
        Channel& c = channels[ch];
        if (c.player && c.player->Playing() && !c.spec.empty()) {
            c.queued = spec, c.queuedLoop = loop;
            return;
        }
        play(ch, spec, 0, loop);
    }
    void stop(const std::string& ch, double fadeout) override {
        auto it = channels.find(ch);
        if (it == channels.end()) return;
        Channel& c = it->second;
        c.queued.clear();
        c.spec.clear();
        if (!c.player) return;
        if (fadeout > 0 && c.player->Playing()) {
            fading.push_back({std::move(c.player), currentGain(c), realNow(), fadeout});
        } else {
            c.player->Stop();
        }
    }
    std::string playing(const std::string& ch) override {
        auto it = channels.find(ch);
        if (it == channels.end() || !it->second.player || !it->second.player->Playing()) return {};
        return it->second.spec;
    }
    double position(const std::string& ch) override {
        auto it = channels.find(ch);
        if (it == channels.end() || !it->second.player || !it->second.player->Playing()) return 0;
        const Channel& c = it->second;
        double p = c.player->Position();
        if (c.loop && p > c.duration) {
            const double ls = std::max(0.0, c.parsed.loop), span = c.duration - ls;
            if (span > 0) p = ls + std::fmod(p - c.duration, span);
        }
        return p;
    }
    void savePersistent(const std::string& t) override {
        std::ofstream(persistentPath, std::ios::binary) << t;
    }
    void log(const std::string& line) override { SDL_Log("DDLC: %s", line.c_str()); }

    std::string loadPersistent() const {
        std::ifstream in(persistentPath, std::ios::binary);
        std::stringstream s;
        s << in.rdbuf();
        return s.str();
    }

    static double currentGain(const Channel& c) {
        if (c.fadeLen <= 0) return c.fadeTo;
        const double k = std::clamp((realNow() - c.fadeStart) / c.fadeLen, 0.0, 1.0);
        return c.fadeFrom + (c.fadeTo - c.fadeFrom) * k;
    }

    // Fades and queues (main thread, every frame).
    void tick() {
        const double now = realNow();
        for (auto& [name, c] : channels) {
            if (!c.player) continue;
            c.player->SetGain(float(currentGain(c)));
            if (!c.queued.empty() && (!c.player->Playing() || c.player->Position() >= c.duration)) {
                const std::string q = c.queued;
                const bool l = c.queuedLoop;
                c.queued.clear();
                c.player->Stop();
                start(name, c, q, 0, l);
            }
        }
        for (auto it = fading.begin(); it != fading.end();) {
            const double k = std::clamp((now - it->start) / it->len, 0.0, 1.0);
            it->player->SetGain(float(it->gain * (1 - k)));
            if (k >= 1 || !it->player->Playing()) {
                it->player->Stop();
                it = fading.erase(it);
            } else {
                ++it;
            }
        }
    }
    void stopAll() {
        for (auto& [_, c] : channels)
            if (c.player) c.player->Stop();
        channels.clear();
        for (auto& f : fading) f.player->Stop();
        fading.clear();
    }

private:
    std::mutex mu_;
    std::map<std::string, std::pair<int, int>> sizes_;
    std::map<std::string, std::shared_ptr<Pcm>> pcm_;
};

} // namespace

namespace {

struct Tex {
    std::unique_ptr<Kotonoha::CanvasImage> img;
    int w = 0, h = 0;
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
    Assets assets;
    Script script;
    Host host;
    TextEngine text;
    std::unique_ptr<Vm> vm;
    Phase phase = Phase::Boot;
    Kotonoha::GameplayFlow* flow = nullptr;
    bool canvasReady = false;
    SDL_FRect place{0, 0, kW, kH};
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
    bool ctrl = false, skipToggle = false, autoMode = false;
    bool pauseMenu = false, nameInput = false;
    std::string nameText, notice;
    std::vector<Button> buttons, drawn;
    std::string hoverKey;
    float poemScroll = 0, poemMax = 0;
    std::unique_ptr<Kotonoha::PcmPlayer> uiSfx;

    // Drawing state.
    std::map<std::string, Tex> images;
    std::map<std::string, Rgba> masks;
    uint64_t frame = 0;
    float scale = 1; // canvas pixels per logical pixel
    SDL_Texture* target = nullptr;
    SDL_Texture* rt = nullptr;
    SDL_Texture* maskTex = nullptr;
    int rtW = 0, rtH = 0, maskW = 0, maskH = 0;

    ~Game() {
        vm.reset();
        host.stopAll();
        if (uiSfx) uiSfx->Stop();
        images.clear();
        text.clear();
        if (rt) SDL_DestroyTexture(rt);
        if (maskTex) SDL_DestroyTexture(maskTex);
    }

    bool skipping() const { return ctrl || skipToggle; }

    void sfx(const char* file) {
        auto p = host.pcm(file);
        if (!p) return;
        if (!uiSfx) uiSfx = std::make_unique<Kotonoha::PcmPlayer>();
        Kotonoha::PcmPlayer::Clip c;
        c.samples = p->samples;
        c.channels = 2;
        c.rate = p->rate;
        uiSfx->Play(static_cast<Kotonoha::Sound*>(global.sound), "Se", std::move(c));
    }

    // ---- primitives (logical 1280x720 coordinates, render scale set) ----

    Tex* image(SDL_Renderer* r, const std::string& file) {
        Tex& t = images[file];
        t.used = frame;
        if (t.failed) return nullptr;
        if (t.img) return &t;
        Rgba px;
        if (!decodeImage(assets.read(file), px)) {
            SDL_Log("DDLC: cannot decode image %s", file.c_str());
            t.failed = true;
            return nullptr;
        }
        SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, px.w, px.h);
        if (!tex) {
            t.failed = true;
            return nullptr;
        }
        SDL_UpdateTexture(tex, nullptr, px.px.data(), px.w * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
        t.img = std::make_unique<Kotonoha::CanvasImage>(tex);
        t.w = px.w, t.h = px.h;
        return &t;
    }

    void evictImages() {
        for (auto it = images.begin(); it != images.end();) {
            if (!it->second.failed && frame - it->second.used > 900) it = images.erase(it);
            else ++it;
        }
    }

    // A textured quad: `src` in texture pixels, `local` the source-space rectangle mapped by m.
    void quad(SDL_Renderer* r, Kotonoha::CanvasImage* ci, SDL_Texture* tex, float tw, float th, const SDL_FRect& src,
              const SDL_FRect& local, const float* m, float alpha) {
        if (m[1] == 0 && m[3] == 0 && m[0] > 0 && m[4] > 0) {
            const SDL_FRect dst{m[0] * local.x + m[2], m[4] * local.y + m[5], m[0] * local.w, m[4] * local.h};
            if (ci) {
                SDL_SetTextureAlphaModFloat(ci->Source(), alpha);
                ci->Draw(r, &src, &dst);
            } else if (tex) {
                SDL_SetTextureAlphaModFloat(tex, alpha);
                SDL_RenderTexture(r, tex, &src, &dst);
            }
            return;
        }
        SDL_Texture* t = ci ? ci->Display(r) : tex;
        if (!t) return;
        SDL_SetTextureAlphaModFloat(t, 1);
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
        SDL_RenderGeometry(r, t, vx, 4, idx, 6);
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
            quad(r, t->img.get(), nullptr, float(t->w), float(t->h), {cx * sx, cy * sy, cw * sx, ch * sy},
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
        quad(r, nullptr, t->tex, t->w, t->h, {0, 0, t->w, t->h}, {-p, -p, t->w / t->scale, t->h / t->scale}, m,
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
        if (w < 0) w = float(t->w);
        if (h < 0) h = float(t->h);
        const float m[6] = {w / float(t->w), 0, x, 0, h / float(t->h), y};
        quad(r, t->img.get(), nullptr, float(t->w), float(t->h), {0, 0, float(t->w), float(t->h)},
             {0, 0, float(t->w), float(t->h)}, m, alpha);
    }

    // Ren'Py Frame(): a 9-slice picture.
    void drawFrame(SDL_Renderer* r, const std::string& file, const SDL_FRect& dst, float l, float t, float rr, float b) {
        Tex* tx = image(r, file);
        if (!tx) return;
        SDL_SetTextureAlphaModFloat(tx->img->Source(), 1);
        SDL_RenderTexture9Grid(r, tx->img->Source(), nullptr, l, rr, t, b, 1.f, &dst);
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
        if (Tex* tb = image(r, box)) drawImage(r, box, (kW - float(tb->w)) / 2, kWinTop + kWinH - float(tb->h));
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
        else openPause(true);
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

    void drawScreenCall(SDL_Renderer* r, const Interaction& in) {
        if (in.screen == "confirm") {
            const Value yes = screenArg(in, 1, "yes_action"), no = screenArg(in, 2, "no_action");
            drawConfirm(r, in.message, {{"Yes", [this, yes] { resume(yes); }}, {"No", [this, no] { resume(no); }}},
                        nullptr);
        } else {
            const Value ok = screenArg(in, 1, "ok_action");
            drawConfirm(r, in.message, {{"OK", [this, ok] { resume(ok.isNone() ? Value::boolean(true) : ok); }}},
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

    void drawScene(SDL_Renderer* r, const Scene& sc, double t, bool live) {
        drawRender(r, vm->renderLayer(sc.master, t));
        for (auto& s : sc.screens)
            if (s.name == "poem") drawPoem(r, s, t);
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
        if (!rt) return paint();
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
            std::string file = assets.resolveImage(tr.mask);
            if (file.empty()) file = tr.mask;
            if (!decodeImage(assets.read(file), m)) SDL_Log("DDLC: cannot decode mask %s", tr.mask.c_str());
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
        vm = std::make_unique<Vm>(assets, script, host);
        vm->clock = [this] { return now(); };
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
        SDL_StopTextInput(global.window);
        host.stop("music", 1.0);
        newVm();
        phase = Phase::Play;
        vm->start("start");
        reportErrors();
    }

    void returnToMenu() {
        pauseMenu = false;
        freeze(false);
        host.stopAll();
        newVm();
        enterMenu();
    }

    void openPause(bool on) {
        if (phase != Phase::Play) return;
        pauseMenu = on;
        freeze(on);
    }

    void resume(Value v = {}) {
        if (!vm || !vm->running() || vm->finished()) return;
        const bool poem = vm->scene.hasScreen("poem");
        vm->resume(std::move(v));
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

    void drawMainMenu(SDL_Renderer* r) {
        const double t = now();
        const auto& m = vm->scene.master;
        const size_t below = std::min<size_t>(4, m.size());
        drawRender(r, vm->renderLayer(std::vector<Shown>(m.begin(), m.begin() + long(below)), t));
        const double pt = vm->py.getAttr(vm->persistent, "playthrough").num();
        const std::string newGame = pt == 1 ? "\xC5\x94\xC5\x97\xC3\xB1\xC4\xAE\xC2\xBC\xC2\xBB\xC5\xA7\xC3\xBE\xC5\x80\xC3"
                                              "\x82\xC5\xBB\xC5\x95\xC4\x9B\xC5\x8D\xC3\xAC\xC2\xAB"
                                            : "New Game";
        if (!nameInput && notice.empty()) {
            navigation(r, {{newGame,
                            [this] {
                                if (vm->py.getAttr(vm->persistent, "playername").truthy()) return startGame();
                                nameInput = true;
                                nameText.clear();
                                SDL_StartTextInput(global.window);
                            }},
                           {"Load Game", [this] { notice = "Saving and loading are not available in this port yet."; }},
                           {"Settings", [this] { notice = "Settings are not available in this port yet."; }},
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
        drawn.clear();
        drawImage(r, "gui/overlay/game_menu.png", 0, 0, kW, kH);
        navigation(r, {{"Return", [this] { openPause(false); }},
                       {"Main Menu", [this] { returnToMenu(); }},
                       {"Quit", [this] { quit = true; }}});
    }

    void drawAll(SDL_Renderer* r) {
        drawn.clear();
        if (vm && phase == Phase::Menu) {
            drawMainMenu(r);
        } else if (vm && (phase == Phase::Splash || phase == Phase::Play)) {
            const Interaction& in = vm->interaction();
            const double t = now();
            if (in.k == Interaction::K::Transition && in.trans) {
                drawTransition(r, in, t);
            } else {
                drawScene(r, vm->scene, t, true);
                if (in.k == Interaction::K::Menu) drawChoices(r, in);
                else if (in.k == Interaction::K::Screen) drawScreenCall(r, in);
                else if (in.k == Interaction::K::Ui) drawPoemGame(r, in);
            }
            if (pauseMenu) drawPauseMenu(r);
        }
        buttons.swap(drawn);
        bool any = false;
        for (auto& b : buttons) any |= inside(b.r);
        if (!any) hoverKey.clear();
    }

    static Kotonoha_Scene_Status drawLayer(KOTONOHA_SCENE_CALL) {
        (void)window;
        auto* g = static_cast<Game*>(userData);
        float w = 0, h = 0;
        if (!g || !target || !SDL_GetTextureSize(target, &w, &h)) return KOTONOHA_SCENE_NULL;
        g->target = target;
        g->scale = w / kW;
        SDL_SetRenderScale(render, w / kW, h / kH);
        SDL_SetRenderDrawColor(render, 0, 0, 0, 255);
        SDL_RenderClear(render);
        g->drawAll(render);
        SDL_SetRenderScale(render, 1, 1);
        return KOTONOHA_SCENE_DRAW;
    }

    static void presentation(SDL_Renderer* r, int w, int h, SDL_RendererLogicalPresentation mode) {
        SDL_Texture* t = SDL_GetRenderTarget(r);
        if (t) SDL_SetRenderTarget(r, nullptr);
        SDL_SetRenderLogicalPresentation(r, w, h, mode);
        if (t) SDL_SetRenderTarget(r, t);
    }

    SDL_AppResult renderFrame(Kotonoha_Game* game) {
        if (quit) return SDL_APP_SUCCESS;
        SDL_Renderer* r = game->render;
        text.frame = ++frame;
        update();
        if (quit) return SDL_APP_SUCCESS;
        SDL_SetRenderTarget(r, nullptr);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        presentation(r, int(kW), int(kH), SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_FRect p{0, 0, kW, kH};
        SDL_GetRenderLogicalPresentationRect(r, &p);
        presentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
        place = {SDL_roundf(p.x), SDL_roundf(p.y), SDL_max(1.f, SDL_roundf(p.w)), SDL_max(1.f, SDL_roundf(p.h))};
        Kotonoha::Canvas& canvas = flow->SceneCanvas();
        if (!canvasReady) {
            canvas.RegisterCanva(drawLayer, 0, place, this, "game");
            canvasReady = true;
        } else {
            canvas.UpdateCanva(drawLayer, -1, place);
        }
        canvas.RenderCanvas(game->window, r);
        Kotonoha::Debug::SetSceneCanvas(&canvas);
        text.evict();
        if (frame % 300 == 0) evictImages();
        return SDL_APP_CONTINUE;
    }

    // Window points -> logical 1280x720 coordinates.
    void toLogical(float x, float y) {
        const float d = SDL_GetWindowPixelDensity(global.window);
        mx = (x * d - place.x) * kW / place.w;
        my = (y * d - place.y) * kH / place.h;
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

    SDL_AppResult handleEvent(SDL_Event* e) {
        switch (e->type) {
        case SDL_EVENT_QUIT: return SDL_APP_SUCCESS;
        case SDL_EVENT_MOUSE_MOTION: toLogical(e->motion.x, e->motion.y); break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            toLogical(e->button.x, e->button.y);
            if (e->button.button == SDL_BUTTON_LEFT) click();
            else if (e->button.button == SDL_BUTTON_RIGHT) openPause(!pauseMenu);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            poemScroll = std::clamp(poemScroll - e->wheel.y * 60.f, 0.f, poemMax);
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (nameInput)
                for (const char* c = e->text.text; *c; ++c)
                    if (std::isalpha((unsigned char)*c) && nameText.size() < 12) nameText += *c;
            break;
        case SDL_EVENT_KEY_DOWN: {
            const SDL_Keycode k = e->key.key;
            if (k == SDLK_LCTRL || k == SDLK_RCTRL) ctrl = true;
            if (nameInput) {
                if (k == SDLK_BACKSPACE && !nameText.empty()) nameText.pop_back();
                else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) submitName();
                else if (k == SDLK_ESCAPE) nameInput = false, SDL_StopTextInput(global.window);
                break;
            }
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
        return quit ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
    }
};

Game* g_game = nullptr;

// The session only hosts the port: it owns the whole front end.
class DdlcSession : public Kotonoha::GameSession {
public:
    explicit DdlcSession(Game& g) : g_(g) {}
    bool Start() override { return true; }
    void Step(int) override {}
    bool Choosing() const override { return false; }
    std::vector<std::string> Choices() const override { return {}; }
    bool HasMessage() const override { return false; }
    std::string Speaker() const override { return {}; }
    std::string Text() const override { return {}; }
    void Draw(SDL_Renderer*) override {}
    bool OwnsFrontend() const override { return true; }
    SDL_AppResult HandleEvent(SDL_Event* e, Kotonoha_Game*) override { return g_.handleEvent(e); }
    SDL_AppResult RenderFrame(::Kotonoha::Kotonoha*, Kotonoha_Game* game) override { return g_.renderFrame(game); }

private:
    Game& g_;
};

} // namespace

SDL_AppResult LocalArgs(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (argv[i] && std::string(argv[i]) == "--ddlc") {
            g_dir = argv[i + 1];
            argv[i][0] = '!';
            argv[i + 1][0] = '!';
            break;
        }
    }
    if (g_dir.empty())
        if (const char* e = std::getenv("DDLC_DIR")) g_dir = e;
    return SDL_APP_CONTINUE;
}

SDL_AppResult LocalInit(void**, int, char*[]) {
    if (g_dir.empty()) {
        SDL_Log("DDLC: use --ddlc <DDLC game dir>");
        return SDL_APP_FAILURE;
    }
    global.config.enableDebugUi = std::getenv("KOTONOHA_DEBUG_UI") != nullptr;
    auto* g = new Game();
    g_game = g;
    if (!g->assets.open(g_dir)) {
        SDL_Log("DDLC: cannot open the archives in %s", g_dir.c_str());
        return SDL_APP_FAILURE;
    }
    std::string err;
    if (!g->script.load(g->assets, &err)) {
        SDL_Log("DDLC: script load failed: %s", err.c_str());
        return SDL_APP_FAILURE;
    }
    g->host.assets = &g->assets;
    g->host.text = &g->text;
    g->text.assets = &g->assets;
    if (char* pref = SDL_GetPrefPath("Kotonoha", "ddlc")) {
        g->host.persistentPath = std::string(pref) + "persistent.txt";
        SDL_free(pref);
    }
    g->persistentText = g->host.loadPersistent();

    Kotonoha::GameplayFlow::Config cfg;
    cfg.id = "ddlc";
    cfg.title = "Doki Doki Literature Club!";
    cfg.width = kW;
    cfg.height = kH;
    g->flow = new Kotonoha::GameplayFlow(cfg);
    if (!g->flow->Attach(std::make_unique<DdlcSession>(*g))) {
        SDL_Log("DDLC: session start failed");
        return SDL_APP_FAILURE;
    }
    SDL_SetWindowTitle(global.window, "Doki Doki Literature Club!");
    return SDL_APP_CONTINUE;
}

SDL_AppResult LocalEvent(void*, SDL_Event* e) {
    if (g_game && g_game->flow) return g_game->flow->HandleEvent(e, &global);
    return SDL_APP_CONTINUE;
}

SDL_AppResult LocalIterate(void* appstate) {
    if (!g_game || !g_game->flow) return SDL_APP_CONTINUE;
    Kotonoha::Kotonoha* app = Kotonoha::GetApp(appstate);
    TTF_Font* font = nullptr;
    if (app) {
        auto it = app->fonts.find("NotoSansJP-Regular");
        if (it != app->fonts.end()) font = it->second;
    }
    Kotonoha::MarkFrameHandled();
    return g_game->flow->RunFrame(app, &global, font);
}

void LocalQuit(void*, SDL_AppResult) {
    if (g_game) {
        if (g_game->vm) g_game->host.savePersistent(g_game->vm->savePersistentText());
        delete g_game->flow;
        g_game->flow = nullptr;
        delete g_game;
    }
    g_game = nullptr;
}
