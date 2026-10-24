#include <Kotonoha/routing/SchoolDaysConfigMenuRenderPlan.hpp>
#include <Kotonoha/routing/SchoolDaysExitViewRenderPlan.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "SchoolDaysConfigExitRenderPlanTest: " << message << '\n';
    std::exit(2);
}

void Check(bool value, const char* message) {
    if (!value) Fail(message);
}

bool Feq(float a, float b) {
    const float d = a - b;
    return d > -0.001f && d < 0.001f;
}

template <typename Rect>
void CheckRect(const Rect& r, float x, float y, float w, float h,
               const char* message) {
    if (!Feq(r.x, x) || !Feq(r.y, y) || !Feq(r.w, w) || !Feq(r.h, h)) {
        Fail(message);
    }
}

struct TempTree {
    std::filesystem::path root;
    TempTree() {
        const auto stamp = std::chrono::high_resolution_clock::now()
                               .time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("kotonoha-config-exit-render-" + std::to_string(stamp));
        std::filesystem::create_directories(root);
    }
    ~TempTree() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void PutLe32(std::ofstream& out, uint32_t value) {
    const unsigned char bytes[4] = {
        static_cast<unsigned char>(value & 0xffu),
        static_cast<unsigned char>((value >> 8u) & 0xffu),
        static_cast<unsigned char>((value >> 16u) & 0xffu),
        static_cast<unsigned char>((value >> 24u) & 0xffu),
    };
    out.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

void WriteCmap(const std::filesystem::path& root, const std::string& relative,
               const std::vector<uint8_t>& regions) {
    const auto path = root / std::filesystem::path(relative);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) Fail("unable to create synthetic CMAP");
    PutLe32(out, static_cast<uint32_t>(regions.size()));
    PutLe32(out, 1u);
    out.write(reinterpret_cast<const char*>(regions.data()),
              static_cast<std::streamsize>(regions.size()));
    if (!out) Fail("unable to write synthetic CMAP");
}

std::vector<uint8_t> ConfigRegions() {
    std::vector<uint8_t> result;
    for (int raw = 1; raw <= 44; ++raw) {
        result.push_back(static_cast<uint8_t>(raw));
    }
    result.push_back(0u);
    return result;
}

template <typename Model>
void Select(Model& model, int control) {
    model.SetSelectionFromPointer(control, 0);
    Check(model.Selection() == control, "synthetic CMAP selection mismatch");
}

} // namespace

int main() {
    using ConfigModel = Kotonoha::SchoolDaysConfigMenuModel;
    using ConfigPlan = Kotonoha::SchoolDaysConfigMenuRenderPlan;
    using ExitModel = Kotonoha::SchoolDaysExitViewModel;
    using ExitPlan = Kotonoha::SchoolDaysExitViewRenderPlan;

    const auto cs = ConfigPlan::LayoutFor(ConfigModel::Variant::Standard);
    const auto cw = ConfigPlan::LayoutFor(ConfigModel::Variant::Wide);
    const auto cf = ConfigPlan::LayoutFor(ConfigModel::Variant::WideFull);
    const auto cn = ConfigPlan::LayoutFor(ConfigModel::Variant::WideNote);
    Check(Feq(cs.scale, 1.0f) && Feq(cs.yOffset, 75.0f) &&
              cs.logicalWidth == 800 && cs.logicalHeight == 600,
          "Config Standard logical layout mismatch");
    Check(Feq(cw.scale, 1.0f) && Feq(cw.yOffset, 0.0f) &&
              cw.logicalWidth == 800 && cw.logicalHeight == 450,
          "Config Wide logical layout mismatch");
    Check(Feq(cf.scale, 1.6f) && cf.logicalWidth == 1280 &&
              cf.logicalHeight == 720,
          "Config WideFull logical layout mismatch");
    Check(Feq(cn.scale, 1.28f) && cn.logicalWidth == 1024 &&
              cn.logicalHeight == 576,
          "Config WideNote logical layout mismatch");

    const auto es = ExitPlan::LayoutFor(ExitModel::Variant::Standard);
    const auto ew = ExitPlan::LayoutFor(ExitModel::Variant::Wide);
    const auto ef = ExitPlan::LayoutFor(ExitModel::Variant::WideFull);
    const auto en = ExitPlan::LayoutFor(ExitModel::Variant::WideNote);
    Check(Feq(es.scale, 1.0f) && Feq(es.yOffset, 75.0f) &&
              es.logicalWidth == 800 && es.logicalHeight == 600,
          "Exit Standard logical layout mismatch");
    Check(Feq(ef.scale, 1.6f) && Feq(ef.yOffset, 0.0f) &&
              ef.logicalWidth == 1280 && ef.logicalHeight == 720,
          "Exit WideFull logical layout mismatch");
    Check(Feq(ew.scale, 1.0f) && Feq(ew.yOffset, 0.0f) &&
              ew.logicalWidth == 800 && ew.logicalHeight == 450,
          "Exit Wide logical layout mismatch");
    Check(Feq(en.scale, 1.28f) && Feq(en.yOffset, 0.0f) &&
              en.logicalWidth == 1024 && en.logicalHeight == 576,
          "Exit WideNote logical layout mismatch");

    TempTree tree;
    const auto configRegions = ConfigRegions();
    WriteCmap(tree.root,
              ConfigModel::CmapRelativePath(ConfigModel::Page::Def,
                                            ConfigModel::Variant::Standard),
              configRegions);
    WriteCmap(tree.root,
              ConfigModel::CmapRelativePath(ConfigModel::Page::Sound,
                                            ConfigModel::Variant::WideFull),
              configRegions);
    WriteCmap(tree.root, ExitModel::CmapRelativePath(ExitModel::Variant::Standard),
              {1u, 2u, 0u});
    WriteCmap(tree.root, ExitModel::CmapRelativePath(ExitModel::Variant::WideFull),
              {1u, 2u, 0u});

    std::string error;
    ConfigModel config;
    ConfigPlan::VisualState state;
    state.defWideCurrent = 4;
    state.defFullCurrent = 6;
    state.defSkipCurrent = 8;
    state.defSuperSkipCurrent = 10;
    state.defDrawMessageCurrent = 12;
    Check(config.Open(tree.root.string(), ConfigModel::Variant::Standard,
                      ConfigModel::Page::Def, &error), error.c_str());

    auto sprites = ConfigPlan::Build(config, state);
    Check(sprites.size() == 7u, "Def base/current-state draw count mismatch");
    Check(sprites[0].texture == ConfigPlan::Texture::Base &&
              sprites[0].role == ConfigPlan::Role::Base && !sprites[0].hasSource,
          "Def base was not first");
    CheckRect(sprites[0].destination, -0.5f, 75.0f, 800.0f, 450.0f,
              "Def Standard base destination mismatch");
    Check(sprites[1].role == ConfigPlan::Role::ActiveTab &&
              sprites[1].recordIndex == 14,
          "Def active tab did not use middle band record14");
    CheckRect(sprites[1].source, 1.0f, 149.0f, 194.0f, 38.0f,
              "Def active-tab source mismatch");
    CheckRect(sprites[1].destination, 9.5f, 128.5f, 195.0f, 39.0f,
              "Def active-tab destination mismatch");
    Check(sprites[2].recordIndex == 17 && sprites[6].recordIndex == 25,
          "Def persistent state band mapping mismatch");

    Select(config, 4);
    sprites = ConfigPlan::Build(config, state);
    Check(sprites.size() == 8u && sprites.back().role == ConfigPlan::Role::Hover &&
              sprites.back().recordIndex == 30,
          "Def current control did not use third-band hover");
    CheckRect(sprites.back().source, 1.0f, 332.0f, 164.0f, 32.0f,
              "Def current-hover source mismatch");
    CheckRect(sprites.back().destination, 36.5f, 229.5f, 165.0f, 33.0f,
              "Def current-hover destination mismatch");

    Select(config, 5);
    sprites = ConfigPlan::Build(config, state);
    Check(sprites.back().recordIndex == 5,
          "Def inactive control did not use first-band hover");
    Select(config, 3);
    sprites = ConfigPlan::Build(config, state);
    Check(sprites.back().recordIndex == 3 && sprites.back().control == 3,
          "Def Close did not use first-band hover only");
    CheckRect(sprites.back().source, 586.0f, 1.0f, 98.0f, 42.0f,
              "Def Close hover source mismatch");
    Select(config, 0);
    sprites = ConfigPlan::Build(config, state);
    Check(sprites.back().recordIndex == 27,
          "Def active tab hover did not use third band");
    Select(config, 2);
    sprites = ConfigPlan::Build(config, state);
    Check(sprites.size() == 7u,
          "removed Config tab id2 unexpectedly emitted hover");

    config.Close();
    state = {};
    state.soundMenVoiceCurrent = 11;
    state.soundMuteCurrent = 13;
    state.soundLevels = {{0, 1, 10}};
    Check(config.Open(tree.root.string(), ConfigModel::Variant::WideFull,
                      ConfigModel::Page::Sound, &error), error.c_str());
    sprites = ConfigPlan::Build(config, state);
    Check(sprites.size() == 7u, "Sound base/state/level draw count mismatch");
    Check(sprites[1].recordIndex == 45 && sprites[2].recordIndex == 48 &&
              sprites[3].recordIndex == 50,
          "Sound active/current middle-band mapping mismatch");
    Check(sprites[4].role == ConfigPlan::Role::SoundLevel &&
              sprites[4].soundBank == 0 && Feq(sprites[4].source.w, 0.0f) &&
              Feq(sprites[4].destination.w, 1.6f),
          "Sound zero-level clip width mismatch");
    Check(sprites[5].soundBank == 1 && Feq(sprites[5].source.w, 54.0f) &&
              Feq(sprites[5].destination.w, 88.0f),
          "Sound level1 cumulative width mismatch");
    Check(sprites[6].soundBank == 2 && Feq(sprites[6].source.w, 522.0f) &&
              Feq(sprites[6].destination.w, 836.8f),
          "Sound level10 cumulative width mismatch");
    CheckRect(sprites[6].destination, 279.2f, 447.2f, 836.8f, 51.2f,
              "Sound WideFull level-strip destination mismatch");

    Select(config, 10);
    sprites = ConfigPlan::Build(config, state);
    Check(sprites[4].role == ConfigPlan::Role::Hover && sprites[4].recordIndex == 54,
          "Sound shipped c10 current-hover quirk was not preserved");
    Select(config, 11);
    sprites = ConfigPlan::Build(config, state);
    Check(sprites[4].recordIndex == 11,
          "Sound shipped c11 first-band-hover quirk was not preserved");
    Select(config, 13);
    sprites = ConfigPlan::Build(config, state);
    Check(sprites[4].recordIndex == 57,
          "Sound current Mute control did not use third-band hover");
    Select(config, 14);
    sprites = ConfigPlan::Build(config, state);
    Check(sprites.size() == 7u,
          "Sound direct level id unexpectedly emitted pointer hover");

    state.soundMenVoiceCurrent = -1;
    state.soundMuteCurrent = -1;
    state.soundLevels = {{-1, -1, -1}};
    config.SetSelectionFromPointer(44, 0); // synthetic trailing background -> -1
    sprites = ConfigPlan::Build(config, state);
    Check(sprites.size() == 2u &&
              sprites[0].role == ConfigPlan::Role::Base &&
              sprites[1].role == ConfigPlan::Role::ActiveTab,
          "unsupported legacy sound state emitted false current markers");

    ExitModel exitView;
    Check(exitView.Open(tree.root.string(), ExitModel::Variant::Standard, &error),
          error.c_str());
    Check(std::string(ExitModel::BaseRelativePath(ExitModel::BaseArtwork::Title)) ==
              "System/Exit/Popup_Title.png" &&
              std::string(ExitModel::BaseRelativePath(ExitModel::BaseArtwork::Exit)) ==
                  "System/Exit/Popup_Exit.png",
          "Exit base artwork resources mismatch");
    auto exitSprites = ExitPlan::Build(exitView);
    Check(exitSprites.size() == 1u &&
              exitSprites[0].texture == ExitPlan::Texture::Base,
          "Exit background should render base only");
    CheckRect(exitSprites[0].destination, -0.5f, 75.0f, 800.0f, 450.0f,
              "Exit Standard base destination mismatch");
    Select(exitView, 0);
    exitSprites = ExitPlan::Build(exitView);
    Check(exitSprites.size() == 2u && exitSprites[1].choice == 0 &&
              exitSprites[1].role == ExitPlan::Role::Hover,
          "Exit choice0 hover missing");
    CheckRect(exitSprites[1].source, 1.0f, 1.0f, 166.0f, 39.0f,
              "Exit choice0 source mismatch");
    CheckRect(exitSprites[1].destination, 229.5f, 408.5f, 167.0f, 40.0f,
              "Exit choice0 Standard destination mismatch");
    Select(exitView, 1);
    exitSprites = ExitPlan::Build(exitView);
    CheckRect(exitSprites[1].source, 168.0f, 1.0f, 167.0f, 39.0f,
              "Exit choice1 source mismatch");
    CheckRect(exitSprites[1].destination, 403.5f, 408.5f, 168.0f, 40.0f,
              "Exit choice1 Standard destination mismatch");

    exitView.Close();
    Check(exitView.Open(tree.root.string(), ExitModel::Variant::WideFull, &error),
          error.c_str());
    Select(exitView, 0);
    exitSprites = ExitPlan::Build(exitView);
    CheckRect(exitSprites[1].destination, 367.2f, 533.6f, 267.2f, 64.0f,
              "Exit WideFull scaled destination mismatch");

    std::cout << "SCHOOL DAYS CONFIG/EXIT RENDER PLAN GATE PASS\n"
              << "logical_variant_layouts=PASS\n"
              << "base_first_normals_baked=PASS\n"
              << "config_three_chip_bands=PASS\n"
              << "def_tabs_close_current_hover=PASS\n"
              << "removed_config_tab2_inert=PASS\n"
              << "sound_cumulative_clip=PASS\n"
              << "sound_c10_c11_native_quirk=PASS\n"
              << "exit_two_choice_hover=PASS\n";
    return 0;
}
