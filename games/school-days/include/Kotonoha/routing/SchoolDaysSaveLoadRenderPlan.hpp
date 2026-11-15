#pragma once

#include <cstdint>
#include <vector>

#include <Kotonoha/routing/SchoolDaysSaveLoadModel.hpp>

namespace Kotonoha {

// Renderer-neutral projection of authenticated MENU::SaveLoad sprite records.
// SAVE/LOAD share rows/pages/close; RouteMap is LOAD-only. Display variants
// differ only by authored destination scale/y-offset and logical CMAP domain.
class SchoolDaysSaveLoadRenderPlan {
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
        RowHover,
        CurrentPage,
        CurrentPageHover,
        CloseHover,
        RouteMap,
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

    struct MetadataRect {
        int row = -1;
        Rect primarySource{};
        Rect secondarySource{};
        Rect optionalThirdSource{};
        Rect primary{};
        Rect secondary{};
        Rect optionalThird{};
    };

    static Layout LayoutFor(SchoolDaysSaveLoadModel::Variant variant);
    // MENU::SaveLoad 0x10011DC0: exact horizontal advance used before the
    // host +0x58 FONTDATA glyph blit.
    static int GlyphAdvance(uint16_t glyph, bool useEnglish);
    static int PrimaryCharacterLimit(bool useEnglish) {
        return useEnglish ? 21 : 20;
    }
    static int SecondaryCharacterLimit() { return 20; }
    static int CommentCharacterLimit(bool useEnglish) {
        return useEnglish ? 40 : 20;
    }
    static std::vector<Sprite> Build(const SchoolDaysSaveLoadModel& model);
    static std::vector<MetadataRect> MetadataLayout(
        SchoolDaysSaveLoadModel::Variant variant, bool useEnglish,
        const std::vector<int>* optionalRenderedAdvance = nullptr);
};

} // namespace Kotonoha
