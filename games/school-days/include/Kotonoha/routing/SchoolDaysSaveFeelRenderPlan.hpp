#pragma once

#include <cstdint>
#include <vector>

#include <Kotonoha/routing/SchoolDaysSaveFeelModel.hpp>

namespace Kotonoha {

// Renderer-neutral projection of the authenticated MENU::SaveFeelView sprite
// records.  Coordinates stay in the original SysMenu logical surface; the SDL
// presenter is responsible only for mapping that logical surface to the actual
// render output.
class SchoolDaysSaveFeelRenderPlan {
public:
    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    enum class Texture : uint8_t {
        Base,
        Chip,
    };

    enum class Role : uint8_t {
        Base,
        SelectedChoice,
        Feeling002Bar,
        Feeling001Bar,
        CountdownTens,
        CountdownOnes,
    };

    struct Sprite {
        Texture texture = Texture::Base;
        Role role = Role::Base;
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

    static Layout LayoutFor(SchoolDaysSaveFeelModel::Variant variant);
    static std::vector<Sprite> Build(const SchoolDaysSaveFeelModel& model);
};

} // namespace Kotonoha
