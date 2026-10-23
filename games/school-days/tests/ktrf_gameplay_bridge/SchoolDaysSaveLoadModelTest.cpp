#include <Kotonoha/routing/SchoolDaysSaveLoadModel.hpp>

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
using Model = Kotonoha::SchoolDaysSaveLoadModel;

[[noreturn]] void Fail(const char* message) {
    std::cerr << "SchoolDaysSaveLoadModelTest: " << message << '\n';
    std::exit(2);
}
void Check(bool value, const char* message) { if (!value) Fail(message); }

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
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: SchoolDaysSaveLoadModelTest <assets-root>\n";
        return 2;
    }
    const std::string assetsRoot = argv[1];
    const std::string cmapPath =
        assetsRoot + "/System/SAVELOAD/SAVELOAD.CMAP";

    struct CorpusCase {
        Model::Variant variant;
        const char* relativePath;
        uint32_t width;
        uint32_t height;
    };
    const std::array<CorpusCase, 4> corpusCases{{
        {Model::Variant::Standard, "System/SaveLoad/SaveLoad.cmap", 800u, 600u},
        {Model::Variant::Wide, "System/SaveLoad/SaveLoad_Wide.cmap", 800u, 450u},
        {Model::Variant::WideFull, "System/SaveLoad/SaveLoad_Wide_Full.cmap", 1280u, 720u},
        {Model::Variant::WideNote, "System/SaveLoad/SaveLoad_Wide_Note.cmap", 1024u, 576u},
    }};
    for (const auto& corpusCase : corpusCases) {
        Check(std::string(Model::CmapRelativePath(corpusCase.variant)) ==
                  corpusCase.relativePath,
              "variant CMAP path mismatch");
        Model corpusModel;
        std::string corpusError;
        const std::string path = assetsRoot + "/" + corpusCase.relativePath;
        Check(corpusModel.Open(path, corpusCase.variant, false, &corpusError),
              corpusError.c_str());
        Check(corpusModel.Cmap().Width() == corpusCase.width &&
                  corpusModel.Cmap().Height() == corpusCase.height,
              "variant CMAP logical dimensions mismatch");

        std::array<bool, 33> seen{};
        for (uint32_t y = 0; y < corpusModel.Cmap().Height(); ++y) {
            for (uint32_t x = 0; x < corpusModel.Cmap().Width(); ++x) {
                const int id = corpusModel.Cmap().Get(static_cast<int>(x),
                                                      static_cast<int>(y));
                Check(id >= 0 && id <= 32, "variant CMAP id escaped 0..32");
                seen[static_cast<size_t>(id)] = true;
            }
        }
        for (bool present : seen) {
            Check(present, "variant CMAP does not contain complete 0..32 domain");
        }
    }

    Model model;
    std::string error;
    Check(model.Open(cmapPath, Model::Variant::Standard, false, &error), error.c_str());
    Check(model.Page() == 0 && model.Selection() == -1,
          "SaveLoad did not open at page zero with unseeded pointer selection");

    int x = 0, y = 0;
    Model::Output out;

    // Region 1 -> control0 -> slot0.
    Check(FindRegion(model.Cmap(), 1, &x, &y), "missing CMAP region1");
    Check(model.SetSelectionFromPointer(x, y) && model.Selection() == 0,
          "region1 did not map to row control0");
    Check(model.Activate(&out) && out.action == Model::Action::None &&
              model.SavePending(),
          "page0 row0 did not enter deferred save state");
    Check(!model.SetSelectionFromPointer(-1, -1) && model.Selection() == 0,
          "pending save did not block pointer actions");
    Check(!model.Activate(&out), "pending save did not block activation");
    Check(model.Tick(&out) && out.action == Model::Action::SaveSlotRequested &&
              out.slot == 0 && !model.SavePending(),
          "next tick did not request slot0 exactly once");
    Check(!model.Tick(&out), "save request repeated after pending tick");

    // Region 10 -> control9 -> slot9.
    Check(FindRegion(model.Cmap(), 10, &x, &y), "missing CMAP region10");
    model.SetSelectionFromPointer(x, y);
    Check(model.Activate(&out) && model.Tick(&out) && out.slot == 9,
          "page0 row9 did not request slot9");

    // Region 12 -> control11 -> page1; then row0 and duplicate-right row9.
    Check(FindRegion(model.Cmap(), 12, &x, &y), "missing page1 region");
    model.SetSelectionFromPointer(x, y);
    Check(model.Activate(&out) && model.Page() == 1 &&
              out.action == Model::Action::None,
          "page1 tab did not change page without output");
    Check(FindRegion(model.Cmap(), 1, &x, &y), "missing row0 after page change");
    model.SetSelectionFromPointer(x, y);
    Check(model.Activate(&out) && model.Tick(&out) && out.slot == 10,
          "page1 row0 did not request slot10");
    Check(FindRegion(model.Cmap(), 32, &x, &y), "missing duplicate row9 region");
    model.SetSelectionFromPointer(x, y);
    Check(model.Activate(&out) && model.Tick(&out) && out.slot == 19,
          "duplicate-right row9 did not map to slot19");

    // Page9 + row9 -> slot99.
    Check(FindRegion(model.Cmap(), 20, &x, &y), "missing page9 region");
    model.SetSelectionFromPointer(x, y);
    Check(model.Activate(&out) && model.Page() == 9,
          "page9 tab did not select page9");
    Check(FindRegion(model.Cmap(), 10, &x, &y), "missing row9 region");
    model.SetSelectionFromPointer(x, y);
    Check(model.Activate(&out) && model.Tick(&out) && out.slot == 99,
          "page9 row9 did not request slot99");

    // Closing and reopening the singleton preserves page 9 rather than
    // constructing a new SaveLoad instance/page 0.
    model.Close();
    Check(model.Open(cmapPath, Model::Variant::Standard, false, &error) &&
              model.Page() == 9,
          "SaveLoad reopen did not preserve singleton page");

    // RouteMap is inert in SAVE mode; Close emits result boundary.
    Check(FindRegion(model.Cmap(), 22, &x, &y), "missing RouteMap region");
    model.SetSelectionFromPointer(x, y);
    Check(!model.Activate(&out) && out.action == Model::Action::None,
          "RouteMap unexpectedly activated in SAVE mode");
    Check(FindRegion(model.Cmap(), 21, &x, &y), "missing close region");
    model.SetSelectionFromPointer(x, y);
    Check(model.Activate(&out) && out.action == Model::Action::Close,
          "close control did not emit Close");

    Check(model.SetSelectionFromPointer(-1, -1) && model.Selection() == -1,
          "background hover did not clear SaveLoad selection");

    // Authenticated TextInput path: row activation opens the host dialog first.
    // Only _CommentSet (OK) arms +0x98; Cancel leaves no deferred save.
    model.Close();
    Check(model.Open(cmapPath, Model::Variant::Standard, true, &error), error.c_str());
    Check(FindRegion(model.Cmap(), 11, &x, &y), "missing TextInput page0 region");
    model.SetSelectionFromPointer(x, y);
    Check(model.Activate(&out) && model.Page() == 0,
          "TextInput witness did not return singleton to page0");
    Check(FindRegion(model.Cmap(), 1, &x, &y), "missing TextInput row region");
    model.SetSelectionFromPointer(x, y);
    Check(model.Activate(&out) &&
              out.action == Model::Action::CommentInputRequested && out.slot == 0 &&
              model.CommentInputPending() && !model.SavePending(),
          "TextInput row did not request comment before save");
    Check(!model.Tick(&out), "TextInput request saved before _CommentSet callback");
    Check(model.CancelComment() && !model.CommentInputPending() &&
              !model.SavePending() && !model.Tick(&out),
          "dialog cancel did not abort slot save");

    model.SetSelectionFromPointer(x, y);
    Check(model.Activate(&out) && out.action == Model::Action::CommentInputRequested,
          "second TextInput row activation failed");
    const std::u16string comment = u"native comment";
    Check(model.SubmitComment(comment) && !model.CommentInputPending() &&
              model.SavePending(),
          "_CommentSet equivalent did not arm deferred save");
    Check(model.Tick(&out) && out.action == Model::Action::SaveSlotRequested &&
              out.slot == 0 && out.comment == comment && !model.SavePending(),
          "comment-confirmed save did not fire on following tick");

    // LOAD mode shares pages/rows and still exposes the host TextInput
    // capability for comment metadata display, but it never opens the editor;
    // occupied rows dispatch immediately. Empty rows stay inert; control21 is
    // RouteMap.
    model.Close();
    Check(model.Open(cmapPath, Model::Variant::Standard, Model::Mode::Load,
                     true, &error),
          error.c_str());
    Check(model.CurrentMode() == Model::Mode::Load &&
              model.TextInputEnabled() && !model.CanEditComment(),
          "LOAD mode did not preserve display capability while disabling edit");
    Check(FindRegion(model.Cmap(), 11, &x, &y), "missing LOAD page0 region");
    model.SetSelectionFromPointer(x, y);
    Check(model.Activate(&out) && model.Page() == 0,
          "LOAD page0 selection failed");
    std::array<bool, 10> occupancy{};
    occupancy[0] = true;
    model.SetRowOccupancy(occupancy);
    Check(FindRegion(model.Cmap(), 1, &x, &y), "missing LOAD occupied row0");
    model.SetSelectionFromPointer(x, y);
    Check(model.Activate(&out) &&
              out.action == Model::Action::LoadSlotRequested && out.slot == 0 &&
              !model.SavePending() && !model.CommentInputPending(),
          "occupied LOAD row did not dispatch immediately");

    Check(FindRegion(model.Cmap(), 2, &x, &y), "missing LOAD empty row1");
    model.SetSelectionFromPointer(x, y);
    Check(!model.Activate(&out) && out.action == Model::Action::None,
          "empty LOAD row unexpectedly dispatched");

    Check(FindRegion(model.Cmap(), 22, &x, &y), "missing LOAD RouteMap region");
    model.SetSelectionFromPointer(x, y);
    Check(model.Activate(&out) && out.action == Model::Action::RouteMap,
          "LOAD RouteMap did not emit result6 boundary");

    Check(FindRegion(model.Cmap(), 21, &x, &y), "missing LOAD close region");
    model.SetSelectionFromPointer(x, y);
    Check(model.Activate(&out) && out.action == Model::Action::Close,
          "LOAD close did not emit result0 boundary");

    std::cout << "SCHOOL DAYS SAVELOAD MODEL GATE PASS\n"
              << "all_variant_cmaps_0_32=PASS\n"
              << "page_zero=PASS\n"
              << "slot_0_9_10_19_99=PASS\n"
              << "duplicate_row_regions=PASS\n"
              << "page_tabs=PASS\n"
              << "save_deferred_next_tick=PASS\n"
              << "pending_blocks_actions=PASS\n"
              << "singleton_page_preserved=PASS\n"
              << "save_mode_routemap_inert=PASS\n"
              << "close_result0_boundary=PASS\n"
              << "background_hover_minus1=PASS\n"
              << "textinput_ok_deferred_save=PASS\n"
              << "textinput_cancel_aborts_save=PASS\n"
              << "load_occupied_immediate=PASS\n"
              << "load_empty_inert=PASS\n"
              << "load_comment_display_edit_split=PASS\n"
              << "load_routemap_result6=PASS\n";
    return 0;
}
