#include <Kotonoha/routing/SchoolDaysReplayRenderPlan.hpp>

#include <array>
#include <cstdio>

namespace Kotonoha {
namespace {

using Plan = SchoolDaysReplayRenderPlan;

struct Record {
    Plan::Rect source;
    Plan::Rect destination;
};

constexpr Record R(float sx, float sy, float sw, float sh,
                   float dx, float dy, float dw, float dh) {
    return {{sx, sy, sw, sh}, {dx, dy, dw, dh}};
}

Plan::Rect Transform(const Plan::Rect& r, const Plan::Layout& layout) {
    return {(r.x - 0.5f) * layout.scale,
            (r.y - 0.5f + layout.yOffset) * layout.scale,
            (r.w + 1.0f) * layout.scale,
            (r.h + 1.0f) * layout.scale};
}

Plan::Sprite Base(const Plan::Layout& layout) {
    Plan::Sprite s;
    s.texture = Plan::Texture::Base;
    s.role = Plan::Role::Base;
    s.destination = {-0.5f * layout.scale,
                     layout.yOffset * layout.scale,
                     800.0f * layout.scale,
                     450.0f * layout.scale};
    return s;
}

Plan::Sprite FromRecord(Plan::Texture texture, Plan::Role role, int index,
                        const Record& r, const Plan::Layout& layout) {
    Plan::Sprite s;
    s.texture = texture;
    s.role = role;
    s.index = index;
    s.hasSource = true;
    s.source = r.source;
    s.destination = Transform(r.destination, layout);
    return s;
}

constexpr std::array<Record, 31> kHSceneRecords{{
    R(1,1,194,38,10,54,194,38), R(196,1,194,38,204,54,194,38),
    R(391,1,98,42,702,6,98,42), R(1,40,194,38,10,54,194,38),
    R(196,40,194,38,204,54,194,38), R(1,79,194,38,10,54,194,38),
    R(196,79,194,38,204,54,194,38), R(391,44,38,25,639,60,38,25),
    R(431,44,38,25,677,60,38,25), R(471,44,38,25,715,60,38,25),
    R(511,44,38,25,753,60,38,25), R(391,70,39,25,638,60,39,25),
    R(431,70,38,25,677,60,38,25), R(471,70,38,25,715,60,38,25),
    R(511,70,38,25,753,60,38,25), R(391,96,39,25,638,60,39,25),
    R(431,96,38,25,677,60,38,25), R(471,96,38,25,715,60,38,25),
    R(511,96,38,25,753,60,38,25), R(1,122,164,94,55,107,164,94),
    R(166,122,164,94,230,107,164,94), R(331,122,164,94,406,107,164,94),
    R(496,122,164,94,581,107,164,94), R(1,217,164,94,55,213,164,94),
    R(166,217,164,94,230,213,164,94), R(331,217,164,94,406,213,164,94),
    R(496,217,164,94,581,213,164,94), R(1,312,164,94,55,318,164,94),
    R(166,312,164,94,230,318,164,94), R(331,312,164,94,406,318,164,94),
    R(496,312,164,94,581,318,164,94),
}};

std::array<Record, 47> BuildPlayDataRecords() {
    std::array<Record, 47> out{};
    for (int i = 0; i < 7; ++i) out[static_cast<size_t>(i)] = kHSceneRecords[static_cast<size_t>(i)];
    const int sx[10] = {391,431,471,511,551,591,631,671,711,751};
    const int dx[10] = {410,449,487,525,563,601,639,677,715,753};
    const int widths[10] = {39,38,38,38,38,38,38,38,38,38};
    for (int i = 0; i < 10; ++i) {
        out[7+i]  = R(static_cast<float>(sx[i]),44,static_cast<float>(widths[i]),25,
                      static_cast<float>(dx[i]),60,static_cast<float>(widths[i]),25);
        out[17+i] = R(static_cast<float>(sx[i]),70,static_cast<float>(widths[i]),25,
                      static_cast<float>(dx[i]),60,static_cast<float>(widths[i]),25);
        out[27+i] = R(static_cast<float>(sx[i]),96,static_cast<float>(widths[i]),25,
                      static_cast<float>(dx[i]),60,static_cast<float>(widths[i]),25);
        out[37+i] = R(1,static_cast<float>(122+32*i),799,31,
                      0,static_cast<float>(96+33*i),799,31);
    }
    return out;
}

const std::array<Record, 47> kPlayDataRecords = BuildPlayDataRecords();

std::array<Record, 24> BuildThumbnailRecords() {
    std::array<Record, 24> out{};
    const int sx[4] = {1,166,331,496};
    const int dx[4] = {55,230,406,581};
    const int dy[3] = {107,213,318};
    const int colorY[3] = {1,96,191};
    const int sepiaY[3] = {286,381,476};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            const int i = row * 4 + col;
            out[i] = R(static_cast<float>(sx[col]),static_cast<float>(colorY[row]),164,94,
                       static_cast<float>(dx[col]),static_cast<float>(dy[row]),164,94);
            out[12+i] = R(static_cast<float>(sx[col]),static_cast<float>(sepiaY[row]),164,94,
                          static_cast<float>(dx[col]),static_cast<float>(dy[row]),164,94);
        }
    }
    return out;
}

const std::array<Record, 24> kThumbnailRecords = BuildThumbnailRecords();

constexpr std::array<Record, 5> kPop2Records{{
    R(1,1,164,35,232,337,164,35), R(166,1,164,35,405,337,164,35),
    R(1,37,160,90,135,165,160,90), R(162,37,160,90,135,165,160,90),
    R(1,128,160,90,135,165,160,90),
}};

constexpr std::array<Record, 5> kPop4Records{{
    R(1,1,165,35,68,337,165,35), R(167,1,165,35,235,337,165,35),
    R(1,37,165,35,401,337,165,35), R(167,37,165,35,568,337,165,35),
    R(1,73,160,90,135,165,160,90),
}};

} // namespace

SchoolDaysReplayRenderPlan::Layout SchoolDaysReplayRenderPlan::LayoutFor(
    SchoolDaysReplayModel::Variant variant) {
    switch (variant) {
    case SchoolDaysReplayModel::Variant::Standard: return {1.0f,75.0f,800,600};
    case SchoolDaysReplayModel::Variant::Wide: return {1.0f,0.0f,800,450};
    case SchoolDaysReplayModel::Variant::WideFull: return {1.6f,0.0f,1280,720};
    case SchoolDaysReplayModel::Variant::WideNote: return {1.28f,0.0f,1024,576};
    }
    return {1.0f,75.0f,800,600};
}

std::vector<SchoolDaysReplayRenderPlan::MetadataRect>
SchoolDaysReplayRenderPlan::MetadataLayout(
    SchoolDaysReplayModel::Variant variant) {
    const Layout layout = LayoutFor(variant);
    std::vector<MetadataRect> rows;
    rows.reserve(10u);
    auto scaleText = [&](float x, float y, float w, float h) -> Rect {
        return {x * layout.scale,
                (y + layout.yOffset) * layout.scale,
                w * layout.scale,
                h * layout.scale};
    };
    for (int row = 0; row < 10; ++row) {
        const float rowY = 48.0f * static_cast<float>(row);
        const float destY = 100.5f + 33.0f * static_cast<float>(row);
        MetadataRect item;
        item.row = row;
        item.dateSource = {0.0f, 2.0f + rowY, 548.0f, 48.0f};
        item.storySource = {1024.0f, 2.0f + rowY, 548.0f, 48.0f};
        item.commentSource = {0.0f, 514.0f + rowY, 986.0f, 48.0f};
        // SceneView 0x1001B6D0/0x1001C850 destinations. Unlike SaveLoad,
        // Replay does not apply the English proportional-x adjustment here.
        item.date = scaleText(1.0f, destY, 252.0f, 24.0f);
        item.story = scaleText(262.5f, destY, 252.0f, 24.0f);
        item.comment = scaleText(330.0f, destY, 494.0f, 24.0f);
        rows.push_back(item);
    }
    return rows;
}

std::string SchoolDaysReplayRenderPlan::ThumbnailRelativePath(int page) {
    if (page < 0 || page > 3) return {};
    char path[64]{};
    std::snprintf(path, sizeof(path), "System/Replay/Replay_Thm%02d.png", page + 1);
    return path;
}

std::vector<SchoolDaysReplayRenderPlan::Sprite>
SchoolDaysReplayRenderPlan::Build(const SchoolDaysReplayModel& model) {
    std::vector<Sprite> out;
    if (!model.IsOpen()) return out;
    const Layout layout = LayoutFor(model.CurrentVariant());
    out.reserve(32u);
    out.push_back(Base(layout));

    const int page = model.Page();
    const int selection = model.Selection();
    const bool hScene = model.CurrentView() == SchoolDaysReplayModel::View::HScene;

    if (hScene) {
        const bool currentPageHover = selection == 3 + page;
        const int currentPageRecord = (currentPageHover ? 15 : 11) + page;
        out.push_back(FromRecord(Texture::Chip,
                                 currentPageHover ? Role::CurrentPageHover
                                                  : Role::CurrentPage,
                                 page, kHSceneRecords[currentPageRecord], layout));

        for (int slot = 0; slot < 12; ++slot) {
            const int absolute = page * 12 + slot;
            if (absolute >= 41) continue;
            out.push_back(FromRecord(Texture::Chip, Role::LockedSlot, absolute,
                                     kHSceneRecords[19 + slot], layout));
        }

        if (selection >= 3 && selection <= 6 && selection != 3 + page) {
            const int hoveredPage = selection - 3;
            out.push_back(FromRecord(Texture::Chip, Role::PageHover, hoveredPage,
                                     kHSceneRecords[7 + hoveredPage], layout));
        }
    } else {
        const bool currentPageHover = selection == 13 + page;
        const int currentPageRecord = (currentPageHover ? 27 : 17) + page;
        out.push_back(FromRecord(Texture::Chip,
                                 currentPageHover ? Role::CurrentPageHover
                                                  : Role::CurrentPage,
                                 page, kPlayDataRecords[currentPageRecord], layout));
        if (selection >= 13 && selection <= 22 && selection != 13 + page) {
            const int hoveredPage = selection - 13;
            out.push_back(FromRecord(Texture::Chip, Role::PageHover, hoveredPage,
                                     kPlayDataRecords[7 + hoveredPage], layout));
        }
    }

    if (selection == 0 || selection == 1) {
        const Record& r = hScene ? kHSceneRecords[selection]
                                 : kPlayDataRecords[selection];
        out.push_back(FromRecord(Texture::Chip, Role::TabHover, selection, r, layout));
    } else if (selection == 2) {
        const Record& r = hScene ? kHSceneRecords[2] : kPlayDataRecords[2];
        out.push_back(FromRecord(Texture::Chip, Role::CloseHover, 2, r, layout));
    }

    const int activeTab = hScene ? 0 : 1;
    const int tabRecord = (selection == activeTab ? 5 : 3) + activeTab;
    const Record& tab = hScene ? kHSceneRecords[tabRecord]
                               : kPlayDataRecords[tabRecord];
    out.push_back(FromRecord(Texture::Chip,
                             selection == activeTab ? Role::CurrentTabHover
                                                    : Role::CurrentTab,
                             activeTab, tab, layout));

    if (hScene) {
        for (int slot = 0; slot < 12; ++slot) {
            const int absolute = page * 12 + slot;
            if (absolute >= 41 || !model.IsHSceneAvailable(absolute)) continue;
            out.push_back(FromRecord(Texture::Thumbnail, Role::ThumbnailSepia,
                                     absolute, kThumbnailRecords[12 + slot], layout));
        }
        if (selection >= 7 && selection <= 18) {
            const int slot = selection - 7;
            const int absolute = page * 12 + slot;
            if (absolute < 41 && model.IsHSceneAvailable(absolute)) {
                out.push_back(FromRecord(Texture::Thumbnail, Role::ThumbnailHover,
                                         absolute, kThumbnailRecords[slot], layout));
            }
        }
    } else {
        int row = -1;
        if (selection >= 3 && selection <= 12) row = selection - 3;
        else if (selection >= 23 && selection <= 32) row = selection - 23;
        if (row >= 0) {
            const int absolute = page * 10 + row;
            if (model.IsPlayDataAvailable(absolute)) {
                out.push_back(FromRecord(Texture::Chip, Role::PlayDataRowHover,
                                         absolute, kPlayDataRecords[37 + row], layout));
            }
        }
    }
    return out;
}

std::vector<SchoolDaysReplayRenderPlan::Sprite>
SchoolDaysReplayRenderPlan::BuildSceneCheck(
    const SchoolDaysReplayModel& model,
    const SceneCheckVisualState& visualState) {
    std::vector<Sprite> out;
    if (!model.IsOpen() || !model.SceneCheckOpen()) return out;
    const Layout layout = LayoutFor(model.CurrentVariant());
    out.push_back(Base(layout));

    const int count = model.SceneCheckChoiceCount();
    const int hovered = model.SceneCheckSelection();
    const bool pop4 = count == 4;
    const auto& records = pop4 ? kPop4Records : kPop2Records;
    if (hovered >= 0 && hovered < count &&
        visualState.enabled[static_cast<size_t>(hovered)]) {
        out.push_back(FromRecord(Texture::Chip, Role::SceneCheckChoiceHover,
                                 hovered, records[static_cast<size_t>(hovered)], layout));
    }

    int preview = visualState.previewRecord >= 0
                      ? visualState.previewRecord
                      : model.SceneCheckPreviewRecord();
    if (preview >= (pop4 ? 4 : 2) && preview <= 4) {
        out.push_back(FromRecord(Texture::Chip, Role::SceneCheckPreview, preview,
                                 records[static_cast<size_t>(preview)], layout));
    }
    return out;
}

} // namespace Kotonoha
