#pragma once

#include <Kotonoha/routing/SchoolDaysTopMeterModel.hpp>

namespace Kotonoha {

// Pure projection of FILM::MenuBar::drawFeeling (SysMenu 0x10026540).
// Coordinates are still in the retail MenuBar logical surface.  The caller
// may subsequently fit that surface into any physical SDL render output; this
// class deliberately knows nothing about monitor/window resolution.
class SchoolDaysTopMeterRenderPlan {
public:
    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    enum class Sprite : unsigned char {
        NegativeSide = 0, // native this+0x98
        PositiveSide = 1, // native this+0x9C
        Center = 2,       // native this+0xA0
    };

    struct Draw {
        Sprite sprite = Sprite::Center;
        Rect source{};
        Rect destination{};
        bool valid = false;
    };

    // `layoutScale` is the authenticated MenuBar logical scale: 1.0 for
    // Standard/Wide, 1.6 for WideFull, 1.28 for WideNote.  It is distinct from
    // the later physical-output viewport scale.
    static Draw Build(const SchoolDaysTopMeterModel::Visual& visual,
                      float layoutScale);
};

} // namespace Kotonoha
