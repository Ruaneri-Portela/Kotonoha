#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <Kotonoha/routing/SchoolDaysConfigMenuModel.hpp>

namespace Kotonoha {

// Renderer-neutral projection of the authenticated MENU::ConfigMenu draw
// contract. Coordinates remain on the original logical authoring canvas; the
// presenter maps that logical surface to the current physical output.
class SchoolDaysConfigMenuRenderPlan {
public:
    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    enum class Texture : uint8_t {
        Base,
        SomConSetBase,
        Chip,
    };

    enum class Role : uint8_t {
        Base,
        ActiveTab,
        CurrentState,
        Hover,
        SoundLevel,
    };

    struct Sprite {
        Texture texture = Texture::Base;
        Role role = Role::Base;
        bool hasSource = false;
        Rect source{};
        Rect destination{};
        int recordIndex = -1;
        int control = -1;
        int soundBank = -1;
    };

    struct Layout {
        float scale = 1.0f;
        float yOffset = 75.0f;
        int logicalWidth = 800;
        int logicalHeight = 600;
    };

    // These are visual inputs only. Control ids deliberately preserve the
    // native pair orientation instead of translating host getters into new
    // display/audio policy.
    struct VisualState {
        bool somConAvailable = true;

        int defWideCurrent = 5;        // native pair 4/5
        int defFullCurrent = 6;        // native pair 6/7
        int defSkipCurrent = 9;        // native pair 8/9
        int defSuperSkipCurrent = 11;  // native pair 10/11
        int defDrawMessageCurrent = 13;// native pair 12/13

        int soundMenVoiceCurrent = 11; // native pair 10/11
        int soundMuteCurrent = 13;     // native pair 12/13
        std::array<int, 3> soundLevels{{0, 0, 0}}; // native range 0..10

        // Opaque SomCon visual state. No device behavior is inferred here.
        bool somConSetActive = false;          // object +0x328
        bool somConInteractionActive = false;  // object +0x31C
        int somConSecondaryCurrent = 17;       // native pair 16/17 (+0x320)
        int somConDeviceIndex = 0;             // object +0x324, native 0..9
    };

    static Layout LayoutFor(SchoolDaysConfigMenuModel::Variant variant);
    static std::string BaseRelativePath(const SchoolDaysConfigMenuModel& model,
                                        const VisualState& state);
    static std::vector<Sprite> Build(const SchoolDaysConfigMenuModel& model,
                                     const VisualState& state);
};

} // namespace Kotonoha
