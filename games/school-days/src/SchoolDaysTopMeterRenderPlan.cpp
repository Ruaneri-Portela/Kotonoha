#include <Kotonoha/routing/SchoolDaysTopMeterRenderPlan.hpp>

#include <algorithm>

namespace Kotonoha {
namespace {

using Plan = SchoolDaysTopMeterRenderPlan;
using Meter = SchoolDaysTopMeterModel;

constexpr float kNegativeDestX = 188.0f;
constexpr float kPositiveDestX = 118.0f;
constexpr float kDestY = 9.0f;
constexpr float kSideWidth = 485.0f;
constexpr float kBarHeight = 9.0f;
constexpr float kNegativeSourceX = 1.0f;
constexpr float kNegativeSourceY = 369.0f;
constexpr float kPositiveSourceX = 1.0f;
constexpr float kPositiveSourceY = 57.0f;
constexpr float kCenterSourceBaseX = 188.0f;
constexpr float kCenterSourceY = 399.0f;
constexpr float kCenterSourceWidth = 417.0f;
constexpr float kCenterDestWidth = 418.0f;

Plan::Rect Destination(float x, float width, float scale) {
    return {(x - 0.5f) * scale,
            (kDestY - 0.5f) * scale,
            (width + 1.0f) * scale,
            (kBarHeight + 1.0f) * scale};
}

} // namespace

SchoolDaysTopMeterRenderPlan::Draw SchoolDaysTopMeterRenderPlan::Build(
    const SchoolDaysTopMeterModel::Visual& visual, float layoutScale) {
    Draw out;
    if (!(layoutScale > 0.0f)) return out;

    const auto geometry = Meter::Classify(visual);
    switch (geometry.geometry) {
    case Meter::Geometry::NegativeSide: {
        const float extent =
            std::clamp(visual.opposite + Meter::kCenterHalfSpan,
                       0.0f, Meter::kSideExtentMax);
        const float remaining = kSideWidth - extent;
        out.sprite = Sprite::NegativeSide;
        out.source = {remaining + kNegativeSourceX, kNegativeSourceY,
                      extent, kBarHeight};
        out.destination = Destination(kNegativeDestX, extent, layoutScale);
        break;
    }

    case Meter::Geometry::PositiveSide: {
        // Native 0x10026540 computes this offset before clamping the extent.
        // Preserve that order: very large feeling values move the destination
        // by the raw excess even though the visible width itself caps at 485.
        const float rawExtent = visual.primary + Meter::kCenterHalfSpan;
        const float preClampOffset = kSideWidth - rawExtent;
        const float extent =
            std::clamp(rawExtent, 0.0f, Meter::kSideExtentMax);
        out.sprite = Sprite::PositiveSide;
        out.source = {kPositiveSourceX, kPositiveSourceY,
                      extent, kBarHeight};
        out.destination =
            Destination(preClampOffset + kPositiveDestX, extent, layoutScale);
        break;
    }

    case Meter::Geometry::Center:
        out.sprite = Sprite::Center;
        // The original deliberately allows this source X to cross the texture
        // edge close to the +/-208.5 boundary.  Keep the raw coordinate in the
        // plan; clipping/sampler behavior belongs to the renderer, not here.
        out.source = {kCenterSourceBaseX - visual.opposite, kCenterSourceY,
                      kCenterSourceWidth, kBarHeight};
        out.destination = Destination(kNegativeDestX, kCenterDestWidth - 1.0f,
                                      layoutScale);
        break;
    }

    out.valid = true;
    return out;
}

} // namespace Kotonoha
