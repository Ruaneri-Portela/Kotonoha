#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <Kotonoha/routing/SchoolDaysCmap.hpp>

namespace Kotonoha {

// Pure transient projection of FILM's visible BackLog list (manager+0x08).
// Authenticated v1.02 keeps ScriptChange/RoutePoint/choice records in separate
// collections; the visible list itself contains BLog_LogMessage (type 2).
class SchoolDaysBackLogStore {
public:
    struct Entry {
        int nativeType = 0;
        int historyIndex = -1;
        std::u16string character;
        std::u16string text;

        bool IsLogMessage() const { return nativeType == 2; }
    };

    size_t AppendLogMessage(int historyIndex, std::u16string resolvedCharacter,
                            std::u16string resolvedText);

    // Mirrors ScriptChange (0x432A10): revisiting a known SceneKey restores the
    // raw message prefix recorded when that script was last activated, then
    // publishes the current raw count as the script's new anchor.
    void ObserveScriptChange(const std::string& sceneKey);
    // Mirrors the manager's prefix-preserving truncate helper 0x4348E0.
    void TruncateRaw(size_t rawCount);
    // Used by new-session/load reset paths; both messages and ScriptChange
    // anchors are intentionally transient.
    void ResetTransient();

    size_t Size() const { return entries_.size(); }
    bool Empty() const { return entries_.empty(); }
    const Entry* At(size_t rawIndex) const;
    const std::vector<Entry>& Entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> scriptAnchors_;
};

class SchoolDaysBackLogModel {
public:
    enum class Orientation : uint8_t { Vertical, Horizon };
    enum class Variant : uint8_t { Standard, Wide, WideFull, WideNote };

    enum class ActionKind : uint8_t { None, CursorChanged, Close };
    struct Action {
        ActionKind kind = ActionKind::None;
        int rawIndex = -1;
    };

    struct Rect {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };
    struct ControlGeometry {
        int rawRegion = 0; // CMAP values 1..5, stored selection is rawRegion-1.
        Rect bounds{};
    };
    struct Layout {
        int logicalWidth = 0;
        int logicalHeight = 0;
        std::array<ControlGeometry, 5> controls{};
    };

    bool Open(const std::string& assetsRoot, const SchoolDaysBackLogStore& store,
              Orientation orientation, Variant variant,
              std::string* error = nullptr);
    void Close();

    int HitControl(int x, int y) const;
    bool SetSelectionFromPointer(int x, int y);

    // Mouse commit is intentionally authenticated: the release hit must equal
    // the selection stored by the preceding pointer-motion update.
    bool ActivatePointer(int x, int y, Action* out);

    bool IsOpen() const { return open_; }
    int Selection() const { return selection_; }
    int CurrentRawIndex() const { return currentRawIndex_; }
    size_t RawCountSnapshot() const { return rawCountSnapshot_; }
    Orientation CurrentOrientation() const { return orientation_; }
    Variant CurrentVariant() const { return variant_; }
    const SchoolDaysCmap& Cmap() const { return cmap_; }
    const SchoolDaysBackLogStore* Store() const { return store_; }

    static Layout LayoutFor(Orientation orientation, Variant variant);
    static std::string CmapRelativePath(Orientation orientation, Variant variant);
    static std::string BaseRelativePath(Orientation orientation);
    static std::string ChipRelativePath(Orientation orientation);

private:
    static int DeltaForSelection(int selection);

    SchoolDaysCmap cmap_;
    const SchoolDaysBackLogStore* store_ = nullptr;
    Orientation orientation_ = Orientation::Vertical;
    Variant variant_ = Variant::Standard;
    size_t rawCountSnapshot_ = 0;
    int currentRawIndex_ = -1;
    int selection_ = -1;
    bool open_ = false;
};

// Renderer-neutral text projection. Native BackLogView measures each type-2
// row with its font renderer while walking current -> previous -> following
// raw indices.  This plan preserves that proven traversal but deliberately
// does not guess glyph metrics, row heights, clipping, or raster placement.
class SchoolDaysBackLogRenderPlan {
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

    struct Layout {
        float scale = 1.0f;
        float yOffset = 75.0f;
        int logicalWidth = 800;
        int logicalHeight = 600;
    };

    enum class Texture : uint8_t { Base, Chip };
    enum class Role : uint8_t { Base, SelectedControl };

    struct Sprite {
        Texture texture = Texture::Base;
        Role role = Role::Base;
        int control = -1;
        bool hasSource = false;
        Rect source{};
        Rect destination{};
    };

    enum class Phase : uint8_t { Current, Previous, Following };
    struct TextEntry {
        size_t rawIndex = 0;
        int historyIndex = -1;
        Phase phase = Phase::Current;
        std::u16string character;
        std::u16string text;
    };

    struct NativeGlyph {
        uint16_t codepoint = 0;
        int x = 0;
        int y = 0;
    };

    struct NativeTextBlock {
        size_t rawIndex = 0;
        Phase phase = Phase::Current;
        int top = 0;
        int extent = 0;
        std::vector<NativeGlyph> glyphs;
    };

    struct NativeTextPlan {
        bool valid = false;
        bool unsupportedMarkup = false;
        std::vector<NativeTextBlock> blocks;
    };

    static std::vector<TextEntry> BuildTextEntries(
        const SchoolDaysBackLogModel& model);

    // Exact v1.02 BackLog base/selected-control composition.  CHIP records are
    // authored in the 800x450 resource space and receive the native half-pixel
    // destination expansion before the logical profile is aspect-fitted by the
    // presenter.  Normal control art is baked into the base PNG.
    static Layout LayoutFor(SchoolDaysBackLogModel::Variant variant);
    static Record ControlRecord(SchoolDaysBackLogModel::Orientation orientation,
                                int control);
    static std::vector<Sprite> BuildSkin(const SchoolDaysBackLogModel& model);

    // Retail v1.02 uses UseEnglish=1 and BackLogType=0 (Horizon).  These are
    // the exact character-count word wrap, FONTDATA advances and 2048x1024
    // backing-surface coordinates used by 0x10002A90/0x100022A0/0x10003600.
    static int RetailEnglishGlyphAdjustment(uint16_t codepoint);
    static int RetailEnglishGlyphAdvance(uint16_t codepoint);
    static std::vector<std::u16string> WrapRetailEnglish(
        const std::u16string& text, bool* unsupportedMarkup = nullptr);
    static int MeasureRetailEnglish(const SchoolDaysBackLogStore::Entry& entry,
                                    bool* unsupportedMarkup = nullptr);
    static NativeTextPlan BuildRetailHorizonEnglish(
        const SchoolDaysBackLogModel& model);
    static Rect TextSurfaceSource();
    static Rect TextSurfaceDestination(SchoolDaysBackLogModel::Variant variant);

    static constexpr bool RequiresNativeTextMeasurement() { return false; }
};

} // namespace Kotonoha
