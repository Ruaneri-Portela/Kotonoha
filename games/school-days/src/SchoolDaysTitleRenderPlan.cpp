#include <Kotonoha/routing/SchoolDaysTitleRenderPlan.hpp>

namespace Kotonoha {
namespace {

using Model = SchoolDaysTitleModel;
using Plan = SchoolDaysTitleRenderPlan;
struct Record { float dx, dy, w, h, sx, sy; };

Record Highlight(Model::Family family, int item) {
    static constexpr Record title[5] = {
        {112,353,116,38,1,1}, {230,353,116,38,118,1},
        {348,353,115,38,235,1}, {465,353,116,38,351,1},
        {582,353,116,38,468,1},
    };
    static constexpr Record clear[5] = {
        {112,405,116,38,1,1}, {230,405,116,38,118,1},
        {348,405,115,38,235,1}, {465,405,116,38,351,1},
        {582,405,116,38,468,1},
    };
    static constexpr Record ac[6] = {
        {112,303,116,38,1,1}, {230,303,116,38,118,1},
        {348,303,115,38,235,1}, {465,303,116,38,1,40},
        {582,303,116,38,118,40}, {247,344,307,106,1,79},
    };
    if (family == Model::Family::TitleClear) return clear[item];
    if (family == Model::Family::TitleAc) return ac[item];
    return title[item];
}

Record DisabledReplay(Model::Family family) {
    if (family == Model::Family::TitleClear) return {348,405,115,38,235,40};
    if (family == Model::Family::TitleAc) return {348,303,115,38,235,40};
    return {348,353,115,38,235,40};
}

Plan::Sprite FromRecord(const Record& r, const Plan::Layout& l) {
    Plan::Sprite s;
    s.texture = Plan::Texture::Chip;
    s.hasSource = true;
    s.source = {r.sx, r.sy, r.w, r.h};
    s.destination = {r.dx * l.scale, (r.dy + l.yOffset) * l.scale,
                     r.w * l.scale, r.h * l.scale};
    return s;
}

} // namespace

SchoolDaysTitleRenderPlan::Layout SchoolDaysTitleRenderPlan::LayoutFor(
    SchoolDaysTitleModel::Variant variant) {
    using Variant = SchoolDaysTitleModel::Variant;
    switch (variant) {
    case Variant::Standard: return {1.0f, 75.0f, 800, 600};
    case Variant::Wide: return {1.0f, 0.0f, 800, 450};
    case Variant::WideFull: return {1.6f, 0.0f, 1280, 720};
    case Variant::WideNote: return {1.28f, 0.0f, 1024, 576};
    }
    return {};
}

std::vector<SchoolDaysTitleRenderPlan::Sprite>
SchoolDaysTitleRenderPlan::Build(const SchoolDaysTitleModel& model) {
    std::vector<Sprite> out;
    if (!model.IsOpen()) return out;
    const auto layout = LayoutFor(model.CurrentVariant());

    Sprite overlay;
    overlay.texture = Texture::Overlay;
    overlay.destination = {0.0f, layout.yOffset * layout.scale,
                           800.0f * layout.scale, 450.0f * layout.scale};
    out.push_back(overlay);

    const int selected = model.Selection();
    if (selected >= 0 && model.SelectionAvailable()) {
        out.push_back(FromRecord(Highlight(model.CurrentFamily(), selected), layout));
    }
    if (!SchoolDaysTitleModel::ItemAvailable(2, model.CurrentHostState())) {
        out.push_back(FromRecord(DisabledReplay(model.CurrentFamily()), layout));
    }
    return out;
}

} // namespace Kotonoha
