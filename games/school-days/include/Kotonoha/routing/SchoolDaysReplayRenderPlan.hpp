#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <Kotonoha/routing/SchoolDaysReplayModel.hpp>

namespace Kotonoha {

// Renderer-neutral projection of the authenticated MENU::SceneView and nested
// MENU::SceneCheck sprite tables. Legacy dimensions are logical canvases only;
// the presenter applies the modern output viewport afterwards.
class SchoolDaysReplayRenderPlan {
public:
    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    enum class Texture : uint8_t { Base, Chip, Thumbnail };
    enum class Role : uint8_t {
        Base,
        TabHover,
        CurrentTab,
        CurrentTabHover,
        CloseHover,
        PageHover,
        CurrentPage,
        CurrentPageHover,
        LockedSlot,
        ThumbnailSepia,
        ThumbnailHover,
        PlayDataRowHover,
        SceneCheckChoiceHover,
        SceneCheckPreview,
    };

    struct Sprite {
        Texture texture = Texture::Base;
        Role role = Role::Base;
        int index = -1;
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

    struct SceneCheckVisualState {
        // Authenticated Pop2 preview record is 2..4; Pop4 uses record4.
        // -1 suppresses preview until the host has provided a proven selector.
        int previewRecord = -1;
        std::array<bool, 4> enabled{{false, false, false, false}};
    };

    static Layout LayoutFor(SchoolDaysReplayModel::Variant variant);
    static std::vector<Sprite> Build(const SchoolDaysReplayModel& model);
    static std::vector<Sprite> BuildSceneCheck(
        const SchoolDaysReplayModel& model,
        const SceneCheckVisualState& visualState);

    static std::string ThumbnailRelativePath(int page);
};

} // namespace Kotonoha
