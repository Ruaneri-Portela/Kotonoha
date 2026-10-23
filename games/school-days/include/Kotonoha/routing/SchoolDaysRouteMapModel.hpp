#pragma once

#include <array>
#include <string>

#include <Kotonoha/routing/SchoolDaysCmap.hpp>
#include <Kotonoha/routing/SchoolDaysSaveFeelModel.hpp>

namespace Kotonoha {

// Pure pointer/state model for MENU::RouteMap (outer mode6).  The selected
// route-node selector is deliberately surfaced as a typed request; its result8
// downstream effect remains owned by the outer host until separately proven.
class SchoolDaysRouteMapModel {
public:
    using Variant = SchoolDaysSaveFeelModel::Variant;

    enum class ActionKind {
        None,
        ReturnToLoad,
        NodeRequested,
    };

    struct Action {
        ActionKind kind = ActionKind::None;
        int selector = -1; // native host +0x48 value for result8
    };

    struct PageInfo {
        int nodeOffset = 0;
        int nodeCount = 0;
    };

    bool Open(const std::string& assetsRoot, Variant variant, int initialMajor,
              int initialSubpage, bool alternateHost,
              std::string* error = nullptr);
    void Close();

    bool SetSelectionFromPointer(int x, int y);
    int HitControl(int x, int y) const;
    bool IsControlAvailable(int control) const;
    bool Activate(Action* out, std::string* error = nullptr);

    void SetNodeAvailable(int localIndex, bool available);
    void SetNodeHistory(int localIndex, bool seen);
    void SetCurrentMarker(int marker) { currentMarker_ = marker; }

    bool IsOpen() const { return open_; }
    int Major() const { return major_; }
    int Subpage() const { return subpage_; }
    int Selection() const { return selection_; }
    int CurrentMarker() const { return currentMarker_; }
    bool AlternateHost() const { return alternateHost_; }
    Variant CurrentVariant() const { return variant_; }
    const SchoolDaysCmap& Cmap() const { return cmap_; }
    PageInfo CurrentPageInfo() const { return Page(major_, subpage_); }
    int SubpageCount() const { return SubpageCountForMajor(major_); }
    bool NodeAvailable(int localIndex) const;
    bool NodeHistory(int localIndex) const;

    static int SubpageCountForMajor(int major);
    static PageInfo Page(int major, int subpage);
    static int NodeSelector(int major, int subpage, int localIndex);
    static std::string NodeSpKey(int major, int subpage, int localIndex);
    static std::string CmapRelativePath(int major, int subpage, Variant variant);
    static std::string BaseRelativePath(int major, int subpage);
    static std::string ChipRelativePath(int major, int subpage);

private:
    bool Reload(std::string* error);
    bool SetMajor(int target, std::string* error);
    bool SetSubpage(int target, std::string* error);
    void ResetPageProjection();

    std::string assetsRoot_;
    SchoolDaysCmap cmap_;
    Variant variant_ = Variant::Standard;
    int major_ = 0;
    int subpage_ = 0;
    int selection_ = -1;
    int currentMarker_ = -1;
    bool alternateHost_ = false;
    std::array<bool, 19> nodeAvailable_{};
    std::array<bool, 19> nodeHistory_{};
    bool open_ = false;
};

} // namespace Kotonoha
