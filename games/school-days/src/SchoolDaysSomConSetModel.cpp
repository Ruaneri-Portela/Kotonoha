#include <Kotonoha/routing/SchoolDaysSomConSetModel.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>

namespace Kotonoha {

std::string SchoolDaysSomConSetModel::CmapRelativePath(Variant variant) {
    switch (variant) {
    case Variant::Standard: return "System/Option/Pop_Som.cmap";
    case Variant::Wide: return "System/Option/Pop_Som_Wide.cmap";
    case Variant::WideFull: return "System/Option/Pop_Som_Wide_Full.cmap";
    case Variant::WideNote: return "System/Option/Pop_Som_Wide_Note.cmap";
    }
    return "System/Option/Pop_Som.cmap";
}

bool SchoolDaysSomConSetModel::Open(const std::string& assetsRoot,
                                    Variant variant, std::string* error) {
    Close();
    std::string physical;
    if (!ResolveSchoolDaysRequiredAssetPath(
            assetsRoot.c_str(), CmapRelativePath(variant), &physical, error,
            "SomconSet CMAP")) {
        return false;
    }
    if (!cmap_.LoadFile(physical, error)) return false;
    variant_ = variant;
    selection_ = -1;
    open_ = true;
    return true;
}

void SchoolDaysSomConSetModel::Close() {
    cmap_.Clear();
    variant_ = Variant::Standard;
    selection_ = -1;
    open_ = false;
}

int SchoolDaysSomConSetModel::HitControl(int x, int y) const {
    if (!open_) return -1;
    const int region = cmap_.RegionAt(x, y);
    const int control = region > 0 ? region - 1 : -1;
    return control == 0 ? 0 : -1;
}

bool SchoolDaysSomConSetModel::SetSelectionFromPointer(int x, int y) {
    const int control = HitControl(x, y);
    if (control == selection_) return false;
    selection_ = control;
    return true;
}

bool SchoolDaysSomConSetModel::Activate(int* hostTransition) const {
    if (!open_ || selection_ != 0 || hostTransition == nullptr) return false;
    *hostTransition = kReturnToConfigHostTransition;
    return true;
}

} // namespace Kotonoha
