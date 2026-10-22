#include <Kotonoha/routing/SchoolDaysReplayModel.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
[[noreturn]] void Fail(const std::string& m) { std::cerr << m << '\n'; std::exit(2); }
void Check(bool v, const char* m) { if (!v) Fail(m); }

void Put32(std::ofstream& out, uint32_t v) {
    const unsigned char b[4] = {static_cast<unsigned char>(v),
        static_cast<unsigned char>(v >> 8), static_cast<unsigned char>(v >> 16),
        static_cast<unsigned char>(v >> 24)};
    out.write(reinterpret_cast<const char*>(b), 4);
}

void WriteCmap(const std::filesystem::path& root, const std::string& rel,
               int controls) {
    const auto p = root / rel;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    Put32(out, static_cast<uint32_t>(controls + 1));
    Put32(out, 1);
    for (int i = 0; i < controls; ++i) out.put(static_cast<char>(i + 1));
    out.put(0);
}
}

int main() {
    namespace fs = std::filesystem;
    using Model = Kotonoha::SchoolDaysReplayModel;
    const fs::path root = fs::temp_directory_path() / "kotonoha-replay-model-gate";
    std::error_code ec; fs::remove_all(root, ec);

    for (auto variant : {Model::Variant::Standard, Model::Variant::Wide,
                         Model::Variant::WideFull, Model::Variant::WideNote}) {
        WriteCmap(root, Model::CmapRelativePath(Model::View::HScene, variant), 19);
        WriteCmap(root, Model::CmapRelativePath(Model::View::PlayData, variant), 33);
        WriteCmap(root, Model::SceneCheckCmapRelativePath(2, variant), 2);
        WriteCmap(root, Model::SceneCheckCmapRelativePath(4, variant), 4);
    }

    Model m;
    std::string error;
    Check(m.Open(root.string(), Model::Variant::Standard, Model::View::HScene, &error),
          error.c_str());
    Check(m.SetSelectionFromPointer(2, 0), "page hover failed"); // control2? raw3 close
    Model::Action a;
    Check(m.Activate(&a) && a.kind == Model::ActionKind::Close,
          "HScene close contract failed");

    // Page 0 tile 11 (control18) is special and must fail closed while locked.
    m.SetSelectionFromPointer(18, 0);
    Check(!m.Activate(&a), "locked HScene tile activated");
    m.SetHSceneAvailable(11, true);
    Check(m.Activate(&a) && a.kind == Model::ActionKind::SceneCheckRequested &&
              a.index == 11 && a.choiceCount == 4 && a.sceneCheckKey == 0,
          "HScene special 11 did not open four-choice SceneCheck");
    Check(m.SceneCheckPreviewRecord() == 4,
          "four-choice SceneCheck preview record drifted");
    m.SetSceneCheckChoiceAvailable(2, true);
    m.SetSelectionFromPointer(2, 0);
    Check(m.Activate(&a) && a.kind == Model::ActionKind::SceneCheckAccepted &&
              a.index == 11 && a.choice == 2,
          "SceneCheck accepted choice contract failed");
    Check(!m.SceneCheckOpen(), "SceneCheck accept did not close nested modal");

    // Reopen the same special and exercise the explicit semantic cancel seam.
    m.SetSelectionFromPointer(18, 0);
    Check(m.Activate(&a) && m.SceneCheckOpen(), "SceneCheck reopen failed");
    Check(m.CancelSceneCheck() && !m.SceneCheckOpen(),
          "SceneCheck cancel/resume failed");

    // HScene page2, first tile -> absolute24, ordinary direct request.
    m.SetSelectionFromPointer(5, 0); // control5 => page2
    Check(m.Activate(&a) && m.Page() == 2, "HScene page2 failed");
    m.SetHSceneAvailable(24, true);
    m.SetSelectionFromPointer(7, 0);
    Check(m.Activate(&a) && a.kind == Model::ActionKind::HSceneRequested &&
              a.index == 24,
          "ordinary HScene absolute index failed");

    // Switch tab to PlayData; page9 right-row19 -> absolute99.
    m.SetSelectionFromPointer(1, 0);
    Check(m.Activate(&a) && m.CurrentView() == Model::View::PlayData && m.Page() == 0,
          "PlayData tab switch failed");
    m.SetSelectionFromPointer(22, 0); // control22 => page9
    Check(m.Activate(&a) && m.Page() == 9, "PlayData page9 failed");
    m.SetPlayDataAvailable(99, true);
    m.SetSelectionFromPointer(32, 0); // right row19
    Check(m.Activate(&a) && a.kind == Model::ActionKind::PlayDataRequested &&
              a.index == 99,
          "PlayData absolute index99 failed");

    // Resource families remain logical-profile selected, independent of output size.
    Check(Model::CmapRelativePath(Model::View::HScene, Model::Variant::WideFull) ==
              "System/Replay/Replay_HScene_Wide_Full.cmap",
          "Replay WideFull resource mapping failed");
    Check(Model::SceneCheckCmapRelativePath(2, Model::Variant::WideNote) ==
              "System/Replay/Pop_Replay_2_Wide_Note.cmap",
          "SceneCheck WideNote resource mapping failed");
    Check(Model::HSceneUnlockCount() == 41 &&
              std::string(Model::HSceneUnlockKey(0)) == "REP02_2S_W03" &&
              std::string(Model::HSceneUnlockKey(40)) == "REP05_SE_N00" &&
              Model::HSceneUnlockKey(41) == nullptr,
          "authenticated HScene unlock table drifted");
    Check(std::string(Model::SceneCheckAvailabilityKey(0, 0)) ==
              "REP03_KB_N00A" &&
              std::string(Model::SceneCheckAvailabilityKey(0, 3)) ==
              "REP03_KB_N00D" &&
              std::string(Model::SceneCheckAvailabilityKey(2, 1)) ==
              "REP04_C1_A00B" &&
              std::string(Model::SceneCheckAvailabilityKey(3, 1)) ==
              "REP05_5H_D00B" &&
              Model::SceneCheckAvailabilityKey(2, 2) == nullptr,
          "authenticated SceneCheck availability table drifted");

    m.Close();
    fs::remove_all(root, ec);
    std::cout << "SCHOOL DAYS REPLAY MODEL GATE PASS\n"
              << "hscene_pages_and_absolute_indices=PASS\n"
              << "scenecheck_2_4_internal_modal=PASS\n"
              << "availability_fail_closed=PASS\n"
              << "hscene_unlock_table41=PASS\n"
              << "scenecheck_availability_keys=PASS\n"
              << "playdata_page_slot_mapping=PASS\n"
              << "logical_variant_resources=PASS\n";
    return 0;
}
