#pragma once

#include <cstdint>
#include <vector>

#include <Kotonoha/routing/SchoolDaysRouteMapModel.hpp>

namespace Kotonoha {

// Renderer-neutral projection of the authenticated MENU::RouteMap v1.02
// sprite tables.  All dimensions are authored logical-canvas coordinates;
// physical output sizing belongs to the presenter/viewport layer.
class SchoolDaysRouteMapRenderPlan {
public:
    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    struct Record {
        Rect source{};
        Rect destination{};
    };

    struct Size {
        int width = 0;
        int height = 0;
    };

    enum class Texture : uint8_t { Base, Chip };

    enum class Role : uint8_t {
        Base,
        NodeLocked,
        NodeAvailable,
        CommonControl,
        NodeCurrent,
        CurrentMajorPrimary,
        CurrentMajorSecondary,
        AlternateHostMajorMask,
        NavigationOverlay,
    };

    // Native table layout is exactly 33 common records followed by four N-wide
    // node bands.  Band0 is identified in the authenticated table as the hover
    // record family; the traced draw paths do not prove when it supersedes the
    // locked/available/current passes, so Build() deliberately does not invent
    // such precedence.
    enum class NodeBand : uint8_t {
        Hover = 0,
        Locked = 1,
        Current = 2,
        Available = 3,
    };

    struct Sprite {
        Texture texture = Texture::Base;
        Role role = Role::Base;
        int index = -1;
        int recordIndex = -1;
        bool hasSource = false;
        Rect source{};
        Rect destination{};
    };

    struct Layout {
        float scale = 1.0f;
        float yOffset = 75.0f;
        int logicalWidth = 800;
        int logicalHeight = 600;
    };

    static Layout LayoutFor(SchoolDaysRouteMapModel::Variant variant);
    static Size ChipSizeForPage(int major, int subpage);
    static Record CommonRecord(int recordIndex);
    static Record NodeRecord(int major, int subpage, int localIndex,
                             NodeBand band);
    static int NodeRecordIndex(int major, int subpage, int localIndex,
                               NodeBand band);

    // This remains false until a native path proves band0 composition order.
    static constexpr bool NodeHoverPrecedenceProven() { return false; }

    // Draw order follows SysMenuSDHQ.dll FUN_1000CE40/FUN_1000F330:
    // base, per-node locked/available pass, common 0..10, optional current
    // node, current-major records 11+major and 17+major, optional alternate
    // host records 23..28, then records 29..32.
    static std::vector<Sprite> Build(const SchoolDaysRouteMapModel& model);
};

} // namespace Kotonoha
