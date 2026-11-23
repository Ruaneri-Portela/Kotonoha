#pragma once
#include <SummerRadish/Pack.hpp>
#include <SummerRadish/Codec.hpp>
#include <SummerRadish/Images.hpp>

namespace SummerRadish {
// Graphic.sdll FUN_10003460, with its native 0..1000 WipeSpeed setting.
int wipeDuration(int type, int speed = 500);
// Returns the .ALP rule-mask asset path for types 0x100..0x6ff, or empty.
std::string wipeMaskAsset(int type);
// Wipe_XFade.mod / Wipe_AlphaFade.mod transition compositor.
Image wipeFrame(const Image &from, const Image &to, int type, int elapsed,
                int duration, const Image *mask = nullptr, int option = 0);
} // namespace SummerRadish
