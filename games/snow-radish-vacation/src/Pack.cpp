#include "SnowRadish/Pack.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace SnowRadish {

size_t lzssDecode(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize)
{
    std::array<uint8_t, 4096> window{};
    size_t wp = 0xFEE, in = 0, out = 0;
    while (out < dstSize && in < srcSize) {
        uint8_t flags = src[in++];
        for (int bit = 7; bit >= 0 && out < dstSize; --bit) {
            if (in >= srcSize) return in;
            if (flags & (1 << bit)) {
                if (in + 2 > srcSize) return in;
                unsigned d = src[in] | (src[in + 1] << 8);
                in += 2;
                unsigned len = (d & 0xF) + 3, pos = d >> 4;
                for (unsigned i = 0; i < len && out < dstSize; ++i) {
                    uint8_t c = window[(pos + i) & 0xFFF];
                    dst[out++] = c;
                    window[wp++ & 0xFFF] = c;
                }
            } else {
                uint8_t c = src[in++];
                dst[out++] = c;
                window[wp++ & 0xFFF] = c;
            }
        }
    }
    return in;
}

static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

std::vector<PackEntry> readDirectory(const std::vector<uint8_t>& pack)
{
    std::vector<PackEntry> entries;
    if (pack.size() < 12) return entries;
    const size_t tail = pack.size() - 12;
    const uint32_t comp = rd32(&pack[tail]) ^ 0xF0F0F0F0u;
    const uint32_t raw = rd32(&pack[tail + 4]) ^ 0xF0F0F0F0u;
    if (comp == 0 || comp > tail || raw < 80 || raw > (64u << 20)) return entries;
    const size_t dirStart = tail - comp;
    std::vector<uint8_t> dir(raw);
    lzssDecode(pack.data() + dirStart, comp, dir.data(), dir.size());

    const size_t count = raw / 80;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* r = &dir[80 * i];
        PackEntry e;
        e.id = rd32(r);
        const char* n = reinterpret_cast<const char*>(r + 4);
        e.name.assign(n, strnlen(n, 64));
        e.offset = rd32(r + 68);
        e.compressedSize = rd32(r + 72);
        e.rawSize = rd32(r + 76);
        if (e.offset > dirStart || e.compressedSize > dirStart - e.offset) continue;
        entries.push_back(std::move(e));
    }
    return entries;
}

std::vector<uint8_t> extractEntry(const std::vector<uint8_t>& pack, const PackEntry& entry)
{
    std::vector<uint8_t> out;
    if (entry.offset >= pack.size()) return out;
    const size_t avail = std::min<size_t>(entry.compressedSize, pack.size() - entry.offset);
    const bool known = entry.rawSize != 0;
    if (known && entry.compressedSize == entry.rawSize) {
        // Stored without compression.
        out.assign(pack.begin() + entry.offset, pack.begin() + entry.offset + avail);
        return out;
    }
    // Worst case is 18 output bytes per 2 source bytes plus flags.
    out.resize(known ? entry.rawSize : avail * 10);
    const size_t produced = lzssDecode(pack.data() + entry.offset, avail, out.data(), out.size());
    (void)produced;
    if (!known) {
        while (!out.empty() && out.back() == 0) out.pop_back();
    }
    return out;
}

bool decodeEvd(const std::vector<uint8_t>& d, Image& img)
{
    if (d.size() < 12 || (rd32(&d[0]) != 3 && rd32(&d[0]) != 0)) return false;
    const uint32_t w = rd32(&d[4]), h = rd32(&d[8]);
    if (w == 0 || h == 0 || w > 8192 || h > 8192) return false;

    static const std::vector<uint32_t> table = [] {
        std::vector<uint32_t> t(0x8000);
        for (int i = 0; i < 0x8000; ++i) {
            int r = (i >> 10) & 31, g = (i >> 5) & 31, b = i & 31;
            if (r > 15) r -= 32;
            if (g > 15) g -= 32;
            if (b > 15) b -= 32;
            t[i] = static_cast<uint32_t>((r << 16) + (g << 8) + b);
        }
        return t;
    }();

    const size_t total = static_cast<size_t>(w) * h;
    std::vector<uint8_t> bgr(total * 3);
    size_t p = 12, px = 0;
    uint32_t cur = 0;
    auto put = [&](uint32_t c) {
        bgr[px * 3] = c & 255;
        bgr[px * 3 + 1] = (c >> 8) & 255;
        bgr[px * 3 + 2] = (c >> 16) & 255;
        ++px;
    };
    const bool raw = rd32(&d[0]) == 0; // type 0: uncompressed BGR
    if (raw) {
        if (d.size() < 12 + total * 3) return false;
        std::copy(d.begin() + 12, d.begin() + 12 + long(total * 3), bgr.begin());
        px = total;
    }
    while (px < total && p < d.size()) {
        const uint8_t a = d[p];
        if (a < 0x80) {
            if (p + 2 > d.size()) return false;
            cur += table[(a << 8) | d[p + 1]];
            p += 2;
            put(cur);
        } else if (a < 0xC0) {
            ++p;
            for (int n = a - 0x7F; n > 0 && px < total; --n) put(cur);
        } else if (a == 0xFF) {
            ++p; // end-of-line marker, not used in full-frame streams
        } else {
            ++p;
            for (int n = a - 0xBF; n > 0 && px < total; --n) {
                if (p + 3 > d.size()) return false;
                cur = d[p] | (d[p + 1] << 8) | (d[p + 2] << 16);
                p += 3;
                put(cur);
            }
        }
    }
    if (px != total) return false;

    img.width = w;
    img.height = h;
    img.rgb.resize(total * 3);
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* s = &bgr[static_cast<size_t>(h - 1 - y) * w * 3];
        uint8_t* o = &img.rgb[static_cast<size_t>(y) * w * 3];
        for (uint32_t x = 0; x < w; ++x) {
            o[x * 3] = s[x * 3 + 2];
            o[x * 3 + 1] = s[x * 3 + 1];
            o[x * 3 + 2] = s[x * 3];
        }
    }
    return true;
}

bool decodeAlpha(const std::vector<uint8_t>& d, bool alternate, AlphaMask& out)
{
    const uint32_t width = alternate ? 250 : 200;
    const uint32_t height = alternate ? 200 : 150;
    if (d.size() != size_t(width) * height) return false;
    out.width = width;
    out.height = height;
    out.alpha = d;
    return true;
}

} // namespace SnowRadish

namespace SnowRadish {

bool decodeAnm(const std::vector<uint8_t>& d, Animation& out)
{
    out.frames.clear();
    if (d.size() < 0x684 || (rd32(d.data()) != 1 && rd32(d.data()) != 2)) return false;

    const uint32_t x = rd32(&d[76]), y = rd32(&d[80]);
    const uint32_t width = rd32(&d[92]), height = rd32(&d[96]);
    if (!width || !height || width > 8192 || height > 8192 || x > 8192 - width || y > 8192 - height)
        return false;

    for (size_t i = 0; i < 8; ++i) {
        const uint32_t offset = rd32(&d[12 + i * 4]);
        const uint32_t size = rd32(&d[44 + i * 4]);
        if (size == 0) break;
        if (offset < 0x684 || offset > d.size() || size > d.size() - offset) {
            out.frames.clear();
            return false;
        }
        std::vector<uint8_t> bytes(d.begin() + offset, d.begin() + offset + size);
        Sprite frame;
        if (bytes.size() >= 24 && rd32(bytes.data()) == 1 && decodeChd(bytes, frame)) {
            out.frames.push_back(std::move(frame));
            continue;
        }
        if (bytes.size() != size_t(width) * height * 3) {
            out.frames.clear();
            return false;
        }
        frame.width = width;
        frame.height = height;
        frame.x = int32_t(x);
        frame.y = int32_t(y);
        frame.rgba.resize(size_t(width) * height * 4);
        for (uint32_t row = 0; row < height; ++row) {
            const uint8_t* source = bytes.data() + size_t(height - 1 - row) * width * 3;
            uint8_t* target = frame.rgba.data() + size_t(row) * width * 4;
            for (uint32_t col = 0; col < width; ++col) {
                target[col * 4] = source[col * 3 + 2];
                target[col * 4 + 1] = source[col * 3 + 1];
                target[col * 4 + 2] = source[col * 3];
                target[col * 4 + 3] = 255;
            }
        }
        out.frames.push_back(std::move(frame));
    }
    return out.frames.size() >= 2;
}

bool decodeChd(const std::vector<uint8_t>& d, Sprite& s)
{
    if (d.size() < 24) return false;
    auto u32 = [&](size_t o) { return uint32_t(d[o]) | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24; };
    if (u32(0) != 1) return false;
    constexpr uint32_t W = 800, H = 600;
    std::vector<uint8_t> canvas(size_t(W) * H * 4, 0);
    uint32_t minX = W, minY = H, maxX = 0, maxY = 0;
    auto put = [&](uint32_t x, uint32_t y, size_t p, uint8_t a) {
        if (x >= W || a == 0) return;
        uint8_t* px = &canvas[(size_t(y) * W + x) * 4];
        px[0] = d[p + 2];
        px[1] = d[p + 1];
        px[2] = d[p];
        px[3] = a;
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    };
    size_t p = 20;
    for (uint32_t row = 0; row < H && p < d.size(); ++row) {
        const uint32_t y = H - 1 - row;
        uint32_t x = 0;
        while (p < d.size()) {
            const uint8_t c = d[p++];
            if (c == 0xFF) break;
            if (c >= 0x9F) {
                x += c - 0x9E;
                continue;
            }
            if (p + 3 > d.size()) return false;
            if (c >= 0x7F) {
                for (uint32_t n = c - 0x7E; n > 0; --n, ++x) put(x, y, p, 255);
            } else {
                put(x, y, p, uint8_t(std::min(255, c * 255 / 0x67)));
                ++x;
            }
            p += 3;
        }
    }
    if (minX > maxX || minY > maxY) {
        s.width = s.height = 1;
        s.x = s.y = 0;
        s.rgba.assign(4, 0);
        return true;
    }
    s.x = int32_t(minX);
    s.y = int32_t(minY);
    s.width = maxX - minX + 1;
    s.height = maxY - minY + 1;
    s.rgba.resize(size_t(s.width) * s.height * 4);
    for (uint32_t y = 0; y < s.height; ++y)
        std::copy_n(&canvas[(size_t(minY + y) * W + minX) * 4], s.width * 4, &s.rgba[size_t(y) * s.width * 4]);
    return true;
}

} // namespace SnowRadish

#include "AdpsTables.inc"

namespace SnowRadish {

Pcm decodeAdps(const uint8_t* data, size_t size)
{
    Pcm pcm;
    pcm.samples.reserve(size * 2);
    int state[2] = {0, 0}, pred[2] = {0, 0};
    for (size_t i = 0; i + 1 < size; i += 2) {
        int16_t frame[2][2];
        for (int ch = 0; ch < 2; ++ch) {
            const uint8_t by = data[i + ch];
            for (int n = 0; n < 2; ++n) {
                const int nib = n ? by >> 4 : by & 15;
                const int k = state[ch] + nib;
                pred[ch] = std::clamp(pred[ch] + kAdpsDelta[k], -32768, 32767);
                state[ch] = kAdpsNext[k];
                frame[ch][n] = static_cast<int16_t>(pred[ch]);
            }
        }
        for (int n = 0; n < 2; ++n) {
            pcm.samples.push_back(frame[0][n]);
            pcm.samples.push_back(frame[1][n]);
        }
    }
    return pcm;
}

bool decodeAdpsFile(const std::vector<uint8_t>& d, AdpsTrack& out)
{
    out = AdpsTrack{};
    if (d.size() < 12) return false;
    const uint32_t introSize = rd32(&d[0]);
    auto block = [&](size_t at, Pcm& pcm) {
        if (at + 4 > d.size()) return false;
        const size_t n = std::min<size_t>(rd32(&d[at]), d.size() - at - 4);
        pcm = decodeAdps(&d[at + 4], n);
        return true;
    };
    if (!block(8, out.intro)) return false;
    if (size_t(8) + introSize < d.size()) block(8 + size_t(introSize), out.loop);
    return true;
}

Pcm decodeAdp4(const std::vector<uint8_t>& d)
{
    Pcm pcm;
    pcm.channels = 1;
    if (d.size() < 4) return pcm;
    const uint32_t units = rd32(&d[0]);
    pcm.samples.reserve(size_t(units) * 2);
    size_t p = 4;
    uint32_t done = 0, flags = 0;
    int state = 0, pred = 0;
    uint8_t key = 0;
    while (done < units && p < d.size()) {
        if (flags < 0x100) flags = d[p++] | 0x8000u;
        if (p >= d.size()) break;
        uint32_t len = d[p++];
        if (len < 0x80) {
            if (p >= d.size()) break;
            len = (len << 8) | d[p++];
        } else {
            len &= 0x7F;
        }
        const uint32_t run = len + 1;
        done += run;
        if (flags & 1) {
            state = pred = 0;
            pcm.samples.insert(pcm.samples.end(), size_t(run) * 2, 0);
        } else {
            for (uint32_t i = 0; i < run && p < d.size(); ++i) {
                const uint8_t by = d[p++] ^ key++;
                for (int n = 0; n < 2; ++n) {
                    const int k = state + (n ? by >> 4 : by & 15);
                    pred = std::clamp(pred + kAdpsDelta[k], -0x7FFF, 0x7FFF);
                    state = kAdpsNext[k];
                    pcm.samples.push_back(static_cast<int16_t>(pred));
                }
            }
        }
        flags >>= 1;
    }
    return pcm;
}

} // namespace SnowRadish
