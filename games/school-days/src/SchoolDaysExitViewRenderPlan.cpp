#include <Kotonoha/routing/SchoolDaysExitViewRenderPlan.hpp>

namespace Kotonoha {
namespace {

using Plan = SchoolDaysExitViewRenderPlan;

struct Record { float dx, dy, w, h, sx, sy; };
static constexpr Record kChoices[2] = {
    {230,334,166,39,1,1},
    {404,334,167,39,168,1},
};

Plan::Sprite Hover(const Record& r, int choice, const Plan::Layout& layout) {
    Plan::Sprite sprite;
    sprite.texture = Plan::Texture::Chip;
    sprite.role = Plan::Role::Hover;
    sprite.hasSource = true;
    sprite.source = {r.sx, r.sy, r.w, r.h};
    sprite.destination = {(r.dx - 0.5f) * layout.scale,
                          (r.dy - 0.5f + layout.yOffset) * layout.scale,
                          (r.w + 1.0f) * layout.scale,
                          (r.h + 1.0f) * layout.scale};
    sprite.choice = choice;
    return sprite;
}

} // namespace

SchoolDaysExitViewRenderPlan::Layout SchoolDaysExitViewRenderPlan::LayoutFor(
    SchoolDaysExitViewModel::Variant variant) {
    using Variant = SchoolDaysExitViewModel::Variant;
    switch (variant) {
    case Variant::Standard: return {1.0f, 75.0f, 800, 600};
    case Variant::Wide: return {1.0f, 0.0f, 800, 450};
    case Variant::WideFull: return {1.6f, 0.0f, 1280, 720};
    case Variant::WideNote: return {1.28f, 0.0f, 1024, 576};
    }
    return {1.0f, 75.0f, 800, 600};
}

std::vector<SchoolDaysExitViewRenderPlan::Sprite>
SchoolDaysExitViewRenderPlan::Build(const SchoolDaysExitViewModel& model) {
    std::vector<Sprite> out;
    if (!model.IsOpen()) return out;
    const Layout layout = LayoutFor(model.CurrentVariant());

    Sprite base;
    base.texture = Texture::Base;
    base.role = Role::Base;
    base.destination = {-0.5f * layout.scale,
                        layout.yOffset * layout.scale,
                        800.0f * layout.scale,
                        450.0f * layout.scale};
    out.push_back(base);

    const int choice = model.Selection();
    if (choice >= 0 && choice < 2) out.push_back(Hover(kChoices[choice], choice, layout));
    return out;
}

} // namespace Kotonoha
