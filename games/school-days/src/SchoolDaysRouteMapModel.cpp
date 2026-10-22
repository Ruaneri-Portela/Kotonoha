#include <Kotonoha/routing/SchoolDaysRouteMapModel.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>

#include <algorithm>
#include <cstdio>

namespace Kotonoha {
namespace {

constexpr SchoolDaysRouteMapModel::PageInfo kPages[6][4] = {
    {{0,4},{0,0},{0,0},{0,0}},
    {{0,9},{0,0},{0,0},{0,0}},
    {{0,7},{7,4},{0,0},{0,0}},
    {{0,8},{8,8},{16,15},{31,4}},
    {{0,11},{11,18},{29,19},{48,9}},
    {{0,11},{11,9},{20,13},{33,12}},
};

const char* VariantSuffix(SchoolDaysRouteMapModel::Variant variant) {
    using Variant = SchoolDaysRouteMapModel::Variant;
    switch (variant) {
    case Variant::Standard: return "";
    case Variant::Wide: return "_Wide";
    case Variant::WideFull: return "_Wide_Full";
    case Variant::WideNote: return "_Wide_Note";
    }
    return "";
}


} // namespace

int SchoolDaysRouteMapModel::SubpageCountForMajor(int major) {
    if (major < 0 || major > 5) return 0;
    static constexpr int kCounts[6] = {1,1,2,4,4,4};
    return kCounts[major];
}

SchoolDaysRouteMapModel::PageInfo SchoolDaysRouteMapModel::Page(int major,
                                                               int subpage) {
    if (major < 0 || major > 5 || subpage < 0 ||
        subpage >= SubpageCountForMajor(major)) return {};
    return kPages[major][subpage];
}

int SchoolDaysRouteMapModel::NodeSelector(int major, int subpage,
                                          int localIndex) {
    const PageInfo page = Page(major, subpage);
    if (localIndex < 0 || localIndex >= page.nodeCount) return -1;
    return (major + 1) * 100 + page.nodeOffset + localIndex;
}

std::string SchoolDaysRouteMapModel::NodeSpKey(int major, int subpage,
                                               int localIndex) {
    const int selector = NodeSelector(major, subpage, localIndex);
    if (selector < 0) return {};
    char key[16]{};
    std::snprintf(key, sizeof(key), "SP%03d", selector);
    return key;
}

std::string SchoolDaysRouteMapModel::CmapRelativePath(int major, int subpage,
                                                      Variant variant) {
    if (major < 0 || major > 5 || subpage < 0 ||
        subpage >= SubpageCountForMajor(major)) return {};
    char path[128]{};
    if (major < 2) {
        std::snprintf(path, sizeof(path), "System/RouteMap/%02d/RouteMap%02d%s.cmap",
                      major + 1, major + 1, VariantSuffix(variant));
    } else {
        std::snprintf(path, sizeof(path),
                      "System/RouteMap/%02d/RouteMap%02d-%d%s.cmap",
                      major + 1, major + 1, subpage + 1, VariantSuffix(variant));
    }
    return path;
}

std::string SchoolDaysRouteMapModel::BaseRelativePath(int major, int subpage) {
    if (major < 0 || major > 5 || subpage < 0 ||
        subpage >= SubpageCountForMajor(major)) return {};
    char path[128]{};
    if (major < 2) {
        std::snprintf(path, sizeof(path), "System/RouteMap/%02d/RouteMap%02d.png",
                      major + 1, major + 1);
    } else {
        std::snprintf(path, sizeof(path), "System/RouteMap/%02d/RouteMap%02d-%d.png",
                      major + 1, major + 1, subpage + 1);
    }
    return path;
}

std::string SchoolDaysRouteMapModel::ChipRelativePath(int major, int subpage) {
    std::string base = BaseRelativePath(major, subpage);
    if (base.empty()) return {};
    const size_t dot = base.rfind(".png");
    if (dot == std::string::npos) return {};
    base.replace(dot, 4, "_Chip.png");
    return base;
}

bool SchoolDaysRouteMapModel::Open(const std::string& assetsRoot, Variant variant,
                                   int initialMajor, int initialSubpage,
                                   bool alternateHost, std::string* error) {
    Close();
    major_ = std::clamp(initialMajor, 0, 5);
    subpage_ = std::clamp(initialSubpage, 0, SubpageCountForMajor(major_) - 1);
    assetsRoot_ = assetsRoot;
    variant_ = variant;
    alternateHost_ = alternateHost;
    if (!Reload(error)) {
        Close();
        return false;
    }
    open_ = true;
    return true;
}

void SchoolDaysRouteMapModel::Close() {
    cmap_.Clear();
    assetsRoot_.clear();
    major_ = 0;
    subpage_ = 0;
    selection_ = -1;
    currentMarker_ = -1;
    alternateHost_ = false;
    nodeAvailable_.fill(false);
    nodeHistory_.fill(false);
    open_ = false;
}

void SchoolDaysRouteMapModel::ResetPageProjection() {
    nodeAvailable_.fill(false);
    nodeHistory_.fill(false);
    selection_ = -1;
}

bool SchoolDaysRouteMapModel::Reload(std::string* error) {
    std::string physical;
    const std::string logical = CmapRelativePath(major_, subpage_, variant_);
    if (!ResolveSchoolDaysRequiredAssetPath(
            assetsRoot_.c_str(), logical, &physical, error, "RouteMap CMAP"))
        return false;
    SchoolDaysCmap next;
    if (!next.LoadFile(physical, error)) return false;
    cmap_ = std::move(next);
    ResetPageProjection();
    return true;
}

int SchoolDaysRouteMapModel::HitControl(int x, int y) const {
    if (!open_) return -1;
    const int raw = cmap_.Get(x, y);
    return raw > 0 ? raw - 1 : -1;
}

bool SchoolDaysRouteMapModel::SetSelectionFromPointer(int x, int y) {
    if (!open_) return false;
    const int hit = HitControl(x, y);
    const bool changed = selection_ != hit;
    selection_ = hit;
    return changed;
}

void SchoolDaysRouteMapModel::SetNodeAvailable(int localIndex, bool available) {
    if (localIndex >= 0 && localIndex < static_cast<int>(nodeAvailable_.size()))
        nodeAvailable_[static_cast<size_t>(localIndex)] = available;
}

void SchoolDaysRouteMapModel::SetNodeHistory(int localIndex, bool seen) {
    if (localIndex >= 0 && localIndex < static_cast<int>(nodeHistory_.size()))
        nodeHistory_[static_cast<size_t>(localIndex)] = seen;
}

bool SchoolDaysRouteMapModel::NodeAvailable(int localIndex) const {
    const PageInfo p = CurrentPageInfo();
    return localIndex >= 0 && localIndex < p.nodeCount &&
           nodeAvailable_[static_cast<size_t>(localIndex)];
}

bool SchoolDaysRouteMapModel::NodeHistory(int localIndex) const {
    const PageInfo p = CurrentPageInfo();
    return localIndex >= 0 && localIndex < p.nodeCount &&
           nodeHistory_[static_cast<size_t>(localIndex)];
}

bool SchoolDaysRouteMapModel::IsControlAvailable(int control) const {
    if (!open_ || control < 0) return false;
    if (control == 0) return true;
    if (control >= 1 && control <= 5) return !alternateHost_;
    if (control == 6) return true;
    if (control == 7) return major_ > 0;
    if (control == 8) return !alternateHost_ && major_ < 5;
    if (control == 9) return subpage_ > 0;
    if (control == 10)
        return major_ >= 2 && subpage_ + 1 < SubpageCountForMajor(major_);
    const int local = control - 11;
    return NodeAvailable(local);
}

bool SchoolDaysRouteMapModel::SetMajor(int target, std::string* error) {
    target = std::clamp(target, 0, 5);
    if (target == major_) return true;
    const int oldMajor = major_;
    const int oldSubpage = subpage_;
    int nextSubpage = subpage_;
    if (oldMajor > 2 && target <= 2) nextSubpage = nextSubpage < 2 ? 0 : 1;
    else if (oldMajor < 3 && target >= 3) nextSubpage = nextSubpage == 0 ? 1 : 2;
    nextSubpage = std::clamp(nextSubpage, 0, SubpageCountForMajor(target) - 1);
    major_ = target;
    subpage_ = nextSubpage;
    if (!Reload(error)) {
        major_ = oldMajor;
        subpage_ = oldSubpage;
        return false;
    }
    return true;
}

bool SchoolDaysRouteMapModel::SetSubpage(int target, std::string* error) {
    const int maxPage = SubpageCountForMajor(major_) - 1;
    target = std::clamp(target, 0, maxPage);
    if (target == subpage_) return true;
    const int old = subpage_;
    subpage_ = target;
    if (!Reload(error)) {
        subpage_ = old;
        return false;
    }
    return true;
}

bool SchoolDaysRouteMapModel::Activate(Action* out, std::string* error) {
    if (!open_ || out == nullptr) return false;
    *out = {};
    const int control = selection_;
    if (!IsControlAvailable(control)) return false;

    if (control >= 0 && control <= 5) return SetMajor(control, error);
    if (control == 6) {
        out->kind = ActionKind::ReturnToLoad;
        return true;
    }
    if (control == 7) return SetMajor(major_ - 1, error);
    if (control == 8) return SetMajor(major_ + 1, error);
    if (control == 9) return SetSubpage(subpage_ - 1, error);
    if (control == 10) return SetSubpage(subpage_ + 1, error);

    const int local = control - 11;
    const int selector = NodeSelector(major_, subpage_, local);
    if (selector < 0) return false;
    out->kind = ActionKind::NodeRequested;
    out->selector = selector;
    return true;
}

} // namespace Kotonoha
