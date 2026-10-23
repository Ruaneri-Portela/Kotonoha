#include <Kotonoha/routing/SchoolDaysConfigMenuRenderPlan.hpp>

#include <algorithm>
#include <array>

namespace Kotonoha {
namespace {

using Model = SchoolDaysConfigMenuModel;
using Plan = SchoolDaysConfigMenuRenderPlan;

struct Record {
    float dx;
    float dy;
    float w;
    float h;
    float sx;
    float sy;
};

// SysMenuSDHQ.dll .data tables recovered from the authenticated v1.02 image.
static constexpr Record kDef[] = {
    {10,54,194,38,1,1},{204,54,194,38,196,1},{398,54,194,38,391,1},{702,6,98,42,586,1},
    {37,155,164,32,1,44},{210,155,164,32,166,44},{427,155,164,32,331,44},{599,155,165,32,496,44},
    {37,254,164,35,1,77},{210,254,164,35,166,77},{427,254,164,35,331,77},{599,254,165,35,496,77},
    {37,354,164,35,1,113},{210,354,164,35,166,113},
    {10,54,194,38,1,149},{204,54,194,38,196,149},{398,54,194,38,391,149},
    {37,155,164,32,1,188},{210,155,164,32,166,188},{427,155,164,32,331,188},{599,155,165,32,496,188},
    {37,254,164,35,1,221},{210,254,164,35,166,221},{427,254,164,35,331,221},{599,254,165,35,496,221},
    {37,354,164,35,1,257},{210,354,164,35,166,257},
    {10,54,194,38,1,293},{204,54,194,38,196,293},{398,54,194,38,391,293},
    {37,155,164,32,1,332},{210,155,164,32,166,332},{427,155,164,32,331,332},{599,155,165,32,496,332},
    {37,254,164,35,1,365},{210,254,164,35,166,365},{427,254,164,35,331,365},{599,254,165,35,496,365},
    {37,354,164,35,1,401},{210,354,164,35,166,401},
};

static constexpr Record kSound[] = {
    {10,54,194,38,1,1},{204,54,194,38,196,1},{398,54,194,38,391,1},{702,6,98,42,586,1},
    {97,130,78,31,1,44},{697,130,78,31,80,44},{97,205,78,31,159,44},{697,205,78,31,238,44},
    {97,280,78,31,317,44},{697,280,78,31,396,44},{37,374,163,34,1,76},{210,374,163,34,165,76},
    {427,374,163,34,329,76},{599,374,165,34,493,76},
    {175,130,54,31,1,111},{229,130,52,31,55,111},{281,130,52,31,107,111},{333,130,52,31,159,111},
    {385,130,52,31,211,111},{437,130,52,31,263,111},{489,130,52,31,315,111},{541,130,52,31,367,111},
    {593,130,52,31,419,111},{645,130,52,31,471,111},
    {175,205,54,31,1,143},{229,205,52,31,55,143},{281,205,52,31,107,143},{333,205,52,31,159,143},
    {385,205,52,31,211,143},{437,205,52,31,263,143},{489,205,52,31,315,143},{541,205,52,31,367,143},
    {593,205,52,31,419,143},{645,205,52,31,471,143},
    {175,280,54,31,1,175},{229,280,52,31,55,175},{281,280,52,31,107,175},{333,280,52,31,159,175},
    {385,280,52,31,211,175},{437,280,52,31,263,175},{489,280,52,31,315,175},{541,280,52,31,367,175},
    {593,280,52,31,419,175},{645,280,52,31,471,175},
    {10,54,194,38,1,207},{204,54,194,38,196,207},{398,54,194,38,391,207},
    {37,374,163,34,1,246},{210,374,163,34,165,246},{427,374,163,34,329,246},{599,374,165,34,493,246},
    {10,54,194,38,1,281},{204,54,194,38,196,281},{398,54,194,38,391,281},
    {37,374,163,34,1,320},{210,374,163,34,165,320},{427,374,163,34,329,320},{599,374,165,34,493,320},
};

static constexpr Record kSomCon[] = {
    {10,54,194,38,1,1},{204,54,194,38,196,1},{398,54,194,38,391,1},{702,6,98,42,586,1},
    {438,375,163,34,1,44},{610,375,164,34,165,44},
    {11,153,70,28,1,81},{89,153,70,28,72,81},{168,153,70,28,143,81},{246,153,70,28,214,81},
    {324,153,70,28,285,81},{403,153,70,28,356,81},{481,153,70,28,427,81},{559,153,70,28,498,81},
    {638,153,70,28,569,81},{716,153,70,28,640,81},{8,234,166,36,330,44},{181,234,167,36,497,44},
    {10,54,194,38,1,110},{204,54,194,38,196,110},{398,54,194,38,391,110},
    {438,375,163,34,1,149},{610,375,164,34,165,149},
    {11,153,70,28,1,186},{89,153,70,28,72,186},{168,153,70,28,143,186},{246,153,70,28,214,186},
    {324,153,70,28,285,186},{403,153,70,28,356,186},{481,153,70,28,427,186},{559,153,70,28,498,186},
    {638,153,70,28,569,186},{716,153,70,28,640,186},{8,234,166,36,330,149},{181,234,167,36,497,149},
    {10,54,194,38,1,215},{204,54,194,38,196,215},{398,54,194,38,391,215},
    {438,375,163,34,1,254},{610,375,164,34,165,254},{8,234,166,36,330,254},{181,234,167,36,497,254},
};

Plan::Rect Destination(const Record& r, const Plan::Layout& layout,
                       float widthOverride = -1.0f) {
    const float width = widthOverride >= 0.0f ? widthOverride : r.w;
    return {(r.dx - 0.5f) * layout.scale,
            (r.dy - 0.5f + layout.yOffset) * layout.scale,
            (width + 1.0f) * layout.scale,
            (r.h + 1.0f) * layout.scale};
}

Plan::Sprite FromRecord(const Record& r, int recordIndex, int control,
                        Plan::Role role, const Plan::Layout& layout) {
    Plan::Sprite sprite;
    sprite.texture = Plan::Texture::Chip;
    sprite.role = role;
    sprite.hasSource = true;
    sprite.source = {r.sx, r.sy, r.w, r.h};
    sprite.destination = Destination(r, layout);
    sprite.recordIndex = recordIndex;
    sprite.control = control;
    return sprite;
}

bool PairContains(int value, int a, int b) {
    return value == a || value == b;
}

bool CommonControlAvailable(int control, const Plan::VisualState& state) {
    if (control == 2) return state.somConAvailable;
    return control >= 0 && control <= 3;
}

bool ControlAvailable(Model::Page page, int control,
                      const Plan::VisualState& state) {
    if (control >= 0 && control <= 3) return CommonControlAvailable(control, state);
    if (page == Model::Page::Def) return control >= 4 && control <= 13;
    if (page == Model::Page::Sound) return control >= 4 && control <= 43;
    if (page == Model::Page::SomCon) {
        if (control == 4 || control == 5) return true;
        if (control >= 6 && control <= 17) {
            return state.somConSetActive && state.somConInteractionActive;
        }
    }
    return false;
}

void PushDefCurrent(std::vector<Plan::Sprite>& out, const Plan::Layout& layout,
                    int control) {
    if (control >= 4 && control <= 13) {
        const int record = 13 + control;
        out.push_back(FromRecord(kDef[record], record, control,
                                 Plan::Role::CurrentState, layout));
    }
}

bool DefCurrentControl(int control, const Plan::VisualState& state) {
    return control == state.defWideCurrent || control == state.defFullCurrent ||
           control == state.defSkipCurrent || control == state.defSuperSkipCurrent ||
           control == state.defDrawMessageCurrent;
}

bool SoundCurrentHover(int control, const Plan::VisualState& state) {
    // Shipped v1.02 FUN_1000A190 tests control 10 twice. Consequently control
    // 10 always receives current-state hover and control 11 never does.
    if (control == 10) return true;
    if (control == 11) return false;
    if (control == 12 || control == 13) return control == state.soundMuteCurrent;
    return false;
}

float SoundWidth(int baseRecord, int level) {
    // FUN_100070E0: zero is an empty source width; values 1..10 extend the
    // source strip through the end of the selected segment.
    if (level <= 0) return 0.0f;
    const int safeLevel = std::min(level, 10);
    const int selected = baseRecord + safeLevel - 1;
    return kSound[selected].sx + kSound[selected].w - kSound[baseRecord].sx;
}

void PushSoundLevel(std::vector<Plan::Sprite>& out, const Plan::Layout& layout,
                    int bank, int level) {
    // -1 remains a presenter sentinel for callers that have not projected a
    // live value. Native level 0 is a distinct authenticated value (an empty
    // strip), so only negative values suppress the sprite entirely.
    if (level < 0) return;
    static constexpr int kBaseRecords[3] = {14, 24, 34};
    const int recordIndex = kBaseRecords[bank];
    const Record& r = kSound[recordIndex];
    const float width = SoundWidth(recordIndex, level);
    Plan::Sprite sprite;
    sprite.texture = Plan::Texture::Chip;
    sprite.role = Plan::Role::SoundLevel;
    sprite.hasSource = true;
    sprite.source = {r.sx, r.sy, width, r.h};
    sprite.destination = Destination(r, layout, width);
    sprite.recordIndex = recordIndex;
    sprite.soundBank = bank;
    out.push_back(sprite);
}

bool SomConCurrentHoverWithSprite(int control, const Plan::VisualState& state,
                                  int* record) {
    if (record == nullptr) return false;
    if (control == 4 && state.somConSetActive) {
        *record = 38;
        return true;
    }
    if (control == 5 && !state.somConSetActive) {
        *record = 39;
        return true;
    }
    if (control == 16 && state.somConSecondaryCurrent == 16) {
        *record = 40;
        return true;
    }
    if (control == 17 && state.somConSecondaryCurrent == 17) {
        *record = 41;
        return true;
    }
    return false;
}

} // namespace

SchoolDaysConfigMenuRenderPlan::Layout SchoolDaysConfigMenuRenderPlan::LayoutFor(
    SchoolDaysConfigMenuModel::Variant variant) {
    using Variant = SchoolDaysConfigMenuModel::Variant;
    switch (variant) {
    case Variant::Standard: return {1.0f, 75.0f, 800, 600};
    case Variant::Wide: return {1.0f, 0.0f, 800, 450};
    case Variant::WideFull: return {1.6f, 0.0f, 1280, 720};
    case Variant::WideNote: return {1.28f, 0.0f, 1024, 576};
    }
    return {1.0f, 75.0f, 800, 600};
}

std::string SchoolDaysConfigMenuRenderPlan::BaseRelativePath(
    const SchoolDaysConfigMenuModel& model, const VisualState& state) {
    if (model.CurrentPage() == SchoolDaysConfigMenuModel::Page::SomCon &&
        state.somConSetActive) {
        return SchoolDaysConfigMenuModel::SomConSetRelativePath();
    }
    return SchoolDaysConfigMenuModel::BaseRelativePath(model.CurrentPage());
}

std::vector<SchoolDaysConfigMenuRenderPlan::Sprite>
SchoolDaysConfigMenuRenderPlan::Build(const SchoolDaysConfigMenuModel& model,
                                      const VisualState& state) {
    std::vector<Sprite> out;
    if (!model.IsOpen()) return out;

    const Layout layout = LayoutFor(model.CurrentVariant());
    const auto page = model.CurrentPage();

    Sprite base;
    base.texture = (page == Model::Page::SomCon && state.somConSetActive)
                       ? Texture::SomConSetBase
                       : Texture::Base;
    base.role = Role::Base;
    base.destination = {-0.5f * layout.scale,
                        layout.yOffset * layout.scale,
                        800.0f * layout.scale,
                        450.0f * layout.scale};
    out.push_back(base);

    if (page == Model::Page::Def) {
        out.push_back(FromRecord(kDef[14], 14, 0, Role::ActiveTab, layout));
        if (PairContains(state.defWideCurrent, 4, 5))
            PushDefCurrent(out, layout, state.defWideCurrent);
        if (PairContains(state.defFullCurrent, 6, 7))
            PushDefCurrent(out, layout, state.defFullCurrent);
        if (PairContains(state.defSkipCurrent, 8, 9))
            PushDefCurrent(out, layout, state.defSkipCurrent);
        if (PairContains(state.defSuperSkipCurrent, 10, 11))
            PushDefCurrent(out, layout, state.defSuperSkipCurrent);
        if (PairContains(state.defDrawMessageCurrent, 12, 13))
            PushDefCurrent(out, layout, state.defDrawMessageCurrent);

        const int control = model.Selection();
        if (control >= 0 && control <= 13 && ControlAvailable(page, control, state)) {
            if (control == 0) {
                out.push_back(FromRecord(kDef[27], 27, control, Role::Hover, layout));
            }
            else if (DefCurrentControl(control, state)) {
                const int record = 26 + control;
                out.push_back(FromRecord(kDef[record], record, control,
                                         Role::Hover, layout));
            }
            else {
                out.push_back(FromRecord(kDef[control], control, control,
                                         Role::Hover, layout));
            }
        }
        return out;
    }

    if (page == Model::Page::Sound) {
        out.push_back(FromRecord(kSound[45], 45, 1, Role::ActiveTab, layout));
        if (PairContains(state.soundMenVoiceCurrent, 10, 11)) {
            const int r = 37 + state.soundMenVoiceCurrent;
            out.push_back(FromRecord(kSound[r], r, state.soundMenVoiceCurrent,
                                     Role::CurrentState, layout));
        }
        if (PairContains(state.soundMuteCurrent, 12, 13)) {
            const int r = 37 + state.soundMuteCurrent;
            out.push_back(FromRecord(kSound[r], r, state.soundMuteCurrent,
                                     Role::CurrentState, layout));
        }

        const int control = model.Selection();
        if (control >= 0 && control <= 13 && ControlAvailable(page, control, state)) {
            if (control == 1) {
                out.push_back(FromRecord(kSound[52], 52, control, Role::Hover, layout));
            }
            else if (SoundCurrentHover(control, state)) {
                const int r = 44 + control;
                out.push_back(FromRecord(kSound[r], r, control, Role::Hover, layout));
            }
            else {
                out.push_back(FromRecord(kSound[control], control, control,
                                         Role::Hover, layout));
            }
        }

        for (int bank = 0; bank < 3; ++bank) {
            PushSoundLevel(out, layout, bank, state.soundLevels[bank]);
        }
        return out;
    }

    // SomCon. Device meanings remain opaque; only authenticated visual flags
    // and indices are projected.
    if (state.somConAvailable) {
        out.push_back(FromRecord(kSomCon[20], 20, 2, Role::ActiveTab, layout));
    }
    const int primaryRecord = state.somConSetActive ? 21 : 22;
    out.push_back(FromRecord(kSomCon[primaryRecord], primaryRecord,
                             state.somConSetActive ? 4 : 5,
                             Role::CurrentState, layout));

    if (state.somConSetActive && state.somConInteractionActive) {
        if (PairContains(state.somConSecondaryCurrent, 16, 17)) {
            const int secondaryRecord = state.somConSecondaryCurrent == 16 ? 33 : 34;
            out.push_back(FromRecord(kSomCon[secondaryRecord], secondaryRecord,
                                     state.somConSecondaryCurrent,
                                     Role::CurrentState, layout));
        }
        if (state.somConDeviceIndex >= 0 && state.somConDeviceIndex <= 9) {
            const int deviceRecord = 23 + state.somConDeviceIndex;
            out.push_back(FromRecord(kSomCon[deviceRecord], deviceRecord,
                                     6 + state.somConDeviceIndex,
                                     Role::CurrentState, layout));
        }
    }

    const int control = model.Selection();
    if (control >= 0 && control <= 17 && ControlAvailable(page, control, state)) {
        if (control == 2) {
            out.push_back(FromRecord(kSomCon[37], 37, control, Role::Hover, layout));
        }
        else {
            int currentRecord = -1;
            if (SomConCurrentHoverWithSprite(control, state, &currentRecord)) {
                out.push_back(FromRecord(kSomCon[currentRecord], currentRecord,
                                         control, Role::Hover, layout));
            }
            else if (state.somConInteractionActive &&
                     state.somConDeviceIndex >= 0 &&
                     state.somConDeviceIndex <= 9 &&
                     control == 6 + state.somConDeviceIndex) {
                // FUN_1000A250 reports the current device as handled without
                // drawing an additional third-band sprite. The persistent marker
                // above remains visible and first-band hover is suppressed.
            }
            else {
                out.push_back(FromRecord(kSomCon[control], control, control,
                                         Role::Hover, layout));
            }
        }
    }
    return out;
}

} // namespace Kotonoha
