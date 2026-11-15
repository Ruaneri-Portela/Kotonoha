#pragma once

#include <cstdint>
#include <vector>

#include <Kotonoha/routing/SchoolDaysTitleModel.hpp>

namespace Kotonoha {

class SchoolDaysTitleRenderPlan {
public:
    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    enum class Texture : uint8_t { Overlay, Chip };
    struct Sprite {
        Texture texture = Texture::Overlay;
        bool hasSource = false;
        Rect source{};
        Rect destination{};
    };
    struct Layout {
        float scale = 1.0f;
        float yOffset = 75.0f;
        int virtualWidth = 800;
        int virtualHeight = 600;
    };

    static Layout LayoutFor(SchoolDaysTitleModel::Variant variant);
    static std::vector<Sprite> Build(const SchoolDaysTitleModel& model);
};

} // namespace Kotonoha
