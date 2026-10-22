#include <Kotonoha/routing/SchoolDaysMenuBarPresenter.hpp>

#include <algorithm>
#include <array>

namespace Kotonoha {
namespace {

struct RetailChipRecord {
    float x, y, w, h, sx, sy;
};

// SysMenuSDHQ.dll v1.02 table at 0x1004CE20, records 0..24.
constexpr std::array<RetailChipRecord, 25> kRetailControls{{
    {6, 3, 106, 19, 1, 67},
    {8, 26, 61, 27, 1, 1},
    {80, 26, 41, 27, 63, 1},
    {124, 26, 41, 27, 105, 1},
    {176, 26, 41, 27, 147, 1},
    {229, 26, 41, 27, 189, 1},
    {273, 26, 41, 27, 231, 1},
    {316, 26, 41, 27, 273, 1},
    {360, 26, 41, 27, 314, 1},
    {404, 26, 41, 27, 356, 1},
    {475, 26, 61, 27, 1, 29},
    {539, 26, 61, 27, 63, 29},
    {603, 26, 61, 27, 125, 29},
    {667, 26, 61, 27, 187, 29},
    {731, 26, 61, 27, 249, 29},
    {676, 5, 12, 17, 267, 379},
    {688, 5, 12, 17, 280, 379},
    {700, 5, 12, 17, 293, 379},
    {712, 5, 12, 17, 306, 379},
    {724, 5, 12, 17, 319, 379},
    {736, 5, 12, 17, 332, 379},
    {748, 5, 12, 17, 345, 379},
    {760, 5, 12, 17, 358, 379},
    {772, 5, 12, 17, 371, 379},
    {784, 5, 12, 17, 384, 379},
}};

// Additional fixed records from the same authenticated table. These are
// visual state records rather than CMAP hit regions.
constexpr std::array<RetailChipRecord, 48> kRetailFixedRecords{{
    // records 25..72
    {6,3,106,19,108,67}, {8,26,61,27,311,29},
    {188,9,485,9,1,369}, {118,9,485,9,1,57},
    {6,3,106,19,215,67}, {6,3,106,19,322,67},
    {6,3,106,19,429,67}, {6,3,106,19,1,87},
    {6,3,106,19,108,87}, {6,3,106,19,215,87},
    {6,3,106,19,322,87}, {6,3,106,19,429,87},
    {6,3,106,19,1,107}, {6,3,106,19,108,107},
    {6,3,106,19,215,107}, {6,3,106,19,322,107},
    {6,3,106,19,429,107}, {6,3,106,19,1,183},
    {6,3,106,19,108,67}, {8,26,61,27,311,155},
    {8,26,61,27,1,127}, {176,26,41,27,63,127},
    {229,26,216,27,105,127}, {475,26,61,27,1,155},
    {539,26,61,27,63,155}, {603,26,61,27,125,155},
    {667,26,61,27,187,155}, {731,26,61,27,249,155},
    {0,55,277,20,260,290}, {0,55,250,20,1,203},
    {78,55,281,20,252,203}, {120,54,283,21,1,224},
    {173,54,152,21,285,224}, {226,54,139,21,1,246},
    {474,54,139,21,141,246}, {536,54,192,21,281,246},
    {494,54,282,21,1,268}, {662,54,129,21,284,268},
    {630,54,165,21,414,268}, {537,56,252,19,14,379},
    {176,26,41,27,1,290}, {229,26,216,27,43,290},
    {116,3,538,22,1,346}, {697,80,97,19,414,21},
    {674,4,124,19,322,127}, {674,4,124,19,414,1},
    {760,5,12,17,1,379}, {1,9,798,9,1,399},
}};

// The scaled CMAPs are authored raster masks, so their integer hit bounds are
// not equivalent to multiplying the Standard bounds by 1.6/1.28. These are
// the exact shipped v1.02 regions from MENUBAR_WIDE_FULL/NOTE.CMAP.
constexpr std::array<SDL_FRect, 25> kWideFullControlRects{{
    {10,5,169,30}, {12,41,98,44}, {127,41,67,44}, {199,41,65,44},
    {281,41,66,44}, {366,41,66,44}, {436,41,66,44}, {505,41,66,44},
    {576,41,65,44}, {646,41,66,44}, {760,41,97,44}, {862,41,98,44},
    {965,41,97,44}, {1067,41,98,44}, {1170,41,97,44}, {1081,7,20,28},
    {1101,7,19,28}, {1120,7,19,28}, {1139,7,20,28}, {1159,7,18,28},
    {1177,7,19,28}, {1196,7,20,28}, {1216,7,19,28}, {1235,7,19,28},
    {1254,7,20,28},
}};

constexpr std::array<SDL_FRect, 25> kWideNoteControlRects{{
    {8,4,135,24}, {10,33,78,35}, {102,33,53,35}, {159,33,52,35},
    {225,33,53,35}, {293,33,53,35}, {349,33,53,35}, {404,33,53,35},
    {461,33,52,35}, {517,33,53,35}, {608,33,78,35}, {690,33,78,35},
    {772,33,78,35}, {854,33,78,35}, {936,33,78,35}, {865,6,16,22},
    {881,6,15,22}, {896,6,15,22}, {911,6,16,22}, {927,6,15,22},
    {942,6,15,22}, {957,6,16,22}, {973,6,15,22}, {988,6,15,22},
    {1003,6,16,22},
}};

} // namespace

SchoolDaysMenuBarPresenter::Layout
SchoolDaysMenuBarPresenter::LayoutForVariant(Variant variant) {
    switch (variant) {
    case Variant::Standard:
        return {800, 600, 1.0f, 800.0f, 75.0f};
    case Variant::Wide:
        return {800, 450, 1.0f, 800.0f, 75.0f};
    case Variant::WideFull:
        return {1280, 720, 1.6f, 1280.0f, 120.0f};
    case Variant::WideNote:
        return {1024, 576, 1.28f, 1024.0f, 96.0f};
    }
    return {};
}

SchoolDaysMenuBarPresenter::Viewport
SchoolDaysMenuBarPresenter::FitViewport(int outputWidth, int outputHeight,
                                       const Layout& layout) {
    Viewport out;
    if (outputWidth <= 0 || outputHeight <= 0 || layout.canvasWidth <= 0 ||
        layout.canvasHeight <= 0) {
        return out;
    }
    const float sx = static_cast<float>(outputWidth) /
                     static_cast<float>(layout.canvasWidth);
    const float sy = static_cast<float>(outputHeight) /
                     static_cast<float>(layout.canvasHeight);
    out.scale = std::min(sx, sy);
    out.x = (static_cast<float>(outputWidth) -
             static_cast<float>(layout.canvasWidth) * out.scale) * 0.5f;
    out.y = (static_cast<float>(outputHeight) -
             static_cast<float>(layout.canvasHeight) * out.scale) * 0.5f;
    return out;
}

SDL_FRect SchoolDaysMenuBarPresenter::BackLogControlRect(Variant variant) {
    return ControlRect(12, variant);
}

SDL_FRect SchoolDaysMenuBarPresenter::ControlRect(int control, Variant variant) {
    if (control < 0 || control >= static_cast<int>(kRetailControls.size())) {
        return {};
    }
    const size_t index = static_cast<size_t>(control);
    if (variant == Variant::WideFull) return kWideFullControlRects[index];
    if (variant == Variant::WideNote) return kWideNoteControlRects[index];
    const auto& record = kRetailControls[index];
    return {record.x, record.y, record.w, record.h};
}

SchoolDaysMenuBarPresenter::ChipRecord
SchoolDaysMenuBarPresenter::ControlChip(int control, Variant variant) {
    ChipRecord out;
    if (control < 0 || control >= static_cast<int>(kRetailControls.size())) {
        return out;
    }
    const auto& record = kRetailControls[static_cast<size_t>(control)];
    const float scale = LayoutForVariant(variant).layoutScale;
    out.destination = {record.x * scale, record.y * scale,
                       record.w * scale, record.h * scale};
    out.source = {record.sx, record.sy, record.w, record.h};
    return out;
}

SchoolDaysMenuBarPresenter::ChipRecord
SchoolDaysMenuBarPresenter::NormalSystemControlChip(int control,
                                                    Variant variant) {
    ChipRecord out;
    if (control < 10 || control > 14) return out;

    // SysMenuSDHQ.dll table records 48..52 are the persistent normal-state
    // sprites for the five system controls. Their destinations match records
    // 10..14; only the atlas source row differs (normal white at y=155 versus
    // orange hover at y=29).
    const int index = control - 10;
    const float scale = LayoutForVariant(variant).layoutScale;
    out.destination = {static_cast<float>(475 + index * 64) * scale,
                       26.0f * scale, 61.0f * scale, 27.0f * scale};
    out.source = {static_cast<float>(1 + index * 62), 155.0f, 61.0f, 27.0f};
    return out;
}

SchoolDaysMenuBarPresenter::ChipRecord
SchoolDaysMenuBarPresenter::FixedChipRecord(int record, Variant variant) {
    ChipRecord out;
    if (record < 25 || record > 72) return out;
    const auto& native = kRetailFixedRecords[static_cast<size_t>(record - 25)];
    const float scale = LayoutForVariant(variant).layoutScale;
    out.destination = {native.x * scale, native.y * scale,
                       native.w * scale, native.h * scale};
    out.source = {native.sx, native.sy, native.w, native.h};
    return out;
}

uint8_t SchoolDaysMenuBarPresenter::FadeAlpha(uint8_t startAlpha,
                                              bool targetVisible,
                                              uint32_t elapsedMs) {
    if (targetVisible) {
        constexpr uint32_t kFadeInMs = 300u;
        if (elapsedMs >= kFadeInMs) return 255;
        const unsigned start = startAlpha;
        return static_cast<uint8_t>(
            start + ((255u - start) * elapsedMs) / kFadeInMs);
    }
    constexpr uint32_t kFadeOutMs = 1000u;
    if (elapsedMs >= kFadeOutMs) return 0;
    return static_cast<uint8_t>(
        (static_cast<unsigned>(startAlpha) * (kFadeOutMs - elapsedMs)) /
        kFadeOutMs);
}

int SchoolDaysMenuBarPresenter::HitTest(float logicalX, float logicalY,
                                        Variant variant) {
    for (int control = 0; control < static_cast<int>(kRetailControls.size());
         ++control) {
        const SDL_FRect rect = ControlRect(control, variant);
        if (logicalX >= rect.x && logicalX < rect.x + rect.w &&
            logicalY >= rect.y && logicalY < rect.y + rect.h) {
            return control;
        }
    }
    return -1;
}

bool SchoolDaysMenuBarPresenter::ControlAvailable(
    int control, bool transportLocked, bool actionInhibit,
    bool speedControlsEnabled, bool superSkipEnabled) {
    // SysMenuSDHQ.dll FUN_10023FB0:
    //   0..3  class0: unconditional
    //   4     class1: host+104==0 && host+110==0 && SuperSkip!=0
    //   5..9  class2: host+110==0 && host+88!=0
    //   10..12 class3: host+104==0
    //   13..14 class0: unconditional
    // Operational names above map to actionInhibit, transportLocked and
    // speedControlsEnabled respectively; no guessed legacy class names are used.
    if (control >= 0 && control <= 3) return true;
    if (control == 4)
        return !actionInhibit && !transportLocked && superSkipEnabled;
    if (control >= 5 && control <= 9)
        return !transportLocked && speedControlsEnabled;
    if (control >= 10 && control <= 12) return !actionInhibit;
    if (control >= 13 && control <= 14) return true;
    return false;
}

bool SchoolDaysMenuBarPresenter::ControlAvailableInNormalGameplay(int control) {
    // Authenticated ordinary unlocked Gameplay: +104=0, +110=0, +88!=0.
    // Retail SuperSkip defaults off, so control4 is visibly present but inert;
    // controls5..9 remain available independently of Skip/SuperSkip.
    return ControlAvailable(control, false, false, true, false);
}

SchoolDaysMenuBarPresenter::Action
SchoolDaysMenuBarPresenter::ActionForControl(int control) {
    Action out;
    out.control = control;
    switch (control) {
    case 0:
        out.kind = ActionKind::ToggleAutoMode;
        break;
    case 1:
        out.kind = ActionKind::TogglePause;
        break;
    case 2:
        out.kind = ActionKind::RestartScene;
        break;
    case 3:
        out.kind = ActionKind::SkipToEnd;
        break;
    case 4:
        out.kind = ActionKind::SkipToNextChoice;
        break;
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
        out.kind = ActionKind::SetPlaybackSpeed;
        out.value = control - 5;
        break;
    case 10:
        out.kind = ActionKind::OpenSave;
        break;
    case 11:
        out.kind = ActionKind::OpenLoad;
        break;
    case 12:
        out.kind = ActionKind::OpenBackLog;
        break;
    case 13:
        out.kind = ActionKind::OpenConfig;
        break;
    case 14:
        out.kind = ActionKind::OpenMainMenuConfirm;
        break;
    default:
        out.kind = ActionKind::None;
        break;
    }
    return out;
}

} // namespace Kotonoha
