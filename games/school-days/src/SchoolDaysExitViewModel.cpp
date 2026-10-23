#include <Kotonoha/routing/SchoolDaysExitViewModel.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>


namespace Kotonoha {
SchoolDaysExitViewModel::Variant SchoolDaysExitViewModel::SelectVariant(
    int effectiveDisplayType, int windowMode, int typeMiniNote) {
    // SysMenuSDHQ.dll:0x1000A5A0.
    if (effectiveDisplayType != 1) return Variant::Standard;
    if (windowMode != 1) return Variant::Wide;
    return typeMiniNote == 0 ? Variant::WideFull : Variant::WideNote;
}

const char* SchoolDaysExitViewModel::CmapRelativePath(Variant variant) {
    switch (variant) {
    case Variant::Standard: return "System/Exit/Popup.cmap";
    case Variant::Wide: return "System/Exit/Popup_Wide.cmap";
    case Variant::WideFull: return "System/Exit/Popup_Wide_Full.cmap";
    case Variant::WideNote: return "System/Exit/Popup_Wide_Note.cmap";
    }
    return "System/Exit/Popup.cmap";
}

const char* SchoolDaysExitViewModel::BaseRelativePath(BaseArtwork artwork) {
    return artwork == BaseArtwork::Exit ? "System/Exit/Popup_Exit.png"
                                        : "System/Exit/Popup_Title.png";
}

const char* SchoolDaysExitViewModel::ChipRelativePath() {
    return "System/Exit/Popup_Chip.png";
}

bool SchoolDaysExitViewModel::Open(const std::string& assetsRoot,
                                   Variant variant, std::string* error) {
    Close();
    std::string physical;
    if (!ResolveSchoolDaysRequiredAssetPath(
            assetsRoot.c_str(), CmapRelativePath(variant), &physical, error,
            "ExitView CMAP")) {
        return false;
    }
    if (!cmap_.LoadFile(physical, error)) return false;

    variant_ = variant;
    selection_ = -1;
    open_ = true;
    return true;
}

void SchoolDaysExitViewModel::Close() {
    cmap_.Clear();
    variant_ = Variant::Standard;
    selection_ = -1;
    open_ = false;
}

int SchoolDaysExitViewModel::HitChoice(int x, int y) const {
    if (!open_) return -1;
    const int region = cmap_.RegionAt(x, y);
    if (region <= 0) return -1;
    const int choice = region - 1;
    return choice >= 0 && choice < 2 ? choice : -1;
}

bool SchoolDaysExitViewModel::SetSelectionFromPointer(int x, int y) {
    const int choice = HitChoice(x, y);
    if (choice == selection_) return false;
    selection_ = choice;
    return true;
}

bool SchoolDaysExitViewModel::Activate(Activation* activation) const {
    if (!open_ || activation == nullptr) return false;
    if (selection_ == 0) {
        activation->outcome = Outcome::ConfirmExit;
        activation->resFlag = 1;
        return true;
    }
    if (selection_ == 1) {
        activation->outcome = Outcome::ReturnToTitle;
        activation->resFlag = 0;
        return true;
    }
    return false;
}

} // namespace Kotonoha
