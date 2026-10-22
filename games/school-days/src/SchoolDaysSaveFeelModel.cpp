#include <Kotonoha/routing/SchoolDaysSaveFeelModel.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>

#include <algorithm>

namespace Kotonoha {
SchoolDaysSaveFeelModel::Variant SchoolDaysSaveFeelModel::SelectVariant(
    int effectiveDisplayType, int windowMode, int typeMiniNote) {
    // SysMenuSDHQ.dll:0x100102B0, preserving its exact predicate order.
    if (effectiveDisplayType != 1) return Variant::Standard;
    if (windowMode != 1) return Variant::Wide;
    return typeMiniNote == 0 ? Variant::WideFull : Variant::WideNote;
}

const char* SchoolDaysSaveFeelModel::CmapRelativePath(Variant variant) {
    switch (variant) {
    case Variant::Standard:
        return "System/SaveLoad/Pop_Save_Feel.cmap";
    case Variant::Wide:
        return "System/SaveLoad/Pop_Save_Feel_Wide.cmap";
    case Variant::WideFull:
        return "System/SaveLoad/Pop_Save_Feel_Wide_Full.cmap";
    case Variant::WideNote:
        return "System/SaveLoad/Pop_Save_Feel_Wide_Note.cmap";
    }
    return "System/SaveLoad/Pop_Save_Feel.cmap";
}

bool SchoolDaysSaveFeelModel::Open(const std::string& assetsRoot,
                                   Variant variant, std::string* error) {
    Close();
    std::string physical;
    if (!ResolveSchoolDaysRequiredAssetPath(
            assetsRoot.c_str(), CmapRelativePath(variant), &physical, error,
            "SaveFeel CMAP")) {
        return false;
    }
    if (!cmap_.LoadFile(physical, error)) return false;
    variant_ = variant;
    selection_ = 0;
    open_ = true;
    countdownStartMs_ = 0;
    countdownState_ = 0;
    countdownTens_ = 0;
    countdownOnes_ = 0;
    countdownEnabled_ = true;
    countdownCloseRequested_ = false;
    feeling001Raw_ = 0;
    feeling002Raw_ = 0;
    feeling001Visible_ = 0.0f;
    feeling002Visible_ = 0.0f;
    entryStartMs_ = 0;
    entryState_ = 0;
    entryTransitionActive_ = true;
    return true;
}

void SchoolDaysSaveFeelModel::Close() {
    cmap_.Clear();
    variant_ = Variant::Standard;
    selection_ = 0;
    open_ = false;
    countdownStartMs_ = 0;
    countdownState_ = 0;
    countdownTens_ = 0;
    countdownOnes_ = 0;
    countdownEnabled_ = false;
    countdownCloseRequested_ = false;
    feeling001Raw_ = 0;
    feeling002Raw_ = 0;
    feeling001Visible_ = 0.0f;
    feeling002Visible_ = 0.0f;
    entryStartMs_ = 0;
    entryState_ = 0;
    entryTransitionActive_ = false;
}

int SchoolDaysSaveFeelModel::HitChoice(int x, int y) const {
    if (!open_) return -1;
    const int region = cmap_.RegionAt(x, y);
    if (region <= 0) return -1;
    const int choice = region - 1;
    return choice >= 0 && choice < 2 ? choice : -1;
}

bool SchoolDaysSaveFeelModel::SetSelectionFromPointer(int x, int y) {
    const int choice = HitChoice(x, y);
    if (choice == selection_) return false;
    selection_ = choice;
    return true;
}

void SchoolDaysSaveFeelModel::Previous() {
    if (!open_) return;
    selection_ = selection_ == 0 ? 1 : 0;
}

void SchoolDaysSaveFeelModel::Next() {
    if (!open_) return;
    selection_ = selection_ == 1 ? 0 : 1;
}

bool SchoolDaysSaveFeelModel::Activate(Result* result) const {
    if (!Interactive() || result == nullptr) return false;
    if (selection_ == 0) {
        *result = Result::OpenSave;
        return true;
    }
    if (selection_ == 1) {
        *result = Result::Skip;
        return true;
    }
    return false;
}

float SchoolDaysSaveFeelModel::ClampFeelingFraction(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

void SchoolDaysSaveFeelModel::SetFeelings(int64_t feeling001,
                                          int64_t feeling002) {
    feeling001Raw_ = feeling001;
    feeling002Raw_ = feeling002;
    if (!entryTransitionActive_) {
        feeling001Visible_ = ClampFeelingFraction(
            static_cast<float>(feeling001Raw_) / 100.0f);
        feeling002Visible_ = ClampFeelingFraction(
            static_cast<float>(feeling002Raw_) / 100.0f);
    }
}

void SchoolDaysSaveFeelModel::TickEntryTransition(uint32_t nowMs) {
    if (!open_ || !entryTransitionActive_) return;

    if (entryState_ == 0) {
        entryState_ = 1;
        feeling001Visible_ = 0.0f;
        feeling002Visible_ = 0.0f;
        entryStartMs_ = nowMs;
        return;
    }

    if (entryState_ == 1) {
        const uint32_t elapsed = nowMs - entryStartMs_;
        if (elapsed < 1500u) {
            const float elapsedFloat = static_cast<float>(elapsed);
            feeling001Visible_ = ClampFeelingFraction(
                ((elapsedFloat * static_cast<float>(feeling001Raw_)) /
                 1500.0f) /
                100.0f);
            feeling002Visible_ = ClampFeelingFraction(
                ((elapsedFloat * static_cast<float>(feeling002Raw_)) /
                 1500.0f) /
                100.0f);
            return;
        }

        feeling001Visible_ = ClampFeelingFraction(
            static_cast<float>(feeling001Raw_) / 100.0f);
        feeling002Visible_ = ClampFeelingFraction(
            static_cast<float>(feeling002Raw_) / 100.0f);
        entryState_ = 2;
        return;
    }

    if (entryState_ == 2) {
        entryStartMs_ = 0;
        entryState_ = 0;
        entryTransitionActive_ = false;
    }
}

void SchoolDaysSaveFeelModel::Tick(uint32_t nowMs) {
    TickEntryTransition(nowMs);
    TickCountdown(nowMs);
}

void SchoolDaysSaveFeelModel::TickCountdown(uint32_t nowMs) {
    if (!open_ || !countdownEnabled_) return;

    if (countdownState_ == 0) {
        countdownState_ = 1;
        countdownStartMs_ = nowMs;
        return;
    }

    if (countdownState_ == 1) {
        const uint32_t elapsed = nowMs - countdownStartMs_;
        if (elapsed < 30000u) {
            const uint32_t seconds = 30u - elapsed / 1000u;
            countdownTens_ = static_cast<int>(seconds / 10u);
            countdownOnes_ = static_cast<int>(seconds % 10u);
            return;
        }

        // Native state 1 explicitly swaps both digit sprites to their zero
        // atlas entries before advancing to state 2.
        countdownTens_ = 0;
        countdownOnes_ = 0;
        countdownState_ = 2;
        return;
    }

    if (countdownState_ == 2) {
        countdownStartMs_ = 0;
        countdownState_ = 0;
        countdownEnabled_ = false;
        countdownCloseRequested_ = true;
    }
}

bool SchoolDaysSaveFeelModel::CountdownResult(Result* result) const {
    if (!countdownCloseRequested_ || result == nullptr) return false;

    // The timeout path never dispatches host +0x4C, so the handoff result
    // remains the causally inherited zero. EXE 0x00426620 therefore follows
    // the same outer-flow branch as explicit result 0 and does not enter
    // SaveLoad.
    *result = Result::Skip;
    return true;
}

} // namespace Kotonoha
