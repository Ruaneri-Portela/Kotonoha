#include <Kotonoha/routing/SchoolDaysTitleModel.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>


namespace Kotonoha {
namespace {

std::string VariantSuffix(SchoolDaysTitleModel::Variant variant) {
    using Variant = SchoolDaysTitleModel::Variant;
    switch (variant) {
    case Variant::Standard: return {};
    case Variant::Wide: return "_Wide";
    case Variant::WideFull: return "_Wide_Full";
    case Variant::WideNote: return "_Wide_Note";
    }
    return {};
}


} // namespace

SchoolDaysTitleModel::Family SchoolDaysTitleModel::SelectFamily(
    const HostState& state) {
    if (state.ac) return Family::TitleAc;
    if (state.alternate) return Family::Title;
    if (state.endClearVisual) return Family::TitleClear;
    return Family::Title;
}

bool SchoolDaysTitleModel::ItemAvailable(int item, const HostState& state) {
    if (item == 0 || item == 1 || item == 3 || item == 4) return true;
    if (item == 2) return !state.alternate && state.endClearReplay;
    if (item == 5) return state.ac;
    return false;
}

const char* SchoolDaysTitleModel::ResourceStem(Family family) {
    switch (family) {
    case Family::Title: return "System/Title/Title";
    case Family::TitleClear: return "System/Title/Title_Clear";
    case Family::TitleAc: return "System/Title/Title_AC";
    }
    return "System/Title/Title";
}

const char* SchoolDaysTitleModel::ChipStem(Family family) {
    switch (family) {
    case Family::Title: return "System/Title/Title_Chip";
    case Family::TitleClear: return "System/Title/Title_Clear_Chip";
    case Family::TitleAc: return "System/Title/Title_AC_Chip";
    }
    return "System/Title/Title_Chip";
}

std::string SchoolDaysTitleModel::CmapRelativePath(Family family,
                                                   Variant variant) {
    std::string path = ResourceStem(family);
    path += VariantSuffix(variant);
    path += ".cmap";
    return path;
}

bool SchoolDaysTitleModel::Open(const std::string& assetsRoot, Variant variant,
                                const HostState& state, std::string* error) {
    Close();
    family_ = SelectFamily(state);
    variant_ = variant;
    host_ = state;

    std::string cmapPath;
    if (!ResolveSchoolDaysRequiredAssetPath(
            assetsRoot.c_str(), CmapRelativePath(family_, variant_),
            &cmapPath, error, "Title CMAP")) {
        Close();
        return false;
    }
    if (!cmap_.LoadFile(cmapPath, error)) {
        Close();
        return false;
    }
    selection_ = -1;
    open_ = true;
    return true;
}

void SchoolDaysTitleModel::Close() {
    cmap_.Clear();
    host_ = {};
    family_ = Family::Title;
    variant_ = Variant::Standard;
    selection_ = -1;
    open_ = false;
}

int SchoolDaysTitleModel::HitItem(int x, int y) const {
    if (!open_) return -1;
    const int region = cmap_.Get(x, y);
    if (region <= 0) return -1;
    const int item = region - 1;
    return item >= 0 && item <= 5 ? item : -1;
}

bool SchoolDaysTitleModel::SetSelectionFromPointer(int x, int y) {
    if (!open_) return false;
    selection_ = HitItem(x, y);
    return true;
}

bool SchoolDaysTitleModel::ActivateSelection(Activation* out) const {
    if (!open_ || out == nullptr || !SelectionAvailable()) return false;

    Activation activation;
    switch (selection_) {
    case 0:
        activation.result = Result::Start;
        activation.startSelector = 0;
        break;
    case 1:
        activation.result = Result::Load;
        break;
    case 2:
        activation.result = Result::Replay;
        break;
    case 3:
        activation.result = Result::Config;
        break;
    case 4:
        activation.result = Result::Exit;
        break;
    case 5:
        activation.result = Result::Start;
        activation.startSelector = 1;
        break;
    default:
        return false;
    }
    *out = activation;
    return true;
}

} // namespace Kotonoha
