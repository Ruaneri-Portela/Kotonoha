#include <Kotonoha/routing/SchoolDaysRouteMapRenderPlan.hpp>

#include <array>
#include <cstddef>
#include <string>

namespace Kotonoha {
namespace {

using Plan = SchoolDaysRouteMapRenderPlan;

struct NodePos {
    float x;
    float y;
};

struct PageGeometry {
    const NodePos* nodes;
    int count;
    int chipWidth;
    int chipHeight;
};

constexpr Plan::Record R(float dx, float dy, float w, float h,
                         float sx, float sy) {
    return {{sx, sy, w, h}, {dx, dy, w, h}};
}

// Records 0..32 are byte-for-byte identical across all 16 native page tables.
constexpr std::array<Plan::Record, 33> kCommon{{
    R(562,10,39,25,1,1),   R(601,10,38,25,41,1),  R(639,10,38,25,80,1),
    R(677,10,38,25,119,1), R(715,10,38,25,158,1), R(753,10,38,25,197,1),
    R(10,5,194,35,236,1),  R(7,45,56,24,236,37),  R(63,45,56,24,293,37),
    R(132,45,56,24,350,37),R(188,45,56,24,407,37),
    R(562,10,39,25,1,27),  R(601,10,38,25,41,27), R(639,10,38,25,80,27),
    R(677,10,38,25,119,27),R(715,10,38,25,158,27),R(753,10,38,25,197,27),
    R(562,10,39,25,1,53),  R(601,10,38,25,41,53), R(639,10,38,25,80,53),
    R(677,10,38,25,119,53),R(715,10,38,25,158,53),R(753,10,38,25,197,53),
    R(562,10,39,25,1,79),  R(601,10,38,25,41,79), R(639,10,38,25,80,79),
    R(677,10,38,25,119,79),R(715,10,38,25,158,79),R(753,10,38,25,197,79),
    R(7,45,56,24,236,62),  R(63,45,56,24,293,62), R(132,45,56,24,350,62),
    R(188,45,56,24,407,62),
}};

constexpr NodePos k00[] = {{21,242},{186,242},{388,242},{741,242}};
constexpr NodePos k10[] = {{25,240},{122,334},{186,179},{239,383},{326,266},
                           {386,266},{655,346},{741,204},{741,346}};
constexpr NodePos k20[] = {{48,204},{48,347},{266,204},{495,263},{533,148},
                           {735,148},{735,263}};
constexpr NodePos k21[] = {{115,238},{459,165},{735,181},{734,297}};
constexpr NodePos k30[] = {{250,226},{315,226},{371,331},{431,152},{623,134},
                           {623,183},{623,307},{623,392}};
constexpr NodePos k31[] = {{21,148},{21,263},{129,264},{244,305},{408,263},
                           {478,263},{741,220},{741,333}};
constexpr NodePos k32[] = {{21,184},{21,300},{75,184},{129,184},{151,282},
                           {309,98},{326,208},{369,242},{412,316},{481,197},
                           {500,139},{519,87},{554,348},{741,246},{741,365}};
constexpr NodePos k33[] = {{129,155},{421,155},{741,133},{741,268}};
constexpr NodePos k40[] = {{21,134},{21,344},{147,134},{278,166},{353,196},
                           {478,142},{569,100},{727,142},{727,255},{727,316},
                           {727,374}};
constexpr NodePos k41[] = {{21,220},{21,333},{87,220},{87,333},{153,219},
                           {155,333},{223,237},{257,141},{329,121},{428,167},
                           {503,189},{510,372},{520,104},{523,333},{540,284},
                           {567,205},{586,148},{602,105}};
constexpr NodePos k42[] = {{21,101},{21,245},{21,300},{21,366},{86,100},
                           {155,128},{176,270},{202,338},{221,148},{248,382},
                           {262,338},{303,118},{388,198},{409,136},{727,90},
                           {727,231},{727,293},{727,347},{727,401}};
constexpr NodePos k43[] = {{21,132},{21,268},{79,268},{137,109},{251,321},
                           {327,185},{481,165},{512,85},{521,321}};
constexpr NodePos k50[] = {{37,141},{37,255},{37,315},{37,374},{142,315},
                           {292,81},{292,201},{292,289},{292,342},{441,227},
                           {689,202}};
constexpr NodePos k51[] = {{37,177},{37,383},{118,100},{213,123},{377,88},
                           {377,140},{377,189},{377,243},{377,289}};
constexpr NodePos k52[] = {{37,90},{37,170},{37,230},{37,292},{37,346},
                           {37,400},{184,244},{330,76},{349,220},{349,291},
                           {349,362},{558,220},{718,220}};
constexpr NodePos k53[] = {{38,121},{37,217},{37,355},{95,121},{94,217},
                           {94,355},{155,121},{210,251},{617,80},{617,135},
                           {617,186},{686,355}};

constexpr PageGeometry kPages[6][4] = {
    {{k00,4,463,260},{nullptr,0,0,0},{nullptr,0,0,0},{nullptr,0,0,0}},
    {{k10,9,463,260},{nullptr,0,0,0},{nullptr,0,0,0},{nullptr,0,0,0}},
    {{k20,7,463,260},{k21,4,463,260},{nullptr,0,0,0},{nullptr,0,0,0}},
    {{k30,8,463,260},{k31,8,463,260},{k32,15,585,260},{k33,4,463,260}},
    {{k40,11,463,260},{k41,18,702,262},{k42,19,741,262},{k43,9,463,260}},
    {{k50,11,463,260},{k51,9,463,260},{k52,13,507,260},{k53,12,468,260}},
};

const PageGeometry* Page(int major, int subpage) {
    if (major < 0 || major >= 6 || subpage < 0 || subpage >= 4) return nullptr;
    const PageGeometry& page = kPages[major][subpage];
    return page.nodes != nullptr ? &page : nullptr;
}

float NodeSize(int major, int subpage, int localIndex) {
    // Exact v1.02 exceptions recovered from 0x100475F0 and 0x10047FC8.
    if (major == 4 && (subpage == 1 || subpage == 2) && localIndex == 4)
        return 40.0f;
    return 38.0f;
}

Plan::Rect Transform(const Plan::Rect& r, const Plan::Layout& layout) {
    return {(r.x - 0.5f) * layout.scale,
            (r.y - 0.5f + layout.yOffset) * layout.scale,
            (r.w + 1.0f) * layout.scale,
            (r.h + 1.0f) * layout.scale};
}

Plan::Sprite Base(const Plan::Layout& layout) {
    Plan::Sprite sprite;
    sprite.texture = Plan::Texture::Base;
    sprite.role = Plan::Role::Base;
    sprite.destination = {-0.5f * layout.scale,
                          layout.yOffset * layout.scale,
                          800.0f * layout.scale,
                          450.0f * layout.scale};
    return sprite;
}

Plan::Sprite FromRecord(Plan::Role role, int index, int recordIndex,
                        const Plan::Record& record,
                        const Plan::Layout& layout) {
    Plan::Sprite sprite;
    sprite.texture = Plan::Texture::Chip;
    sprite.role = role;
    sprite.index = index;
    sprite.recordIndex = recordIndex;
    sprite.hasSource = true;
    sprite.source = record.source;
    sprite.destination = Transform(record.destination, layout);
    return sprite;
}

} // namespace

SchoolDaysRouteMapRenderPlan::Layout SchoolDaysRouteMapRenderPlan::LayoutFor(
    SchoolDaysRouteMapModel::Variant variant) {
    using Variant = SchoolDaysRouteMapModel::Variant;
    switch (variant) {
    case Variant::Standard: return {1.0f, 75.0f, 800, 600};
    case Variant::Wide: return {1.0f, 0.0f, 800, 450};
    case Variant::WideFull: return {1.6f, 0.0f, 1280, 720};
    case Variant::WideNote: return {1.28f, 0.0f, 1024, 576};
    }
    return {1.0f, 75.0f, 800, 600};
}

SchoolDaysRouteMapRenderPlan::Size SchoolDaysRouteMapRenderPlan::ChipSizeForPage(
    int major, int subpage) {
    const PageGeometry* page = Page(major, subpage);
    return page != nullptr ? Size{page->chipWidth, page->chipHeight} : Size{};
}

SchoolDaysRouteMapRenderPlan::Record SchoolDaysRouteMapRenderPlan::CommonRecord(
    int recordIndex) {
    if (recordIndex < 0 || recordIndex >= static_cast<int>(kCommon.size())) return {};
    return kCommon[static_cast<size_t>(recordIndex)];
}

int SchoolDaysRouteMapRenderPlan::NodeRecordIndex(
    int major, int subpage, int localIndex, NodeBand band) {
    const PageGeometry* page = Page(major, subpage);
    if (page == nullptr || localIndex < 0 || localIndex >= page->count) return -1;
    const int bandIndex = static_cast<int>(band);
    if (bandIndex < 0 || bandIndex > 3) return -1;
    return 33 + bandIndex * page->count + localIndex;
}

SchoolDaysRouteMapRenderPlan::Record SchoolDaysRouteMapRenderPlan::NodeRecord(
    int major, int subpage, int localIndex, NodeBand band) {
    const PageGeometry* page = Page(major, subpage);
    if (page == nullptr || localIndex < 0 || localIndex >= page->count) return {};
    const int bandIndex = static_cast<int>(band);
    if (bandIndex < 0 || bandIndex > 3) return {};

    const NodePos pos = page->nodes[localIndex];
    const float size = NodeSize(major, subpage, localIndex);
    return R(pos.x, pos.y, size, size,
             1.0f + 39.0f * static_cast<float>(localIndex),
             105.0f + 39.0f * static_cast<float>(bandIndex));
}

SchoolDaysRouteMapRenderPlan::CompletionText
SchoolDaysRouteMapRenderPlan::CompletionTextFor(
    const SchoolDaysRouteMapModel& model, bool useEnglish) {
    CompletionText out;
    const Layout layout = LayoutFor(model.CurrentVariant());
    out.displayedPercent = model.AlternateHost() ? 0 : model.CompletionPercent();
    out.color = model.AlternateHost() ? UINT32_C(0xFF7F7F7F)
                                      : UINT32_C(0xFFFFFFFF);

    std::u16string value;
    if (out.displayedPercent < 10) value = u"  ";
    else if (out.displayedPercent < 100) value = u" ";
    const std::string digits = std::to_string(out.displayedPercent);
    for (char digit : digits) value.push_back(static_cast<char16_t>(digit));

    if (useEnglish) {
        out.text = u"  ";
        out.text += value;
        out.text += u"\uFF05   Complete";
    } else {
        out.text = u"\u30B7\u30CA\u30EA\u30AA\u9054\u6210\u7387";
        out.text += value;
        out.text += u"\uFF05";
    }

    // Authenticated RouteMap text object placement from SysMenuSDHQ v1.02.
    out.destination = {305.0f * layout.scale,
                       (layout.yOffset + 12.0f) * layout.scale,
                       288.0f * layout.scale, 24.0f * layout.scale};
    return out;
}

std::vector<SchoolDaysRouteMapRenderPlan::Sprite>
SchoolDaysRouteMapRenderPlan::Build(const SchoolDaysRouteMapModel& model) {
    std::vector<Sprite> out;
    if (!model.IsOpen()) return out;

    const int major = model.Major();
    const int subpage = model.Subpage();
    const PageGeometry* page = Page(major, subpage);
    if (page == nullptr) return out;

    const Layout layout = LayoutFor(model.CurrentVariant());
    out.reserve(static_cast<size_t>(page->count + 24));
    out.push_back(Base(layout));

    // Native first pass: exactly one locked/available record per local node.
    for (int i = 0; i < page->count; ++i) {
        const NodeBand band = model.NodeAvailable(i) ? NodeBand::Available
                                                     : NodeBand::Locked;
        const Role role = model.NodeAvailable(i) ? Role::NodeAvailable
                                                 : Role::NodeLocked;
        out.push_back(FromRecord(role, i, NodeRecordIndex(major, subpage, i, band),
                                 NodeRecord(major, subpage, i, band), layout));
    }

    // Native common pass: records 0..10 are always emitted in ascending order.
    for (int record = 0; record <= 10; ++record) {
        out.push_back(FromRecord(Role::CommonControl, record, record,
                                 CommonRecord(record), layout));
    }

    // +0x3E4 is a native control index.  The original additionally verifies
    // GetRouteMapPage before drawing; the model projection supplies -1 when the
    // marker does not belong to the open page.
    const int marker = model.CurrentMarker();
    const int currentLocal = marker - 11;
    if (currentLocal >= 0 && currentLocal < page->count) {
        const int record = NodeRecordIndex(major, subpage, currentLocal,
                                           NodeBand::Current);
        out.push_back(FromRecord(Role::NodeCurrent, currentLocal, record,
                                 NodeRecord(major, subpage, currentLocal,
                                            NodeBand::Current), layout));
    }

    const int primary = 11 + major;
    out.push_back(FromRecord(Role::CurrentMajorPrimary, major, primary,
                             CommonRecord(primary), layout));
    const int secondary = 17 + major;
    out.push_back(FromRecord(Role::CurrentMajorSecondary, major, secondary,
                             CommonRecord(secondary), layout));

    // Host virtual +0x34 gates this six-record pass; RouteMapModel names the
    // same proven host distinction alternateHost.
    if (model.AlternateHost()) {
        for (int record = 23; record <= 28; ++record) {
            out.push_back(FromRecord(Role::AlternateHostMajorMask, record - 23,
                                     record, CommonRecord(record), layout));
        }
    }

    for (int record = 29; record <= 32; ++record) {
        out.push_back(FromRecord(Role::NavigationOverlay, record - 29, record,
                                 CommonRecord(record), layout));
    }
    return out;
}

} // namespace Kotonoha
