#include <Kotonoha/routing/SchoolDaysSaveLoadRenderPlan.hpp>

#include <algorithm>

namespace Kotonoha {
namespace {

using Plan = SchoolDaysSaveLoadRenderPlan;

Plan::Rect ScaleSpriteDestination(float x, float y, float w, float h,
                                  const Plan::Layout& layout) {
    return {(x - 0.5f) * layout.scale,
            (y - 0.5f + layout.yOffset) * layout.scale,
            (w + 1.0f) * layout.scale,
            (h + 1.0f) * layout.scale};
}

Plan::Rect ScaleTextDestination(float x, float y, float w, float h,
                                const Plan::Layout& layout) {
    return {x * layout.scale,
            (y + layout.yOffset) * layout.scale,
            w * layout.scale,
            h * layout.scale};
}

Plan::Sprite Chip(Plan::Role role, int index, Plan::Rect source,
                  Plan::Rect destination) {
    Plan::Sprite sprite;
    sprite.texture = Plan::Texture::Chip;
    sprite.role = role;
    sprite.index = index;
    sprite.hasSource = true;
    sprite.source = source;
    sprite.destination = destination;
    return sprite;
}

} // namespace

SchoolDaysSaveLoadRenderPlan::Layout SchoolDaysSaveLoadRenderPlan::LayoutFor(
    SchoolDaysSaveLoadModel::Variant variant) {
    switch (variant) {
    case SchoolDaysSaveLoadModel::Variant::Standard:
        return {1.0f, 75.0f, 800, 600};
    case SchoolDaysSaveLoadModel::Variant::Wide:
        return {1.0f, 0.0f, 800, 450};
    case SchoolDaysSaveLoadModel::Variant::WideFull:
        return {1.6f, 0.0f, 1280, 720};
    case SchoolDaysSaveLoadModel::Variant::WideNote:
        return {1.28f, 0.0f, 1024, 576};
    }
    return {1.0f, 75.0f, 800, 600};
}

int SchoolDaysSaveLoadRenderPlan::GlyphAdvance(uint16_t glyph,
                                               bool useEnglish) {
    if (glyph >= 0x80u) return 45;
    if (!useEnglish) return 24;

    if (glyph == static_cast<uint16_t>('j') ||
        glyph == static_cast<uint16_t>('i') ||
        glyph == static_cast<uint16_t>('l')) {
        return 17;
    }
    if (glyph == static_cast<uint16_t>('m') ||
        glyph == static_cast<uint16_t>('w')) {
        return 32;
    }
    if (glyph >= static_cast<uint16_t>('A') &&
        glyph <= static_cast<uint16_t>('Z')) {
        if (glyph == static_cast<uint16_t>('M') ||
            glyph == static_cast<uint16_t>('W') ||
            glyph == static_cast<uint16_t>('Q')) {
            return 35;
        }
        if (glyph == static_cast<uint16_t>('I')) return 17;
        return 28;
    }
    return 24;
}

std::vector<SchoolDaysSaveLoadRenderPlan::Sprite>
SchoolDaysSaveLoadRenderPlan::Build(const SchoolDaysSaveLoadModel& model) {
    std::vector<Sprite> sprites;
    if (!model.IsOpen()) return sprites;

    const Layout layout = LayoutFor(model.CurrentVariant());
    sprites.reserve(5u);

    Sprite base;
    base.texture = Texture::Base;
    base.role = Role::Base;
    base.destination = {-0.5f * layout.scale,
                        layout.yOffset * layout.scale,
                        800.0f * layout.scale,
                        450.0f * layout.scale};
    sprites.push_back(base);

    // SaveLoad+0x98 pending makes every control invalid in FUN_10014910.  The
    // native state pass therefore emits no row/page/close hover overlay while a
    // deferred save request is pending.
    const bool interactive = !model.SavePending();
    const int selection = model.Selection();
    if (interactive) {
        int row = -1;
        if (selection >= 0 && selection <= 9) row = selection;
        if (selection >= 22 && selection <= 31) row = selection - 22;
        if (row >= 0) {
            sprites.push_back(Chip(
                Role::RowHover, row,
                {1.0f, 122.0f + 32.0f * static_cast<float>(row), 558.0f, 32.0f},
                ScaleSpriteDestination(230.0f,
                                       96.0f + 32.0f * static_cast<float>(row),
                                       558.0f, 32.0f, layout)));
        }
    }

    // The current page is represented by one of two fixed objects: +F8 when
    // not hovered, +FC when its own page tab is the current CMAP region.
    if (interactive) {
        const int page = model.Page();
        const bool hovered = selection == page + 10;
        sprites.push_back(Chip(
            hovered ? Role::CurrentPageHover : Role::CurrentPage, page,
            {391.0f + 40.0f * static_cast<float>(page), hovered ? 96.0f : 70.0f,
             39.0f, 26.0f},
            ScaleSpriteDestination(410.0f + 39.0f * static_cast<float>(page),
                                   60.0f, 39.0f, 26.0f, layout)));
    }

    if (interactive && selection == 20) {
        sprites.push_back(Chip(Role::CloseHover, 20, {391.0f, 1.0f, 98.0f, 42.0f},
                               ScaleSpriteDestination(702.0f, 6.0f, 98.0f, 42.0f,
                                                      layout)));
    }

    // The fixed RouteMap CHIP object is drawn in both SAVE and LOAD.  Only the
    // control21 highlight/action path is LOAD-only; SAVE leaves the decoration
    // inert rather than omitting it.
    sprites.push_back(Chip(Role::RouteMap, 21, {1.0f, 1.0f, 194.0f, 38.0f},
                           ScaleSpriteDestination(10.0f, 54.0f, 194.0f,
                                                  38.0f, layout)));
    return sprites;
}

std::vector<SchoolDaysSaveLoadRenderPlan::MetadataRect>
SchoolDaysSaveLoadRenderPlan::MetadataLayout(
    SchoolDaysSaveLoadModel::Variant variant, bool useEnglish,
    const std::vector<int>* optionalRenderedAdvance) {
    const Layout layout = LayoutFor(variant);
    std::vector<MetadataRect> rows;
    rows.reserve(10u);
    for (int row = 0; row < 10; ++row) {
        const float y = 100.5f + 33.0f * static_cast<float>(row);
        MetadataRect item;
        item.row = row;
        item.primarySource = {0.0f, 2.0f + 48.0f * static_cast<float>(row),
                              548.0f, 48.0f};
        item.secondarySource = {1024.0f,
                                2.0f + 48.0f * static_cast<float>(row),
                                548.0f, 48.0f};
        item.optionalThirdSource = {0.0f,
                                    514.0f + 48.0f * static_cast<float>(row),
                                    986.0f, 48.0f};

        const float primaryX = useEnglish ? 6.0f : 1.0f;
        const float secondaryX = useEnglish ? 277.5f : 262.5f;
        float optionalX = 330.0f;
        if (useEnglish && optionalRenderedAdvance != nullptr &&
            static_cast<size_t>(row) < optionalRenderedAdvance->size()) {
            const float advance = static_cast<float>((*optionalRenderedAdvance)[row]);
            optionalX += std::max(0.0f, 235.5f - advance / 4.0f);
        }
        item.primary = ScaleTextDestination(primaryX, y, 252.0f, 24.0f, layout);
        item.secondary =
            ScaleTextDestination(secondaryX, y, 252.0f, 24.0f, layout);
        item.optionalThird =
            ScaleTextDestination(optionalX, y, 494.0f, 24.0f, layout);
        rows.push_back(item);
    }
    return rows;
}

} // namespace Kotonoha
