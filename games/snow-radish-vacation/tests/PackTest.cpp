#include "SnowRadish/Pack.hpp"
#include <cstdio>
#include <vector>

int main()
{
    // flags 0x20: lit 'a', lit 'b', match to window 0xFEE (the 'a' just written), len 3.
    const uint8_t src[] = {0x20, 'a', 'b', 0xE0, 0xFE};
    uint8_t out[4] = {};
    SnowRadish::lzssDecode(src, sizeof src, out, 4);
    if (out[0] != 'a' || out[1] != 'b' || out[2] != 'a' || out[3] != 'b') { std::puts("FAIL literal"); return 1; }
    // 2x2 EVD: literal BGR(1,2,3), then a run of 3 repeating it. Rows are bottom-up.
    const std::vector<uint8_t> evd = {3, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 0xC0, 1, 2, 3, 0x82};
    SnowRadish::Image img;
    if (!SnowRadish::decodeEvd(evd, img) || img.width != 2 || img.height != 2 || img.rgb[0] != 3 ||
        img.rgb[1] != 2 || img.rgb[2] != 1 || img.rgb[11] != 1) { std::puts("FAIL evd"); return 1; }
    SnowRadish::AlphaMask mask;
    std::vector<uint8_t> alpf(200 * 150, 128);
    if (!SnowRadish::decodeAlpha(alpf, false, mask) || mask.width != 200 || mask.height != 150 ||
        mask.alpha.front() != 128) { std::puts("FAIL ALPF"); return 1; }
    std::vector<uint8_t> alpa(250 * 200, 64);
    if (!SnowRadish::decodeAlpha(alpa, true, mask) || mask.width != 250 || mask.height != 200 ||
        mask.alpha.back() != 64) { std::puts("FAIL ALPA"); return 1; }
    alpa.pop_back();
    if (SnowRadish::decodeAlpha(alpa, true, mask)) { std::puts("FAIL alpha size"); return 1; }
    std::vector<uint8_t> anm(0x684 + 18, 0);
    auto set32 = [&](size_t offset, uint32_t value) {
        anm[offset] = uint8_t(value);
        anm[offset + 1] = uint8_t(value >> 8);
        anm[offset + 2] = uint8_t(value >> 16);
        anm[offset + 3] = uint8_t(value >> 24);
    };
    set32(0, 2);
    set32(4, 2);
    set32(12, 0x684);
    set32(16, 0x684 + 6);
    set32(20, 0x684 + 12);
    set32(44, 6);
    set32(48, 6);
    set32(52, 6);
    set32(76, 4);
    set32(80, 8);
    set32(92, 2);
    set32(96, 1);
    anm[0x684] = 1; anm[0x685] = 2; anm[0x686] = 3;
    anm[0x687] = 4; anm[0x688] = 5; anm[0x689] = 6;
    anm[0x68a] = 7; anm[0x68b] = 8; anm[0x68c] = 9;
    anm[0x68d] = 10; anm[0x68e] = 11; anm[0x68f] = 12;
    anm[0x690] = 13; anm[0x691] = 14; anm[0x692] = 15;
    SnowRadish::Animation animation;
    if (!SnowRadish::decodeAnm(anm, animation) || animation.frames.size() != 3 ||
        animation.frames[0].width != 2 || animation.frames[0].height != 1 ||
        animation.frames[0].x != 4 || animation.frames[0].rgba[0] != 3 ||
        animation.frames[2].rgba[0] != 15) { std::puts("FAIL ANM"); return 1; }
    std::puts("OK");
    return 0;
}
