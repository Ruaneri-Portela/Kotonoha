#pragma once
#include <SummerRadish/Pack.hpp>
namespace SummerRadish {
// Graphic.sdll FUN_10003460, with its native 0..1000 WipeSpeed setting.
int wipeDuration(int type, int speed = 500);
// Wipe_XFade.mod: 128 quantized weights, doubled, with MMX sum then >>8.
Image wipeFrame(const Image &from, const Image &to, int type, int elapsed,
                int duration);
} // namespace SummerRadish
