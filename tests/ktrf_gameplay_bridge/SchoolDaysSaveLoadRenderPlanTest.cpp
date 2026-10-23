#include <Kotonoha/routing/SchoolDaysSaveLoadRenderPlan.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
using Model = Kotonoha::SchoolDaysSaveLoadModel;
using Plan = Kotonoha::SchoolDaysSaveLoadRenderPlan;

[[noreturn]] void Fail(const char* message) {
    std::cerr << "SchoolDaysSaveLoadRenderPlanTest: " << message << '\n';
    std::exit(2);
}
void Check(bool value, const char* message) { if (!value) Fail(message); }
bool Eq(float a, float b) { return std::fabs(a - b) < 0.001f; }

const Plan::Sprite* Find(const std::vector<Plan::Sprite>& sprites, Plan::Role role) {
    for (const auto& sprite : sprites) if (sprite.role == role) return &sprite;
    return nullptr;
}

bool FindRegion(const Kotonoha::SchoolDaysCmap& cmap, int region, int* x, int* y) {
    for (uint32_t yy = 0; yy < cmap.Height(); ++yy) {
        for (uint32_t xx = 0; xx < cmap.Width(); ++xx) {
            if (cmap.Get(static_cast<int>(xx), static_cast<int>(yy)) == region) {
                *x = static_cast<int>(xx);
                *y = static_cast<int>(yy);
                return true;
            }
        }
    }
    return false;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) Fail("usage: SchoolDaysSaveLoadRenderPlanTest <assets-root>");
    const std::string assetsRoot = argv[1];
    const std::string cmapPath =
        assetsRoot + "/System/SAVELOAD/SAVELOAD.CMAP";
    Model model;
    std::string error;
    Check(model.Open(cmapPath, Model::Variant::Standard, false, &error), error.c_str());

    const auto layout = Plan::LayoutFor(Model::Variant::Standard);
    Check(Eq(layout.scale, 1.0f) && Eq(layout.yOffset, 75.0f) &&
              layout.logicalWidth == 800 && layout.logicalHeight == 600,
          "Standard layout mismatch");

    auto sprites = Plan::Build(model);
    const auto* base = Find(sprites, Plan::Role::Base);
    const auto* page = Find(sprites, Plan::Role::CurrentPage);
    const auto* route = Find(sprites, Plan::Role::RouteMap);
    Check(base != nullptr && Eq(base->destination.y, 75.0f),
          "SAVE base yOffset mismatch");
    Check(page != nullptr && page->index == 0 && Eq(page->source.x, 391.0f) &&
              Eq(page->source.y, 70.0f) && Eq(page->destination.x, 409.5f) &&
              Eq(page->destination.y, 134.5f),
          "current-page persistent overlay mismatch");
    Check(route != nullptr && Eq(route->destination.x, 9.5f) &&
              Eq(route->destination.y, 128.5f),
          "RouteMap record mismatch");

    // The native fixed RouteMap CHIP is visual in SAVE too; only its action is
    // LOAD-only. This guards against conflating control availability with draw.
    Check(route->index == 21 && Eq(route->source.x, 1.0f) &&
              Eq(route->source.y, 1.0f) && Eq(route->source.w, 194.0f) &&
              Eq(route->source.h, 38.0f),
          "SAVE RouteMap fixed record disappeared or drifted");

    int x = 0, y = 0;
    Check(FindRegion(model.Cmap(), 1, &x, &y), "row region missing");
    Check(model.SetSelectionFromPointer(x, y), "row hover did not select");
    sprites = Plan::Build(model);
    const auto* row = Find(sprites, Plan::Role::RowHover);
    Check(row != nullptr && row->index == 0 && Eq(row->source.y, 122.0f) &&
              Eq(row->destination.x, 229.5f) && Eq(row->destination.y, 170.5f) &&
              Eq(row->destination.w, 559.0f) && Eq(row->destination.h, 33.0f),
          "row hover record mismatch");

    Check(FindRegion(model.Cmap(), 11, &x, &y), "page0 region missing");
    model.SetSelectionFromPointer(x, y);
    sprites = Plan::Build(model);
    page = Find(sprites, Plan::Role::CurrentPageHover);
    Check(page != nullptr && page->index == 0 && Eq(page->source.y, 96.0f),
          "current-page hover overlay mismatch");

    Check(FindRegion(model.Cmap(), 21, &x, &y), "close region missing");
    model.SetSelectionFromPointer(x, y);
    sprites = Plan::Build(model);
    const auto* close = Find(sprites, Plan::Role::CloseHover);
    Check(close != nullptr && Eq(close->destination.x, 701.5f) &&
              Eq(close->destination.y, 80.5f),
          "close hover record mismatch");

    Check(FindRegion(model.Cmap(), 1, &x, &y), "row region missing for pending");
    model.SetSelectionFromPointer(x, y);
    Model::Output output;
    Check(model.Activate(&output) && model.SavePending(),
          "row did not enter pending state");
    sprites = Plan::Build(model);
    Check(Find(sprites, Plan::Role::RowHover) == nullptr &&
              Find(sprites, Plan::Role::CurrentPage) == nullptr &&
              Find(sprites, Plan::Role::CurrentPageHover) == nullptr &&
              Find(sprites, Plan::Role::CloseHover) == nullptr,
          "pending state did not suppress interactive overlays");

    const auto metadata = Plan::MetadataLayout(Model::Variant::Standard, false);
    Check(metadata.size() == 10 && Eq(metadata[0].primary.x, 1.0f) &&
              Eq(metadata[0].primary.y, 175.5f) &&
              Eq(metadata[0].secondary.x, 262.5f) &&
              Eq(metadata[0].optionalThird.x, 330.0f) &&
              Eq(metadata[9].primary.y, 472.5f) &&
              Eq(metadata[0].primarySource.x, 0.0f) &&
              Eq(metadata[0].primarySource.y, 2.0f) &&
              Eq(metadata[0].primarySource.w, 548.0f) &&
              Eq(metadata[0].secondarySource.x, 1024.0f) &&
              Eq(metadata[0].optionalThirdSource.y, 514.0f) &&
              Eq(metadata[0].optionalThirdSource.w, 986.0f),
          "metadata placement mismatch");
    std::vector<int> englishAdvance(10, 400);
    auto englishMetadata =
        Plan::MetadataLayout(Model::Variant::Standard, true, &englishAdvance);
    Check(Eq(englishMetadata[0].primary.x, 6.0f) &&
              Eq(englishMetadata[0].secondary.x, 277.5f) &&
              Eq(englishMetadata[0].optionalThird.x, 465.5f),
          "English metadata offsets/centering mismatch");
    englishAdvance[0] = 1000;
    englishMetadata =
        Plan::MetadataLayout(Model::Variant::Standard, true, &englishAdvance);
    Check(Eq(englishMetadata[0].optionalThird.x, 330.0f),
          "English metadata left-clamp mismatch");
    Check(Plan::GlyphAdvance(u'i', true) == 17 &&
              Plan::GlyphAdvance(u'j', true) == 17 &&
              Plan::GlyphAdvance(u'l', true) == 17 &&
              Plan::GlyphAdvance(u'm', true) == 32 &&
              Plan::GlyphAdvance(u'w', true) == 32 &&
              Plan::GlyphAdvance(u'M', true) == 35 &&
              Plan::GlyphAdvance(u'Q', true) == 35 &&
              Plan::GlyphAdvance(u'W', true) == 35 &&
              Plan::GlyphAdvance(u'I', true) == 17 &&
              Plan::GlyphAdvance(u'A', true) == 28 &&
              Plan::GlyphAdvance(u'a', true) == 24 &&
              Plan::GlyphAdvance(u'A', false) == 24 &&
              Plan::GlyphAdvance(0x3042u, true) == 45,
          "authenticated SaveLoad glyph advances mismatch");
    Check(Plan::PrimaryCharacterLimit(false) == 20 &&
              Plan::PrimaryCharacterLimit(true) == 21 &&
              Plan::SecondaryCharacterLimit() == 20 &&
              Plan::CommentCharacterLimit(false) == 20 &&
              Plan::CommentCharacterLimit(true) == 40,
          "authenticated SaveLoad text limits mismatch");

    struct VariantCase {
        Model::Variant variant;
        const char* relativePath;
        float scale;
        float yOffset;
        int logicalWidth;
        int logicalHeight;
        float baseX;
        float baseY;
        float baseW;
        float baseH;
        float rowX;
        float rowY;
        float rowW;
        float rowH;
        float pageX;
        float pageY;
        float pageW;
        float pageH;
        float closeX;
        float closeY;
        float closeW;
        float closeH;
        float metadataX;
        float metadataY;
        float metadataW;
        float metadataH;
        float metadataRow9Y;
    };
    const std::array<VariantCase, 4> variants{{
        {Model::Variant::Standard, "System/SaveLoad/SaveLoad.cmap",
         1.0f, 75.0f, 800, 600,
         -0.5f, 75.0f, 800.0f, 450.0f,
         229.5f, 170.5f, 559.0f, 33.0f,
         409.5f, 134.5f, 40.0f, 27.0f,
         701.5f, 80.5f, 99.0f, 43.0f,
         1.0f, 175.5f, 252.0f, 24.0f, 472.5f},
        {Model::Variant::Wide, "System/SaveLoad/SaveLoad_Wide.cmap",
         1.0f, 0.0f, 800, 450,
         -0.5f, 0.0f, 800.0f, 450.0f,
         229.5f, 95.5f, 559.0f, 33.0f,
         409.5f, 59.5f, 40.0f, 27.0f,
         701.5f, 5.5f, 99.0f, 43.0f,
         1.0f, 100.5f, 252.0f, 24.0f, 397.5f},
        {Model::Variant::WideFull, "System/SaveLoad/SaveLoad_Wide_Full.cmap",
         1.6f, 0.0f, 1280, 720,
         -0.8f, 0.0f, 1280.0f, 720.0f,
         367.2f, 152.8f, 894.4f, 52.8f,
         655.2f, 95.2f, 64.0f, 43.2f,
         1122.4f, 8.8f, 158.4f, 68.8f,
         1.6f, 160.8f, 403.2f, 38.4f, 636.0f},
        {Model::Variant::WideNote, "System/SaveLoad/SaveLoad_Wide_Note.cmap",
         1.28f, 0.0f, 1024, 576,
         -0.64f, 0.0f, 1024.0f, 576.0f,
         293.76f, 122.24f, 715.52f, 42.24f,
         524.16f, 76.16f, 51.2f, 34.56f,
         897.92f, 7.04f, 126.72f, 55.04f,
         1.28f, 128.64f, 322.56f, 30.72f, 508.8f},
    }};

    for (const auto& variantCase : variants) {
        const auto variantLayout = Plan::LayoutFor(variantCase.variant);
        Check(Eq(variantLayout.scale, variantCase.scale) &&
                  Eq(variantLayout.yOffset, variantCase.yOffset) &&
                  variantLayout.logicalWidth == variantCase.logicalWidth &&
                  variantLayout.logicalHeight == variantCase.logicalHeight,
              "variant render layout mismatch");

        Model variantModel;
        std::string variantError;
        Check(variantModel.Open(assetsRoot + "/" + variantCase.relativePath,
                                variantCase.variant, false, &variantError),
              variantError.c_str());

        auto variantSprites = Plan::Build(variantModel);
        const auto* variantBase = Find(variantSprites, Plan::Role::Base);
        Check(variantBase != nullptr && !variantBase->hasSource &&
                  Eq(variantBase->destination.x, variantCase.baseX) &&
                  Eq(variantBase->destination.y, variantCase.baseY) &&
                  Eq(variantBase->destination.w, variantCase.baseW) &&
                  Eq(variantBase->destination.h, variantCase.baseH),
              "variant SAVE base geometry mismatch");
        const auto* variantRoute = Find(variantSprites, Plan::Role::RouteMap);
        Check(variantRoute != nullptr &&
                  Eq(variantRoute->source.x, 1.0f) &&
                  Eq(variantRoute->source.y, 1.0f) &&
                  Eq(variantRoute->source.w, 194.0f) &&
                  Eq(variantRoute->source.h, 38.0f),
              "variant fixed RouteMap visual missing in SAVE");
        const auto* variantPage = Find(variantSprites, Plan::Role::CurrentPage);
        Check(variantPage != nullptr &&
                  Eq(variantPage->source.x, 391.0f) &&
                  Eq(variantPage->source.y, 70.0f) &&
                  Eq(variantPage->source.w, 39.0f) &&
                  Eq(variantPage->source.h, 26.0f) &&
                  Eq(variantPage->destination.x, variantCase.pageX) &&
                  Eq(variantPage->destination.y, variantCase.pageY) &&
                  Eq(variantPage->destination.w, variantCase.pageW) &&
                  Eq(variantPage->destination.h, variantCase.pageH),
              "variant current-page geometry/source mismatch");

        int variantX = 0;
        int variantY = 0;
        Check(FindRegion(variantModel.Cmap(), 1, &variantX, &variantY),
              "variant row0 region missing");
        Check(variantModel.SetSelectionFromPointer(variantX, variantY),
              "variant row0 hover did not select");
        variantSprites = Plan::Build(variantModel);
        const auto* variantRow = Find(variantSprites, Plan::Role::RowHover);
        Check(variantRow != nullptr &&
                  Eq(variantRow->source.x, 1.0f) &&
                  Eq(variantRow->source.y, 122.0f) &&
                  Eq(variantRow->source.w, 558.0f) &&
                  Eq(variantRow->source.h, 32.0f) &&
                  Eq(variantRow->destination.x, variantCase.rowX) &&
                  Eq(variantRow->destination.y, variantCase.rowY) &&
                  Eq(variantRow->destination.w, variantCase.rowW) &&
                  Eq(variantRow->destination.h, variantCase.rowH),
              "variant row0 geometry/source mismatch");

        Check(FindRegion(variantModel.Cmap(), 21, &variantX, &variantY),
              "variant close region missing");
        variantModel.SetSelectionFromPointer(variantX, variantY);
        variantSprites = Plan::Build(variantModel);
        const auto* variantClose = Find(variantSprites, Plan::Role::CloseHover);
        Check(variantClose != nullptr &&
                  Eq(variantClose->source.x, 391.0f) &&
                  Eq(variantClose->source.y, 1.0f) &&
                  Eq(variantClose->source.w, 98.0f) &&
                  Eq(variantClose->source.h, 42.0f) &&
                  Eq(variantClose->destination.x, variantCase.closeX) &&
                  Eq(variantClose->destination.y, variantCase.closeY) &&
                  Eq(variantClose->destination.w, variantCase.closeW) &&
                  Eq(variantClose->destination.h, variantCase.closeH),
              "variant close geometry/source mismatch");

        const auto variantMetadata =
            Plan::MetadataLayout(variantCase.variant, false);
        Check(variantMetadata.size() == 10u &&
                  Eq(variantMetadata[0].primarySource.x, 0.0f) &&
                  Eq(variantMetadata[0].primarySource.y, 2.0f) &&
                  Eq(variantMetadata[0].primarySource.w, 548.0f) &&
                  Eq(variantMetadata[0].primarySource.h, 48.0f) &&
                  Eq(variantMetadata[0].secondarySource.x, 1024.0f) &&
                  Eq(variantMetadata[0].optionalThirdSource.y, 514.0f) &&
                  Eq(variantMetadata[0].optionalThirdSource.w, 986.0f) &&
                  Eq(variantMetadata[0].primary.x, variantCase.metadataX) &&
                  Eq(variantMetadata[0].primary.y, variantCase.metadataY) &&
                  Eq(variantMetadata[0].primary.w, variantCase.metadataW) &&
                  Eq(variantMetadata[0].primary.h, variantCase.metadataH) &&
                  Eq(variantMetadata[9].primary.y, variantCase.metadataRow9Y),
              "variant metadata geometry/source mismatch");
    }

    std::cout << "SCHOOL DAYS SAVELOAD RENDER PLAN GATE PASS\n"
              << "all_variant_layouts=PASS\n"
              << "all_variant_base_geometry=PASS\n"
              << "all_variant_row_page_close_geometry=PASS\n"
              << "all_variant_source_rects_unscaled=PASS\n"
              << "all_variant_metadata_geometry=PASS\n"
              << "standard_yoffset75=PASS\n"
              << "row_hover_fixed_crop=PASS\n"
              << "current_page_dual_state=PASS\n"
              << "close_hover=PASS\n"
              << "route_fixed_record=PASS\n"
              << "pending_state_gate=PASS\n"
              << "metadata_geometry_only=PASS\n"
              << "glyph_advance_table=PASS\n"
              << "metadata_character_limits=PASS\n";
    return 0;
}
