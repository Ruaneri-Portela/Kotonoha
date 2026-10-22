#include <Kotonoha/routing/SchoolDaysBackLog.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

using Store = Kotonoha::SchoolDaysBackLogStore;
using Model = Kotonoha::SchoolDaysBackLogModel;
using Plan = Kotonoha::SchoolDaysBackLogRenderPlan;

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << message << '\n';
    std::exit(2);
}
void Check(bool value, const char* message) { if (!value) Fail(message); }

void Put32(std::ofstream& out, int value) {
    const unsigned int u = static_cast<unsigned int>(value);
    const unsigned char bytes[4] = {
        static_cast<unsigned char>(u), static_cast<unsigned char>(u >> 8u),
        static_cast<unsigned char>(u >> 16u), static_cast<unsigned char>(u >> 24u)};
    out.write(reinterpret_cast<const char*>(bytes), 4);
}

void WriteCmap(const std::filesystem::path& root, Model::Orientation orientation,
               Model::Variant variant) {
    const Model::Layout layout = Model::LayoutFor(orientation, variant);
    std::vector<unsigned char> pixels(
        static_cast<size_t>(layout.logicalWidth * layout.logicalHeight), 0u);
    for (const Model::ControlGeometry& control : layout.controls) {
        for (int y = control.bounds.y; y < control.bounds.y + control.bounds.h; ++y) {
            for (int x = control.bounds.x; x < control.bounds.x + control.bounds.w; ++x) {
                pixels[static_cast<size_t>(y * layout.logicalWidth + x)] =
                    static_cast<unsigned char>(control.rawRegion);
            }
        }
    }
    const auto path = root / Model::CmapRelativePath(orientation, variant);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    Put32(out, layout.logicalWidth);
    Put32(out, layout.logicalHeight);
    out.write(reinterpret_cast<const char*>(pixels.data()),
              static_cast<std::streamsize>(pixels.size()));
}

struct ExpectedLayout {
    Model::Orientation orientation;
    Model::Variant variant;
    int width;
    int height;
    std::array<Model::Rect, 5> rects;
};

constexpr ExpectedLayout kLayouts[] = {
    {Model::Orientation::Vertical, Model::Variant::Standard, 800,600,
     {{{10,484,90,34},{100,484,89,34},{611,484,89,34},{700,484,90,34},{702,81,98,42}}}},
    {Model::Orientation::Vertical, Model::Variant::Wide, 800,450,
     {{{10,409,90,34},{100,409,89,34},{611,409,89,34},{700,409,90,34},{702,6,98,42}}}},
    {Model::Orientation::Vertical, Model::Variant::WideFull, 1280,720,
     {{{16,654,144,55},{160,654,142,55},{978,654,142,55},{1120,654,144,55},{1123,10,157,67}}}},
    {Model::Orientation::Vertical, Model::Variant::WideNote, 1024,576,
     {{{13,523,115,44},{128,523,114,44},{782,523,114,44},{896,523,115,44},{899,8,125,53}}}},
    {Model::Orientation::Horizon, Model::Variant::Standard, 800,600,
     {{{758,138,36,92},{758,230,36,89},{758,319,36,89},{758,408,36,92},{702,81,98,42}}}},
    {Model::Orientation::Horizon, Model::Variant::Wide, 800,450,
     {{{758,63,36,92},{758,155,36,89},{758,244,36,89},{758,333,36,92},{702,6,98,42}}}},
    {Model::Orientation::Horizon, Model::Variant::WideFull, 1280,720,
     {{{1213,101,57,147},{1213,248,57,142},{1213,390,57,143},{1213,533,57,147},{1123,10,157,67}}}},
    {Model::Orientation::Horizon, Model::Variant::WideNote, 1024,576,
     {{{970,81,46,117},{970,198,46,114},{970,312,46,114},{970,426,46,118},{898,8,126,54}}}},
};

bool Same(const Model::Rect& a, const Model::Rect& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

void Near(float value, float expected, const char* message) {
    if (std::fabs(value - expected) > 0.01f) Fail(message);
}

void CheckPlanRect(const Plan::Rect& got, float x, float y, float w, float h,
                   const char* message) {
    if (std::fabs(got.x - x) > 0.01f || std::fabs(got.y - y) > 0.01f ||
        std::fabs(got.w - w) > 0.01f || std::fabs(got.h - h) > 0.01f) {
        Fail(message);
    }
}

void CheckLayouts() {
    for (const ExpectedLayout& expected : kLayouts) {
        const Model::Layout got = Model::LayoutFor(expected.orientation, expected.variant);
        Check(got.logicalWidth == expected.width && got.logicalHeight == expected.height,
              "BackLog logical profile dimensions drifted");
        for (int i = 0; i < 5; ++i) {
            Check(got.controls[static_cast<size_t>(i)].rawRegion == i + 1,
                  "BackLog CMAP raw control id drifted");
            Check(Same(got.controls[static_cast<size_t>(i)].bounds,
                       expected.rects[static_cast<size_t>(i)]),
                  "BackLog CMAP control geometry drifted");
        }
    }
    Check(Model::CmapRelativePath(Model::Orientation::Vertical, Model::Variant::WideNote) ==
              "System/BackLog/BackLog_Vertical_Wide_Note.cmap",
          "BackLog Vertical WideNote CMAP path drifted");
    Check(Model::CmapRelativePath(Model::Orientation::Horizon, Model::Variant::WideFull) ==
              "System/BackLog/BackLog_Horizon_Wide_Full.cmap",
          "BackLog Horizon WideFull CMAP path drifted");
    Check(Model::BaseRelativePath(Model::Orientation::Vertical) ==
              "System/BackLog/BackLog_Vertical.png" &&
          Model::ChipRelativePath(Model::Orientation::Horizon) ==
              "System/BackLog/BackLog_Horizon_Chip.png",
          "BackLog base/chip resource path drifted");
}

void CheckNativeSkinRecords(const std::filesystem::path& root) {
    constexpr float kVertical[5][6] = {
        {10,409,90,34,1,1}, {100,409,89,34,92,1},
        {611,409,89,34,1,36}, {700,409,90,34,91,36},
        {702,6,98,42,1,71},
    };
    constexpr float kHorizon[5][6] = {
        {758,63,36,92,1,1}, {758,155,36,89,38,1},
        {758,244,36,89,75,1}, {758,333,36,92,112,1},
        {702,6,98,42,149,1},
    };

    for (int orientationIndex = 0; orientationIndex < 2; ++orientationIndex) {
        const auto orientation = orientationIndex == 0
                                     ? Model::Orientation::Vertical
                                     : Model::Orientation::Horizon;
        const auto& expected = orientationIndex == 0 ? kVertical : kHorizon;
        for (int control = 0; control < 5; ++control) {
            const auto record = Plan::ControlRecord(orientation, control);
            CheckPlanRect(record.destination, expected[control][0], expected[control][1],
                          expected[control][2], expected[control][3],
                          "BackLog native CHIP destination record drifted");
            CheckPlanRect(record.source, expected[control][4], expected[control][5],
                          expected[control][2], expected[control][3],
                          "BackLog native CHIP source record drifted");
        }
    }

    struct Profile {
        Model::Variant variant;
        float scale;
        float yOffset;
        int width;
        int height;
    };
    constexpr Profile profiles[] = {
        {Model::Variant::Standard, 1.0f, 75.0f, 800, 600},
        {Model::Variant::Wide, 1.0f, 0.0f, 800, 450},
        {Model::Variant::WideFull, 1.6f, 0.0f, 1280, 720},
        {Model::Variant::WideNote, 1.28f, 0.0f, 1024, 576},
    };
    for (const Profile& expected : profiles) {
        const auto layout = Plan::LayoutFor(expected.variant);
        Near(layout.scale, expected.scale, "BackLog skin profile scale drifted");
        Near(layout.yOffset, expected.yOffset, "BackLog skin profile yOffset drifted");
        Check(layout.logicalWidth == expected.width &&
                  layout.logicalHeight == expected.height,
              "BackLog skin logical canvas drifted");
    }

    Store store;
    store.AppendLogMessage(0, u"Speaker", u"skin");
    std::string error;
    for (const Profile& profile : profiles) {
        Model model;
        Check(model.Open(root.string(), store, Model::Orientation::Horizon,
                         profile.variant, &error), error.c_str());
        auto skin = Plan::BuildSkin(model);
        Check(skin.size() == 1u && skin[0].role == Plan::Role::Base &&
                  skin[0].texture == Plan::Texture::Base && !skin[0].hasSource,
              "BackLog idle skin should contain only baked base art");
        CheckPlanRect(skin[0].destination, -0.5f,
                      profile.yOffset - 0.5f, 800.0f * profile.scale + 1.0f,
                      450.0f * profile.scale + 1.0f,
                      "BackLog base logical destination drifted");

        const Model::Layout hitLayout = Model::LayoutFor(Model::Orientation::Horizon,
                                                          profile.variant);
        const Model::Rect hit = hitLayout.controls[0].bounds;
        Check(model.SetSelectionFromPointer(hit.x, hit.y),
              "BackLog skin selection setup failed");
        skin = Plan::BuildSkin(model);
        Check(skin.size() == 2u && skin[1].role == Plan::Role::SelectedControl &&
                  skin[1].texture == Plan::Texture::Chip && skin[1].control == 0 &&
                  skin[1].hasSource,
              "BackLog selected CHIP pass drifted");
        CheckPlanRect(skin[1].source, 1, 1, 36, 92,
                      "BackLog selected CHIP source drifted");
        CheckPlanRect(skin[1].destination,
                      (758.0f - 0.5f) * profile.scale,
                      (63.0f - 0.5f + profile.yOffset) * profile.scale,
                      37.0f * profile.scale, 93.0f * profile.scale,
                      "BackLog selected CHIP half-pixel expansion drifted");
    }
}

void CheckStoreAndRender(const std::filesystem::path& root) {
    Store store;
    Check(store.AppendLogMessage(10, u"A", u"first") == 0u,
          "first type2 raw index drifted");
    Check(store.AppendLogMessage(11, u"B", u"second") == 1u,
          "second type2 raw index drifted");
    Check(store.Size() == 2u && store.At(0)->historyIndex == 10 &&
              store.At(0)->character == u"A" &&
              store.At(0)->text == u"first",
          "type2 history projection payload drifted");

    Model model;
    std::string error;
    Check(model.Open(root.string(), store, Model::Orientation::Vertical,
                     Model::Variant::Standard, &error), error.c_str());
    Check(model.RawCountSnapshot() == 2u && model.CurrentRawIndex() == 1,
          "BackLog open did not seed current raw index from tail");

    auto plan = Plan::BuildTextEntries(model);
    Check(plan.size() == 2u && plan[0].rawIndex == 1u &&
              plan[0].phase == Plan::Phase::Current &&
              plan[0].character == u"B" && plan[0].text == u"second" &&
              plan[1].rawIndex == 0u && plan[1].phase == Plan::Phase::Previous,
          "BackLog type2 traversal order drifted");
    Check(!Plan::RequiresNativeTextMeasurement(),
          "BackLog native text measurement should be closed");

    const auto layout = Model::LayoutFor(Model::Orientation::Vertical,
                                         Model::Variant::Standard);
    const auto center = [&](int control) {
        const Model::Rect r = layout.controls[static_cast<size_t>(control)].bounds;
        return std::array<int,2>{r.x + r.w / 2, r.y + r.h / 2};
    };
    const auto minus1 = center(1);
    const auto plus1 = center(2);
    Model::Action action;

    Check(model.SetSelectionFromPointer(minus1[0], minus1[1]) && model.Selection() == 1,
          "BackLog pointer selection normalization failed");
    Check(!model.ActivatePointer(plus1[0], plus1[1], &action) &&
              model.CurrentRawIndex() == 1,
          "BackLog stale click bypassed hit==stored-selection gate");
    Check(model.ActivatePointer(minus1[0], minus1[1], &action) &&
              action.kind == Model::ActionKind::CursorChanged &&
              model.CurrentRawIndex() == 0,
          "BackLog -1 raw navigation failed");
    plan = Plan::BuildTextEntries(model);
    Check(plan.size() == 2u && plan[0].rawIndex == 0u &&
              plan[0].phase == Plan::Phase::Current &&
              plan[1].rawIndex == 1u && plan[1].phase == Plan::Phase::Following,
          "BackLog current type2 render role failed");

    model.SetSelectionFromPointer(plus1[0], plus1[1]);
    Check(model.ActivatePointer(plus1[0], plus1[1], &action) &&
              model.CurrentRawIndex() == 1,
          "BackLog +1 raw navigation failed");
    model.SetSelectionFromPointer(minus1[0], minus1[1]);
    Check(model.ActivatePointer(minus1[0], minus1[1], &action) &&
              model.CurrentRawIndex() == 0,
          "BackLog -1 restore after +1 failed");

    const auto minus3 = center(0);
    model.SetSelectionFromPointer(minus3[0], minus3[1]);
    Check(model.ActivatePointer(minus3[0], minus3[1], &action) &&
              model.CurrentRawIndex() == 0,
          "BackLog -3 clamp failed");
    const auto plus3 = center(3);
    model.SetSelectionFromPointer(plus3[0], plus3[1]);
    Check(model.ActivatePointer(plus3[0], plus3[1], &action) &&
              model.CurrentRawIndex() == 1,
          "BackLog +3 navigation failed");
    model.SetSelectionFromPointer(plus3[0], plus3[1]);
    Check(model.ActivatePointer(plus3[0], plus3[1], &action) &&
              model.CurrentRawIndex() == 1,
          "BackLog +3 upper clamp failed");

    const auto close = center(4);
    model.SetSelectionFromPointer(close[0], close[1]);
    Check(model.ActivatePointer(close[0], close[1], &action) &&
              action.kind == Model::ActionKind::Close,
          "BackLog Close action failed");

    Store anchored;
    anchored.ObserveScriptChange("A");
    anchored.AppendLogMessage(20, u"A", u"a0");
    anchored.AppendLogMessage(21, u"A", u"a1");
    anchored.ObserveScriptChange("B");
    anchored.AppendLogMessage(22, u"B", u"b0");
    Check(anchored.Size() == 3u, "BackLog anchor setup failed");
    anchored.ObserveScriptChange("A");
    Check(anchored.Empty(),
          "BackLog ScriptChange revisit did not truncate to saved raw prefix");
    anchored.AppendLogMessage(23, u"A", u"a-new");
    anchored.ObserveScriptChange("B");
    Check(anchored.Size() == 1u,
          "BackLog ScriptChange anchor was not prefix-preserving after branch rewrite");
    anchored.ResetTransient();
    anchored.AppendLogMessage(24, u"C", u"fresh");
    anchored.ObserveScriptChange("A");
    Check(anchored.Size() == 1u,
          "BackLog reset retained stale ScriptChange anchors");
    store.ResetTransient();
    Check(store.Empty(), "BackLog transient reset failed");
}

void CheckNativeTextPlan(const std::filesystem::path& root) {
    Check(Plan::RetailEnglishGlyphAdvance(u'i') == 16 &&
              Plan::RetailEnglishGlyphAdvance(u'l') == 16 &&
              Plan::RetailEnglishGlyphAdvance(u'm') == 31 &&
              Plan::RetailEnglishGlyphAdvance(u'w') == 31 &&
              Plan::RetailEnglishGlyphAdvance(u'M') == 34 &&
              Plan::RetailEnglishGlyphAdvance(u'W') == 34 &&
              Plan::RetailEnglishGlyphAdvance(u'Q') == 34 &&
              Plan::RetailEnglishGlyphAdvance(u'A') == 27 &&
              Plan::RetailEnglishGlyphAdvance(u'a') == 23,
          "BackLog retail English glyph advances drifted");

    bool unsupported = false;
    const auto wrapped = Plan::WrapRetailEnglish(
        std::u16string(46u, u'a') + std::u16string(u" b"), &unsupported);
    Check(!unsupported && wrapped.size() == 2u && wrapped[0].size() == 47u &&
              wrapped[0].back() == u' ' && wrapped[1] == u"b",
          "BackLog retail 48-character word wrap drifted");
    const auto escaped = Plan::WrapRetailEnglish(u"a\\nb", &unsupported);
    Check(!unsupported && escaped.size() == 1u && escaped[0] == u"ab",
          "BackLog retail literal \n handling drifted");
    (void)Plan::WrapRetailEnglish(std::u16string(1u, static_cast<char16_t>(0x300A)),
                                  &unsupported);
    Check(unsupported, "BackLog unsupported native markup did not fail closed");

    Store store;
    store.AppendLogMessage(0, u"A", u"Hi");
    store.AppendLogMessage(1, u"A", u"Hi");
    store.AppendLogMessage(2, u"A", u"Hi");
    Model model;
    std::string error;
    Check(model.Open(root.string(), store, Model::Orientation::Horizon,
                     Model::Variant::Wide, &error), error.c_str());
    const auto layout = Model::LayoutFor(Model::Orientation::Horizon,
                                         Model::Variant::Wide);
    const Model::Rect minusOne = layout.controls[1].bounds;
    Model::Action action;
    Check(model.SetSelectionFromPointer(minusOne.x, minusOne.y) &&
              model.ActivatePointer(minusOne.x, minusOne.y, &action) &&
              model.CurrentRawIndex() == 1,
          "BackLog native text plan cursor setup failed");

    const auto plan = Plan::BuildRetailHorizonEnglish(model);
    Check(plan.valid && !plan.unsupportedMarkup && plan.blocks.size() == 3u,
          "BackLog retail native text plan failed");
    Check(plan.blocks[0].phase == Plan::Phase::Current &&
              plan.blocks[0].rawIndex == 1u && plan.blocks[0].top == 281 &&
              plan.blocks[0].extent == 198,
          "BackLog current native text block placement drifted");
    Check(plan.blocks[1].phase == Plan::Phase::Previous &&
              plan.blocks[1].rawIndex == 0u && plan.blocks[1].top == 83 &&
              plan.blocks[2].phase == Plan::Phase::Following &&
              plan.blocks[2].rawIndex == 2u && plan.blocks[2].top == 479,
          "BackLog surrounding native text traversal drifted");
    const auto& glyphs = plan.blocks[0].glyphs;
    Check(glyphs.size() == 3u && glyphs[0].codepoint == u'A' &&
              glyphs[0].x == 0 && glyphs[0].y == 296 &&
              glyphs[1].codepoint == u'H' && glyphs[1].x == 110 &&
              glyphs[1].y == 353 && glyphs[2].codepoint == u'i' &&
              glyphs[2].x == 137 && glyphs[2].y == 353,
          "BackLog native FONTDATA glyph coordinates drifted");

    Check(Plan::MeasureRetailEnglish(*store.At(1)) == 198,
          "BackLog native text extent drifted");

    Store longSpeaker;
    longSpeaker.AppendLogMessage(0, std::u16string(60u, u'A'), u"Hi");
    Model longSpeakerModel;
    Check(longSpeakerModel.Open(root.string(), longSpeaker,
                                Model::Orientation::Horizon,
                                Model::Variant::Wide, &error),
          error.c_str());
    const auto longSpeakerPlan =
        Plan::BuildRetailHorizonEnglish(longSpeakerModel);
    Check(longSpeakerPlan.valid && longSpeakerPlan.blocks.size() == 1u &&
              longSpeakerPlan.blocks[0].glyphs.size() == 50u,
          "BackLog retail speaker 48-unit clamp drifted");
    Check(longSpeakerPlan.blocks[0].glyphs[47].x == 47 * 27 &&
              longSpeakerPlan.blocks[0].glyphs[48].codepoint == u'H' &&
              longSpeakerPlan.blocks[0].glyphs[48].x == 110,
          "BackLog speaker clamp leaked glyphs into the body run");

    CheckPlanRect(Plan::TextSurfaceSource(), 0, 0, 1280, 720,
                  "BackLog FONTDATA source crop drifted");
    CheckPlanRect(Plan::TextSurfaceDestination(Model::Variant::Wide),
                  -0.5f, -0.5f, 801.0f, 451.0f,
                  "BackLog FONTDATA Wide destination drifted");
    CheckPlanRect(Plan::TextSurfaceDestination(Model::Variant::Standard),
                  -0.5f, 74.5f, 801.0f, 451.0f,
                  "BackLog FONTDATA Standard destination drifted");
    CheckPlanRect(Plan::TextSurfaceDestination(Model::Variant::WideFull),
                  -0.8f, -0.8f, 1281.6f, 721.6f,
                  "BackLog FONTDATA WideFull destination drifted");
}

void CheckAllProfilesOpen(const std::filesystem::path& root) {
    Store store;
    store.AppendLogMessage(0, u"Speaker", u"message");
    std::string error;
    for (const ExpectedLayout& expected : kLayouts) {
        Model model;
        Check(model.Open(root.string(), store, expected.orientation, expected.variant, &error),
              error.c_str());
        const Model::Layout layout = Model::LayoutFor(expected.orientation, expected.variant);
        for (int i = 0; i < 5; ++i) {
            const Model::Rect r = layout.controls[static_cast<size_t>(i)].bounds;
            Check(model.HitControl(r.x, r.y) == i &&
                      model.HitControl(r.x + r.w - 1, r.y + r.h - 1) == i,
                  "BackLog CMAP control rectangle is not exact/inclusive");
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "kotonoha-backlog-gate";
    std::error_code ec;
    fs::remove_all(root, ec);
    for (const ExpectedLayout& expected : kLayouts)
        WriteCmap(root, expected.orientation, expected.variant);

    CheckLayouts();
    CheckNativeSkinRecords(root);
    CheckAllProfilesOpen(root);
    CheckStoreAndRender(root);
    CheckNativeTextPlan(root);

    if (argc == 2) {
        Store store;
        store.AppendLogMessage(0, u"Speaker", u"real corpus probe");
        std::string error;
        for (const ExpectedLayout& expected : kLayouts) {
            Model model;
            Check(model.Open(argv[1], store, expected.orientation, expected.variant, &error),
                  error.c_str());
            const Model::Layout layout = Model::LayoutFor(expected.orientation, expected.variant);
            for (int i = 0; i < 5; ++i) {
                const Model::Rect r = layout.controls[static_cast<size_t>(i)].bounds;
                Check(model.HitControl(r.x, r.y) == i &&
                          model.HitControl(r.x + r.w - 1, r.y + r.h - 1) == i,
                      "real BackLog CMAP geometry drifted");
            }
        }
    }

    fs::remove_all(root, ec);
    std::cout << "SCHOOL DAYS BACKLOG CORE GATE PASS\n"
              << "producer_contract_projection=PASS\n"
              << "visible_list_type2_only=PASS\n"
              << "type2_history_index_text=PASS\n"
              << "scriptchange_prefix_rewind=PASS\n"
              << "raw_cursor_minus3_minus1_plus1_plus3=PASS\n"
              << "hit_equals_stored_selection=PASS\n"
              << "orientation_four_logical_profiles=PASS\n"
              << "all_eight_cmap_geometries=PASS\n"
              << "native_base_chip_skin=PASS\n"
              << "five_chip_record_tables=PASS\n"
              << "selected_only_chip_pass=PASS\n"
              << "legacy_half_pixel_transform=PASS\n"
              << "retail_fontdata_metrics=PASS\n"
              << "retail_48char_word_wrap=PASS\n"
              << "retail_speaker_48unit_clamp=PASS\n"
              << "retail_native_text_layout=PASS\n"
              << "retail_text_surface_crop=PASS\n";
    if (argc == 2) std::cout << "real_corpus_all_eight_cmaps=PASS\n";
    return 0;
}
