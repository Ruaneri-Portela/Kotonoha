#pragma once

#include <cstdint>
#include <vector>

#include <Kotonoha/routing/SchoolDaysExitViewModel.hpp>

namespace Kotonoha {

class SchoolDaysExitViewRenderPlan {
public:
    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    enum class Texture : uint8_t { Base, Chip };
    enum class Role : uint8_t { Base, Hover };

    struct Sprite {
        Texture texture = Texture::Base;
        Role role = Role::Base;
        bool hasSource = false;
        Rect source{};
        Rect destination{};
        int choice = -1;
    };

    struct Layout {
        float scale = 1.0f;
        float yOffset = 75.0f;
        int logicalWidth = 800;
        int logicalHeight = 600;
    };

    static Layout LayoutFor(SchoolDaysExitViewModel::Variant variant);
    static std::vector<Sprite> Build(const SchoolDaysExitViewModel& model);
};

} // namespace Kotonoha
