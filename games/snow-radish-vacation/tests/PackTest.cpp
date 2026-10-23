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
    std::puts("OK");
    return 0;
}
