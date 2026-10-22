#include <Kotonoha/routing/SchoolDaysSaveFeelRenderPlan.hpp>

#include <algorithm>

namespace Kotonoha {
namespace {

using Plan = SchoolDaysSaveFeelRenderPlan;

Plan::Rect ScaleSpriteDestination(float x, float y, float w, float h,
                                  const Plan::Layout& layout) {
    // Common SysMenu sprite placement used by SaveFeel: descriptor coordinates
    // are expanded by one texel and shifted by half a pixel before the global
    // display scale/y-offset are applied.
    return {(x - 0.5f) * layout.scale,
            (y - 0.5f + layout.yOffset) * layout.scale,
            (w + 1.0f) * layout.scale,
            (h + 1.0f) * layout.scale};
}

Plan::Sprite Chip(Plan::Role role, Plan::Rect source, Plan::Rect destination) {
    Plan::Sprite sprite;
    sprite.texture = Plan::Texture::Chip;
    sprite.role = role;
    sprite.hasSource = true;
    sprite.source = source;
    sprite.destination = destination;
    return sprite;
}

} // namespace

SchoolDaysSaveFeelRenderPlan::Layout SchoolDaysSaveFeelRenderPlan::LayoutFor(
    SchoolDaysSaveFeelModel::Variant variant) {
    switch (variant) {
    case SchoolDaysSaveFeelModel::Variant::Standard:
        return {1.0f, 75.0f, 800, 600};
    case SchoolDaysSaveFeelModel::Variant::Wide:
        return {1.0f, 0.0f, 800, 450};
    case SchoolDaysSaveFeelModel::Variant::WideFull:
        return {1.6f, 0.0f, 1280, 720};
    case SchoolDaysSaveFeelModel::Variant::WideNote:
        return {1.28f, 0.0f, 1024, 576};
    }
    return {1.0f, 75.0f, 800, 600};
}

std::vector<SchoolDaysSaveFeelRenderPlan::Sprite>
SchoolDaysSaveFeelRenderPlan::Build(const SchoolDaysSaveFeelModel& model) {
    std::vector<Sprite> sprites;
    if (!model.IsOpen()) return sprites;

    const Layout layout = LayoutFor(model.CurrentVariant());
    sprites.reserve(6u);

    // The normal SIM/NAO buttons and prompt frame are baked into this image.
    Sprite base;
    base.texture = Texture::Base;
    base.role = Role::Base;
    base.destination = {-0.5f * layout.scale,
                        layout.yOffset * layout.scale,
                        800.0f * layout.scale,
                        450.0f * layout.scale};
    sprites.push_back(base);

    if (model.Interactive()) {
        if (model.Selection() == 0) {
            sprites.push_back(Chip(
                Role::SelectedChoice, {1.0f, 1.0f, 164.0f, 35.0f},
                ScaleSpriteDestination(232.0f, 274.0f, 164.0f, 35.0f,
                                       layout)));
        }
        else if (model.Selection() == 1) {
            sprites.push_back(Chip(
                Role::SelectedChoice, {166.0f, 1.0f, 163.0f, 35.0f},
                ScaleSpriteDestination(405.0f, 274.0f, 163.0f, 35.0f,
                                       layout)));
        }
    }

    // The native renderer keeps the complete source strip and changes only the
    // destination width.  This intentionally compresses the strip horizontally
    // rather than clipping its source rectangle.
    const float visible002 = std::clamp(model.Feeling002Visible(), 0.0f, 1.0f);
    const float visible001 = std::clamp(model.Feeling001Visible(), 0.0f, 1.0f);
    sprites.push_back(Chip(
        Role::Feeling002Bar, {1.0f, 37.0f, 604.0f, 18.0f},
        {(150.0f - 0.5f) * layout.scale,
         (371.0f - 0.5f + layout.yOffset) * layout.scale,
         605.0f * layout.scale * visible002, 19.0f * layout.scale}));
    sprites.push_back(Chip(
        Role::Feeling001Bar, {1.0f, 56.0f, 604.0f, 17.0f},
        {(150.0f - 0.5f) * layout.scale,
         (398.0f - 0.5f + layout.yOffset) * layout.scale,
         605.0f * layout.scale * visible001, 18.0f * layout.scale}));

    const int tens = std::clamp(model.CountdownTens(), 0, 9);
    const int ones = std::clamp(model.CountdownOnes(), 0, 9);
    sprites.push_back(Chip(
        Role::CountdownTens,
        {1.0f + 60.0f * static_cast<float>(tens), 141.0f, 57.0f, 63.0f},
        ScaleSpriteDestination(348.0f, 151.0f, 57.0f, 63.0f, layout)));
    sprites.push_back(Chip(
        Role::CountdownOnes,
        {1.0f + 60.0f * static_cast<float>(ones), 74.0f, 59.0f, 66.0f},
        ScaleSpriteDestination(394.0f, 150.0f, 59.0f, 66.0f, layout)));

    return sprites;
}

} // namespace Kotonoha
