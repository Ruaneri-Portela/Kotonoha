#include <Kotonoha/routing/SchoolDaysBackLog.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>

#include <algorithm>
#include <utility>

namespace Kotonoha {
namespace {

using Model = SchoolDaysBackLogModel;

constexpr Model::ControlGeometry C(int raw, int x, int y, int w, int h) {
    return {raw, {x, y, w, h}};
}

// Exact inclusive CMAP region bounds from the shipped eight BackLog maps.
constexpr Model::Layout kVerticalLayouts[4] = {
    {800, 600, {C(1,10,484,90,34), C(2,100,484,89,34),
                C(3,611,484,89,34), C(4,700,484,90,34), C(5,702,81,98,42)}},
    {800, 450, {C(1,10,409,90,34), C(2,100,409,89,34),
                C(3,611,409,89,34), C(4,700,409,90,34), C(5,702,6,98,42)}},
    {1280, 720, {C(1,16,654,144,55), C(2,160,654,142,55),
                 C(3,978,654,142,55), C(4,1120,654,144,55), C(5,1123,10,157,67)}},
    {1024, 576, {C(1,13,523,115,44), C(2,128,523,114,44),
                 C(3,782,523,114,44), C(4,896,523,115,44), C(5,899,8,125,53)}},
};

constexpr Model::Layout kHorizonLayouts[4] = {
    {800, 600, {C(1,758,138,36,92), C(2,758,230,36,89),
                C(3,758,319,36,89), C(4,758,408,36,92), C(5,702,81,98,42)}},
    {800, 450, {C(1,758,63,36,92), C(2,758,155,36,89),
                C(3,758,244,36,89), C(4,758,333,36,92), C(5,702,6,98,42)}},
    {1280, 720, {C(1,1213,101,57,147), C(2,1213,248,57,142),
                 C(3,1213,390,57,143), C(4,1213,533,57,147), C(5,1123,10,157,67)}},
    {1024, 576, {C(1,970,81,46,117), C(2,970,198,46,114),
                 C(3,970,312,46,114), C(4,970,426,46,118), C(5,898,8,126,54)}},
};

int VariantIndex(Model::Variant variant) {
    switch (variant) {
    case Model::Variant::Standard: return 0;
    case Model::Variant::Wide: return 1;
    case Model::Variant::WideFull: return 2;
    case Model::Variant::WideNote: return 3;
    }
    return 0;
}

const char* VariantSuffix(Model::Variant variant) {
    switch (variant) {
    case Model::Variant::Standard: return "";
    case Model::Variant::Wide: return "_Wide";
    case Model::Variant::WideFull: return "_Wide_Full";
    case Model::Variant::WideNote: return "_Wide_Note";
    }
    return "";
}

const char* OrientationName(Model::Orientation orientation) {
    return orientation == Model::Orientation::Vertical ? "Vertical" : "Horizon";
}

using Plan = SchoolDaysBackLogRenderPlan;

constexpr Plan::Record R(float dx, float dy, float w, float h,
                         float sx, float sy) {
    return {{sx, sy, w, h}, {dx, dy, w, h}};
}

// Exact five-record tables at SysMenuSDHQ.dll 0x10043030 (Vertical) and
// 0x100430A8 (Horizon).  Records are {dst x/y/w/h, src x/y}.
constexpr std::array<Plan::Record, 5> kVerticalChip{{
    R(10,409,90,34,1,1), R(100,409,89,34,92,1),
    R(611,409,89,34,1,36), R(700,409,90,34,91,36),
    R(702,6,98,42,1,71),
}};

constexpr std::array<Plan::Record, 5> kHorizonChip{{
    R(758,63,36,92,1,1), R(758,155,36,89,38,1),
    R(758,244,36,89,75,1), R(758,333,36,92,112,1),
    R(702,6,98,42,149,1),
}};

Plan::Rect TransformChip(const Plan::Rect& authored,
                         const Plan::Layout& layout) {
    return {(authored.x - 0.5f) * layout.scale,
            (authored.y - 0.5f + layout.yOffset) * layout.scale,
            (authored.w + 1.0f) * layout.scale,
            (authored.h + 1.0f) * layout.scale};
}

constexpr int kRetailWrapCapacity = 48;
constexpr int kRetailWrapLines = 8;
constexpr int kRetailCenter = 380;
constexpr int kRetailEnglishLimit = 1225;

bool IsUnsupportedRetailMarkup(uint16_t codepoint) {
    // 0x10002A90 routes these two codepoints into auxiliary native objects
    // rather than the ordinary FONTDATA pass.  The authenticated v1.02 English
    // ORS corpus contains neither marker in any PrintText payload, so keep the
    // unneeded auxiliary branch fail-closed instead of inventing it.
    return codepoint == 0x300Au || codepoint == 0xFF5Cu;
}

} // namespace

size_t SchoolDaysBackLogStore::AppendLogMessage(int historyIndex,
                                                std::u16string resolvedCharacter,
                                                std::u16string resolvedText) {
    entries_.push_back(
        {2, historyIndex, std::move(resolvedCharacter), std::move(resolvedText)});
    return entries_.size() - 1u;
}

void SchoolDaysBackLogStore::ObserveScriptChange(const std::string& sceneKey) {
    if (sceneKey.empty()) return;
    const auto found = scriptAnchors_.find(sceneKey);
    if (found != scriptAnchors_.end()) TruncateRaw(found->second);
    scriptAnchors_[sceneKey] = entries_.size();
}

void SchoolDaysBackLogStore::TruncateRaw(size_t rawCount) {
    if (rawCount < entries_.size())
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(rawCount),
                       entries_.end());
}

void SchoolDaysBackLogStore::ResetTransient() {
    entries_.clear();
    scriptAnchors_.clear();
}

const SchoolDaysBackLogStore::Entry* SchoolDaysBackLogStore::At(
    size_t rawIndex) const {
    return rawIndex < entries_.size() ? &entries_[rawIndex] : nullptr;
}

SchoolDaysBackLogModel::Layout SchoolDaysBackLogModel::LayoutFor(
    Orientation orientation, Variant variant) {
    const int index = VariantIndex(variant);
    return orientation == Orientation::Vertical ? kVerticalLayouts[index]
                                                : kHorizonLayouts[index];
}

std::string SchoolDaysBackLogModel::CmapRelativePath(Orientation orientation,
                                                     Variant variant) {
    return std::string("System/BackLog/BackLog_") + OrientationName(orientation) +
           VariantSuffix(variant) + ".cmap";
}

std::string SchoolDaysBackLogModel::BaseRelativePath(Orientation orientation) {
    return std::string("System/BackLog/BackLog_") + OrientationName(orientation) +
           ".png";
}

std::string SchoolDaysBackLogModel::ChipRelativePath(Orientation orientation) {
    return std::string("System/BackLog/BackLog_") + OrientationName(orientation) +
           "_Chip.png";
}

bool SchoolDaysBackLogModel::Open(const std::string& assetsRoot,
                                  const SchoolDaysBackLogStore& store,
                                  Orientation orientation, Variant variant,
                                  std::string* error) {
    Close();
    orientation_ = orientation;
    variant_ = variant;

    std::string cmapPath;
    if (!ResolveSchoolDaysRequiredAssetPath(
            assetsRoot.c_str(), CmapRelativePath(orientation, variant),
            &cmapPath, error, "BackLog CMAP") ||
        !cmap_.LoadFile(cmapPath, error)) {
        Close();
        return false;
    }

    const Layout expected = LayoutFor(orientation, variant);
    if (static_cast<int>(cmap_.Width()) != expected.logicalWidth ||
        static_cast<int>(cmap_.Height()) != expected.logicalHeight) {
        if (error != nullptr) *error = "School Days BackLog CMAP logical dimensions drifted";
        Close();
        return false;
    }

    store_ = &store;
    rawCountSnapshot_ = store.Size();
    currentRawIndex_ = rawCountSnapshot_ == 0u
                           ? -1
                           : static_cast<int>(rawCountSnapshot_ - 1u);
    selection_ = -1;
    open_ = true;
    return true;
}

void SchoolDaysBackLogModel::Close() {
    cmap_.Clear();
    store_ = nullptr;
    rawCountSnapshot_ = 0;
    currentRawIndex_ = -1;
    selection_ = -1;
    open_ = false;
}

int SchoolDaysBackLogModel::HitControl(int x, int y) const {
    if (!open_) return -1;
    const int raw = cmap_.Get(x, y);
    return raw >= 1 && raw <= 5 ? raw - 1 : -1;
}

bool SchoolDaysBackLogModel::SetSelectionFromPointer(int x, int y) {
    if (!open_) return false;
    const int hit = HitControl(x, y);
    const bool changed = hit != selection_;
    selection_ = hit;
    return changed;
}

int SchoolDaysBackLogModel::DeltaForSelection(int selection) {
    static constexpr int kDelta[4] = {-3, -1, 1, 3};
    return selection >= 0 && selection < 4 ? kDelta[selection] : 0;
}

bool SchoolDaysBackLogModel::ActivatePointer(int x, int y, Action* out) {
    if (!open_ || out == nullptr) return false;
    *out = {};
    const int hit = HitControl(x, y);
    if (hit < 0 || hit != selection_) return false;

    if (hit == 4) {
        out->kind = ActionKind::Close;
        out->rawIndex = currentRawIndex_;
        return true;
    }

    if (rawCountSnapshot_ == 0u) return false;
    const int last = static_cast<int>(rawCountSnapshot_ - 1u);
    currentRawIndex_ = std::clamp(currentRawIndex_ + DeltaForSelection(hit), 0, last);
    out->kind = ActionKind::CursorChanged;
    out->rawIndex = currentRawIndex_;
    return true;
}

std::vector<SchoolDaysBackLogRenderPlan::TextEntry>
SchoolDaysBackLogRenderPlan::BuildTextEntries(const SchoolDaysBackLogModel& model) {
    std::vector<TextEntry> out;
    if (!model.IsOpen() || model.Store() == nullptr ||
        model.CurrentRawIndex() < 0 || model.RawCountSnapshot() == 0u) {
        return out;
    }

    const SchoolDaysBackLogStore& store = *model.Store();
    const size_t count = std::min(model.RawCountSnapshot(), store.Size());
    const size_t current = static_cast<size_t>(model.CurrentRawIndex());
    out.reserve(count);

    const auto appendIfText = [&](size_t rawIndex, Phase phase) {
        const SchoolDaysBackLogStore::Entry* e = store.At(rawIndex);
        if (e != nullptr && e->IsLogMessage())
            out.push_back(
                {rawIndex, e->historyIndex, phase, e->character, e->text});
    };

    if (current < count) appendIfText(current, Phase::Current);
    for (size_t raw = current; raw > 0u; --raw)
        appendIfText(raw - 1u, Phase::Previous);
    for (size_t raw = current + 1u; raw < count; ++raw)
        appendIfText(raw, Phase::Following);
    return out;
}

SchoolDaysBackLogRenderPlan::Layout SchoolDaysBackLogRenderPlan::LayoutFor(
    SchoolDaysBackLogModel::Variant variant) {
    using Variant = SchoolDaysBackLogModel::Variant;
    switch (variant) {
    case Variant::Standard: return {1.0f, 75.0f, 800, 600};
    case Variant::Wide: return {1.0f, 0.0f, 800, 450};
    case Variant::WideFull: return {1.6f, 0.0f, 1280, 720};
    case Variant::WideNote: return {1.28f, 0.0f, 1024, 576};
    }
    return {1.0f, 75.0f, 800, 600};
}

SchoolDaysBackLogRenderPlan::Record SchoolDaysBackLogRenderPlan::ControlRecord(
    SchoolDaysBackLogModel::Orientation orientation, int control) {
    if (control < 0 || control >= 5) return {};
    const auto& records = orientation == SchoolDaysBackLogModel::Orientation::Vertical
                              ? kVerticalChip
                              : kHorizonChip;
    return records[static_cast<size_t>(control)];
}

std::vector<SchoolDaysBackLogRenderPlan::Sprite>
SchoolDaysBackLogRenderPlan::BuildSkin(const SchoolDaysBackLogModel& model) {
    std::vector<Sprite> out;
    if (!model.IsOpen()) return out;

    const Layout layout = LayoutFor(model.CurrentVariant());
    Sprite base;
    base.texture = Texture::Base;
    base.role = Role::Base;
    // Shared menuBase setup (0x1000AEB0 -> 0x1000B070) stores the already
    // scaled base W/H and then applies a final half-pixel border.  Unlike CHIP
    // records, that -0.5 bias itself is not multiplied by the profile scale.
    base.destination = {-0.5f,
                        layout.yOffset - 0.5f,
                        800.0f * layout.scale + 1.0f,
                        450.0f * layout.scale + 1.0f};
    out.push_back(base);

    const int selection = model.Selection();
    if (selection >= 0 && selection < 5) {
        const Record record = ControlRecord(model.CurrentOrientation(), selection);
        Sprite selected;
        selected.texture = Texture::Chip;
        selected.role = Role::SelectedControl;
        selected.control = selection;
        selected.hasSource = true;
        selected.source = record.source;
        selected.destination = TransformChip(record.destination, layout);
        out.push_back(selected);
    }
    return out;
}

int SchoolDaysBackLogRenderPlan::RetailEnglishGlyphAdjustment(uint16_t codepoint) {
    switch (codepoint) {
    case u'j':
    case u'i':
    case u'l':
    case u'I': return -7;
    case u'm':
    case u'w': return 8;
    case u'M':
    case u'W':
    case u'Q': return 11;
    default:
        return codepoint >= u'A' && codepoint <= u'Z' ? 4 : 0;
    }
}

int SchoolDaysBackLogRenderPlan::RetailEnglishGlyphAdvance(uint16_t codepoint) {
    return 23 + RetailEnglishGlyphAdjustment(codepoint);
}

std::vector<std::u16string> SchoolDaysBackLogRenderPlan::WrapRetailEnglish(
    const std::u16string& text, bool* unsupportedMarkup) {
    if (unsupportedMarkup != nullptr) *unsupportedMarkup = false;
    std::vector<std::u16string> lines;
    std::u16string line;
    line.reserve(70u);

    for (size_t i = 0u; i < text.size() && lines.size() < kRetailWrapLines;) {
        const uint16_t codepoint = static_cast<uint16_t>(text[i]);
        if (codepoint == u'\\' && i + 1u < text.size() && text[i + 1u] == u'n') {
            i += 2u;
            continue;
        }
        if (IsUnsupportedRetailMarkup(codepoint)) {
            if (unsupportedMarkup != nullptr) *unsupportedMarkup = true;
            ++i;
            continue;
        }

        bool wrap = false;
        if (i > 0u && text[i - 1u] == u' ') {
            size_t wordLength = 0u;
            for (size_t j = i; j < text.size() && text[j] != u' '; ++j) {
                ++wordLength;
            }
            wrap = line.size() + wordLength >=
                   static_cast<size_t>(kRetailWrapCapacity);
        }
        if (wrap) {
            lines.push_back(line);
            line.clear();
            if (lines.size() >= kRetailWrapLines) break;
        }
        line.push_back(text[i]);
        ++i;
    }
    if (!line.empty() && lines.size() < kRetailWrapLines) lines.push_back(line);
    return lines;
}

int SchoolDaysBackLogRenderPlan::MeasureRetailEnglish(
    const SchoolDaysBackLogStore::Entry& entry, bool* unsupportedMarkup) {
    bool unsupported = false;
    const auto lines = WrapRetailEnglish(entry.text, &unsupported);
    if (unsupportedMarkup != nullptr) *unsupportedMarkup = unsupported;
    const int rows = static_cast<int>(lines.size()) +
                     (entry.character.empty() ? 0 : 1);
    return 126 + 36 * rows;
}

SchoolDaysBackLogRenderPlan::NativeTextPlan
SchoolDaysBackLogRenderPlan::BuildRetailHorizonEnglish(
    const SchoolDaysBackLogModel& model) {
    NativeTextPlan plan;
    if (!model.IsOpen() || model.Store() == nullptr ||
        model.CurrentOrientation() != SchoolDaysBackLogModel::Orientation::Horizon ||
        model.CurrentRawIndex() < 0 || model.RawCountSnapshot() == 0u) {
        return plan;
    }

    const SchoolDaysBackLogStore& store = *model.Store();
    const size_t count = std::min(model.RawCountSnapshot(), store.Size());
    const int current = model.CurrentRawIndex();
    if (current < 0 || static_cast<size_t>(current) >= count) return plan;

    auto buildBlock = [&](int rawIndex, Phase phase, int top) {
        NativeTextBlock block;
        block.rawIndex = static_cast<size_t>(rawIndex);
        block.phase = phase;
        block.top = top;
        const SchoolDaysBackLogStore::Entry* entry = store.At(block.rawIndex);
        if (entry == nullptr || !entry->IsLogMessage()) return block;

        bool unsupported = false;
        const auto lines = WrapRetailEnglish(entry->text, &unsupported);
        plan.unsupportedMarkup = plan.unsupportedMarkup || unsupported;
        block.extent = 126 + 36 * (static_cast<int>(lines.size()) +
                                   (entry->character.empty() ? 0 : 1));

        int axis = top + 21;
        auto appendRun = [&](const std::u16string& text, int startX, int y) {
            int x = startX;
            for (char16_t ch : text) {
                const uint16_t codepoint = static_cast<uint16_t>(ch);
                block.glyphs.push_back({codepoint, x, y});
                x += RetailEnglishGlyphAdvance(codepoint);
            }
        };

        const auto axisVisible = [](int value) {
            return value >= 90 && value <= 750 && value >= 6 &&
                   value < kRetailEnglishLimit;
        };
        if (!entry->character.empty()) {
            if (axisVisible(axis)) {
                const size_t speakerUnits =
                    std::min(entry->character.size(),
                             static_cast<size_t>(kRetailWrapCapacity));
                appendRun(entry->character.substr(0u, speakerUnits), 0, axis - 6);
            }
            axis += 57;
        }
        for (const auto& line : lines) {
            if (axisVisible(axis)) appendRun(line, 110, axis - 6);
            axis += 57;
        }
        return block;
    };

    const SchoolDaysBackLogStore::Entry* currentEntry =
        store.At(static_cast<size_t>(current));
    if (currentEntry == nullptr || !currentEntry->IsLogMessage()) return plan;
    bool unsupported = false;
    const int currentExtent = MeasureRetailEnglish(*currentEntry, &unsupported);
    plan.unsupportedMarkup = unsupported;
    int top = kRetailCenter - currentExtent / 2;
    int bottom = top + currentExtent;
    plan.blocks.push_back(buildBlock(current, Phase::Current, top));

    for (int raw = current - 1; raw >= 0 && top >= 0; --raw) {
        const SchoolDaysBackLogStore::Entry* entry = store.At(static_cast<size_t>(raw));
        if (entry == nullptr || !entry->IsLogMessage()) continue;
        const int extent = MeasureRetailEnglish(*entry, &unsupported);
        plan.unsupportedMarkup = plan.unsupportedMarkup || unsupported;
        top -= extent;
        plan.blocks.push_back(buildBlock(raw, Phase::Previous, top));
    }
    for (int raw = current + 1;
         raw < static_cast<int>(count) && bottom <= kRetailEnglishLimit - 3;
         ++raw) {
        const SchoolDaysBackLogStore::Entry* entry = store.At(static_cast<size_t>(raw));
        if (entry == nullptr || !entry->IsLogMessage()) continue;
        plan.blocks.push_back(buildBlock(raw, Phase::Following, bottom));
        const int extent = MeasureRetailEnglish(*entry, &unsupported);
        plan.unsupportedMarkup = plan.unsupportedMarkup || unsupported;
        bottom += extent;
    }
    plan.valid = !plan.unsupportedMarkup;
    return plan;
}

SchoolDaysBackLogRenderPlan::Rect SchoolDaysBackLogRenderPlan::TextSurfaceSource() {
    return {0.0f, 0.0f, 1280.0f, 720.0f};
}

SchoolDaysBackLogRenderPlan::Rect SchoolDaysBackLogRenderPlan::TextSurfaceDestination(
    SchoolDaysBackLogModel::Variant variant) {
    const Layout layout = LayoutFor(variant);
    return {-0.5f * layout.scale,
            (layout.yOffset - 0.5f) * layout.scale,
            801.0f * layout.scale,
            451.0f * layout.scale};
}

} // namespace Kotonoha
