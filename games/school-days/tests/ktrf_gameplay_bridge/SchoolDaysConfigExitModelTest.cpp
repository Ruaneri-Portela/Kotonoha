#include <Kotonoha/routing/SchoolDaysConfigMenuModel.hpp>
#include <Kotonoha/routing/SchoolDaysExitViewModel.hpp>
#include <Kotonoha/routing/SchoolDaysTitleModel.hpp>

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
    std::cerr << "SchoolDaysConfigExitModelTest: " << message << '\n';
    std::exit(2);
}

void Check(bool value, const char* message) {
    if (!value) Fail(message);
}

struct TempTree {
    std::filesystem::path root;

    TempTree() {
        const auto stamp = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        root = std::filesystem::temp_directory_path() /
               ("kotonoha-config-exit-" + std::to_string(stamp));
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
    if (!out) Fail("unable to create synthetic CMAP: " + path.string());
    PutLe32(out, static_cast<uint32_t>(regions.size()));
    PutLe32(out, 1u);
    out.write(reinterpret_cast<const char*>(regions.data()),
              static_cast<std::streamsize>(regions.size()));
    if (!out) Fail("unable to write synthetic CMAP: " + path.string());
}

template <typename Model>
void SelectControl(Model& model, int control) {
    if (!model.SetSelectionFromPointer(control, 0) ||
        model.Selection() != control) {
        Fail("pointer did not select expected CMAP control " +
             std::to_string(control));
    }
}

} // namespace

int main() {
    using Config = Kotonoha::SchoolDaysConfigMenuModel;
    using Exit = Kotonoha::SchoolDaysExitViewModel;
    using Title = Kotonoha::SchoolDaysTitleModel;

    Check(static_cast<int>(Title::Result::Config) == 4,
          "Title Config result code drifted");
    Check(static_cast<int>(Title::Result::Exit) == -1,
          "Title Exit result code drifted");

    Check(Config::SelectVariant(0, 0, 0) == Config::Variant::Standard &&
              Config::SelectVariant(1, 0, 0) == Config::Variant::Wide &&
              Config::SelectVariant(1, 1, 0) == Config::Variant::WideFull &&
              Config::SelectVariant(1, 1, 1) == Config::Variant::WideNote,
          "Config variant predicate mismatch");
    Check(Exit::SelectVariant(0, 0, 0) == Exit::Variant::Standard &&
              Exit::SelectVariant(1, 0, 0) == Exit::Variant::Wide &&
              Exit::SelectVariant(1, 1, 0) == Exit::Variant::WideFull &&
              Exit::SelectVariant(1, 1, 1) == Exit::Variant::WideNote,
          "Exit variant predicate mismatch");

    Check(Config::CmapRelativePath(Config::Page::Def, Config::Variant::Standard) ==
              "System/Option/Option_Def.cmap" &&
              Config::CmapRelativePath(Config::Page::Sound, Config::Variant::Wide) ==
                  "System/Option/Option_Sound_Wide.cmap" &&
              Config::CmapRelativePath(Config::Page::Display,
                                       Config::Variant::Wide) ==
                  "System/Option/Option_Def_Wide.cmap" &&
              Config::CmapRelativePath(Config::Page::Def,
                                       Config::Variant::WideNote) ==
                  "System/Option/Option_Def_Wide_Note.cmap",
          "Config CMAP resource paths mismatch");
    Check(Config::BaseRelativePath(Config::Page::Def) ==
              "System/Option/Option_Def.png" &&
              Config::ChipRelativePath(Config::Page::Sound) ==
                  "System/Option/Option_Sound_Chip.png",
          "Config image resource paths mismatch");

    Check(std::string(Exit::CmapRelativePath(Exit::Variant::Standard)) ==
              "System/Exit/Popup.cmap" &&
              std::string(Exit::CmapRelativePath(Exit::Variant::Wide)) ==
                  "System/Exit/Popup_Wide.cmap" &&
              std::string(Exit::CmapRelativePath(Exit::Variant::WideFull)) ==
                  "System/Exit/Popup_Wide_Full.cmap" &&
              std::string(Exit::CmapRelativePath(Exit::Variant::WideNote)) ==
                  "System/Exit/Popup_Wide_Note.cmap" &&
              std::string(Exit::BaseRelativePath(Exit::BaseArtwork::Exit)) ==
                  "System/Exit/Popup_Exit.png" &&
              std::string(Exit::BaseRelativePath(Exit::BaseArtwork::Title)) ==
                  "System/Exit/Popup_Title.png" &&
              std::string(Exit::ChipRelativePath()) ==
                  "System/Exit/Popup_Chip.png",
          "Exit resource paths mismatch");

    TempTree tree;
    const std::vector<uint8_t> configRegions = [] {
        std::vector<uint8_t> result;
        for (int raw = 1; raw <= 44; ++raw) {
            result.push_back(static_cast<uint8_t>(raw));
        }
        result.push_back(0u);
        return result;
    }();
    const std::vector<uint8_t> exitRegions = {1u, 2u, 0u};

    const Config::Variant configVariants[] = {
        Config::Variant::Standard, Config::Variant::Wide,
        Config::Variant::WideFull, Config::Variant::WideNote,
    };
    const Config::Page pages[] = {
        Config::Page::Def, Config::Page::Sound, Config::Page::Display,
    };
    for (const auto variant : configVariants) {
        for (const auto page : pages) {
            WriteCmap(tree.root, Config::CmapRelativePath(page, variant),
                      configRegions);
        }
    }

    const Exit::Variant exitVariants[] = {
        Exit::Variant::Standard, Exit::Variant::Wide,
        Exit::Variant::WideFull, Exit::Variant::WideNote,
    };
    for (const auto variant : exitVariants) {
        WriteCmap(tree.root, Exit::CmapRelativePath(variant), exitRegions);
    }

    std::string error;
    Config config;
    for (const auto variant : configVariants) {
        for (const auto page : pages) {
            Check(config.Open(tree.root.string(), variant, page, &error),
                  error.c_str());
            Check(config.CurrentVariant() == variant && config.CurrentPage() == page,
                  "Config Open lost variant/page identity");
            Check(config.Cmap().Width() == 45u && config.Cmap().Height() == 1u,
                  "Config synthetic CMAP dimensions mismatch");
        }
    }

    Check(config.Open(tree.root.string(), Config::Variant::Standard,
                      Config::Page::Def, &error),
          error.c_str());
    Config::Action action;
    for (int control = 0; control <= 2; ++control) {
        SelectControl(config, control);
        Check(config.Activate(&action) &&
                  action.kind == Config::ActionKind::SelectPage &&
                  action.page == static_cast<Config::Page>(control),
              "Config common tab mapping mismatch");
    }
    SelectControl(config, 3);
    Check(config.Activate(&action) && action.kind == Config::ActionKind::Close,
          "Config close mapping mismatch");

    struct ToggleWitness {
        int control;
        Config::ActionKind kind;
        int value;
    };
    const ToggleWitness defWitnesses[] = {
        {4, Config::ActionKind::RequestWide, 1},
        {5, Config::ActionKind::RequestWide, 0},
        {6, Config::ActionKind::RequestFull, 1},
        {7, Config::ActionKind::RequestFull, 0},
        {8, Config::ActionKind::SetSkip, 1},
        {9, Config::ActionKind::SetSkip, 0},
        {10, Config::ActionKind::SetSuperSkip, 1},
        {11, Config::ActionKind::SetSuperSkip, 0},
        {12, Config::ActionKind::SetDrawMessage, 1},
        {13, Config::ActionKind::SetDrawMessage, 0},
    };
    for (const auto& witness : defWitnesses) {
        SelectControl(config, witness.control);
        Check(config.Activate(&action) && action.kind == witness.kind &&
                  action.value == witness.value,
              "Config Def action mapping mismatch");
    }

    Check(config.Open(tree.root.string(), Config::Variant::Wide,
                      Config::Page::Sound, &error),
          error.c_str());
    for (int bank = 0; bank < 3; ++bank) {
        for (int pairMember = 0; pairMember < 2; ++pairMember) {
            const int control = 4 + bank * 2 + pairMember;
            const int expectedDirection = pairMember == 0 ? 1 : 0;
            SelectControl(config, control);
            Check(config.Activate(&action) &&
                      action.kind == Config::ActionKind::StepSoundBank &&
                      action.soundBank == bank &&
                      action.choice == expectedDirection,
                  "Config Sound +/- direction mapping mismatch");
        }
    }
    SelectControl(config, 10);
    Check(config.Activate(&action) &&
              action.kind == Config::ActionKind::SetMenVoice && action.value == 1,
          "Config MenVoice on mapping mismatch");
    SelectControl(config, 11);
    Check(config.Activate(&action) &&
              action.kind == Config::ActionKind::SetMenVoice && action.value == 0,
          "Config MenVoice off mapping mismatch");
    SelectControl(config, 12);
    Check(config.Activate(&action) && action.kind == Config::ActionKind::SetMute &&
              action.value == 1,
          "Config Mute on mapping mismatch");
    SelectControl(config, 13);
    Check(config.Activate(&action) && action.kind == Config::ActionKind::SetMute &&
              action.value == 0,
          "Config Mute off mapping mismatch");

    for (int bank = 0; bank < 3; ++bank) {
        for (int value = 1; value <= 10; ++value) {
            const int control = 14 + bank * 10 + value - 1;
            SelectControl(config, control);
            Check(config.Activate(&action) &&
                      action.kind == Config::ActionKind::SetSoundLevel &&
                      action.soundBank == bank && action.value == value,
                  "Config direct sound-level mapping mismatch");
        }
    }
    Check(config.SetSelectionFromPointer(44, 0) && config.Selection() == -1 &&
              !config.Activate(&action),
          "Config background hit did not clear activation");

    Exit exitView;
    for (const auto variant : exitVariants) {
        Check(exitView.Open(tree.root.string(), variant, &error), error.c_str());
        Check(exitView.CurrentVariant() == variant && exitView.Cmap().Width() == 3u &&
                  exitView.Cmap().Height() == 1u,
              "Exit Open lost variant/CMAP identity");
    }

    Check(exitView.Open(tree.root.string(), Exit::Variant::Standard, &error),
          error.c_str());
    Check(exitView.SetSelectionFromPointer(0, 0) && exitView.Selection() == 0,
          "Exit CMAP id0 pointer mapping mismatch");
    Exit::Activation exitAction;
    Check(exitView.Activate(&exitAction) &&
              exitAction.outcome == Exit::Outcome::ConfirmExit &&
              exitAction.resFlag == 1 && Exit::ConfirmExitHostCode == 9,
          "Exit confirm contract mismatch");
    Check(exitView.SetSelectionFromPointer(1, 0) && exitView.Selection() == 1,
          "Exit CMAP id1 pointer mapping mismatch");
    Check(exitView.Activate(&exitAction) &&
              exitAction.outcome == Exit::Outcome::ReturnToTitle &&
              exitAction.resFlag == 0 && Exit::ReturnToTitleParentMode == 2,
          "Exit return-to-title contract mismatch");
    Check(exitView.SetSelectionFromPointer(2, 0) && exitView.Selection() == -1 &&
              !exitView.Activate(&exitAction),
          "Exit background hit did not clear activation");

    std::cout << "SCHOOL DAYS CONFIG/EXIT MODEL GATE PASS\n"
              << "title_config_exit_codes=PASS\n"
              << "resource_variants=PASS\n"
              << "config_common_controls=PASS\n"
              << "config_display_tab2=PASS\n"
              << "config_def_typed_outputs=PASS\n"
              << "config_sound_opaque_banks=PASS\n"
              << "exit_two_choice_contract=PASS\n"
              << "pointer_only_background_clear=PASS\n";
    return 0;
}
