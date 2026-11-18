#include "PmailAudio.hpp"
#include <Pmail/Port.hpp>
#include <Pmail/Vm.hpp>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
Pmail::Arg integer(int value) {
  return {static_cast<uint32_t>(uint16_t(value))};
}
Pmail::Arg string(unsigned index) { return {(3u << 20) | (2u << 16) | index}; }
} // namespace

int main() {
  try {
    Kotonoha::PmailPort::PmailGame game({});
    auto object = std::make_shared<Pmail::ScriptObject>();
    object->id = 3;
    object->name = "synthetic-computer.obj";
    object->strings = {"mail",          "hello", "12:30", "one", "two",
                       "$NO meets $HO", "ROOM",  "etc00", "pm01"};
    game.vm().registerObject(object);
    const auto call = [&](int id, std::vector<Pmail::Arg> args) {
      game.native(game.vm(), id, args);
    };

    call(52, {string(0), integer(251), integer(160), integer(200), integer(100),
              integer(24), integer(10)});
    auto labels = game.computerText();
    check(labels.size() == 1 && labels[0].text == "mail" &&
              labels[0].x == 251 && labels[0].y == 160 &&
              labels[0].width == 200 && labels[0].height == 100 &&
              labels[0].size == 24 && labels[0].color == 10,
          "SYS 52 did not register positioned computer text");
    for (unsigned i = 0; i < 15; ++i)
      call(52, {string(0), integer(int(i)), integer(20), integer(50),
                integer(25), integer(16), integer(8)});
    check(game.computerText().size() == 10, "SYS 52 text slots are unbounded");

    call(80, {string(2)});
    check(game.computerClock() == "12:30", "SYS 80 clock missing");

    call(160, {integer(11), string(1)});
    call(162, {integer(2), string(3)});
    auto history = game.chatHistory();
    check(history.size() == 2 && history[0].mode == 11 &&
              history[0].text == "hello" && history[1].mode == 2 &&
              history[1].text == "one",
          "SYS 160 / 162 history order or modes incorrect");
    call(163, {});
    check(game.chatHistory().size() == 2,
          "SYS 163 redraw unexpectedly cleared chat");
    for (int i = 0; i < 17; ++i)
      call(162, {integer(i), string(4)});
    history = game.chatHistory();
    check(history.size() == 15 && history.front().mode == 2 &&
              history.back().mode == 16,
          "Chat history must retain the newest 15 entries");
    call(161, {});
    check(game.chatHistory().empty(), "SYS 161 did not clear chat");

    // Native message/text lifecycle recovered from pmail.exe:
    // 18/25 -> 0x422bb0/0x422c20 current/next text refs, 19/20 -> 0x4251d0/
    // 0x425100 text-surface lifetime, 216 -> 0xa8add8 text mode, 34 ->
    // 0x423a00 timed pause, 32/65 -> save-slot and framebuffer refreshes.
    call(18, {string(0)});
    call(25, {string(1)});
    check(game.currentTextRef() == std::pair<int, int>({3, 0}) &&
              game.nextTextRef() == std::pair<int, int>({3, 1}),
          "SYS 18/25 did not retain native text references");
    call(19, {});
    check(game.textSurfacesReady(), "SYS 19 did not initialize text surfaces");
    call(216, {integer(4)});
    check(game.textMode() == 4, "SYS 216 did not store the text mode");
    call(20, {});
    check(!game.textSurfacesReady(), "SYS 20 did not release text surfaces");
    call(17, {});
    check(game.textMode() == 0 &&
              game.currentTextRef() == std::pair<int, int>({-1, -1}) &&
              game.nextTextRef() == std::pair<int, int>({-1, -1}),
          "SYS 17 did not reset the native text state");
    call(160, {integer(1), string(1)});
    call(24, {});
    check(game.chatHistory().empty(),
          "SYS 24 did not clear the displayed chat transcript");
    call(160, {integer(1), string(1)});
    call(216, {integer(0)});
    check(game.chatHistory().empty() && game.textMode() == 0,
          "SYS 216(0) did not reset the displayed text and chat");
    call(34, {integer(5000)});
    check(game.nativeTimeWaitActive(), "SYS 34 did not start its timed wait");
    check(game.click(), "SYS 34 timed wait must be skippable by input");
    check(!game.nativeTimeWaitActive(),
          "SYS 34 timed wait did not end after input");
    call(32, {});
    call(65, {});
    check(Kotonoha::PmailPort::PmailAudio::DaySoundName(3, 613) ==
                  "V35330000" &&
              Kotonoha::PmailPort::PmailAudio::DaySoundName(3, 614) ==
                  "V35330001" &&
              Kotonoha::PmailPort::PmailAudio::DaySoundName(10, 613) ==
                  "V35530000" &&
              Kotonoha::PmailPort::PmailAudio::DaySoundName(11, 613) ==
                  "V35130000" &&
              Kotonoha::PmailPort::PmailAudio::DaySoundName(12, 613).empty(),
          "SYS 137 native day-sound naming changed");
    // SYS 53 -> pmail.exe:0x424e70 plays the L_/S_ x I5/I3 AVI variant for a
    // script-supplied stem. Without a runtime movie backend the VM must keep
    // running, but the requested stem is still observable.
    call(53, {string(8)});
    check(game.lastRequestedMovie() == "pm01" && !game.movieWaitActive(),
          "SYS 53 did not record its movie stem or blocked without a backend");
    // SYS 215 -> 0x422650 marks one of the 32 entries at 0x44f394 unlocked;
    // unknown names are ignored.
    call(215, {string(7)});
    call(215, {string(7)});
    call(215, {string(6)});
    check(game.extraUnlocked("etc00") && !game.extraUnlocked("ROOM") &&
              game.unlockedExtras() == std::vector<std::string>({"etc00"}),
          "SYS 215 did not retain the native extra/gallery unlock set");
    // SYS 212 -> pmail.exe:0x4225c0/0x41e9f0. The native omake menu starts at
    // "no item" (0), wraps between 1..4, and writes the result back through
    // the variable argument. Escape maps to 4 (EXIT).
    const Pmail::Arg extrasVar = {uint32_t(1u << 16) | 0x2388};
    call(212, {extrasVar});
    check(game.extrasMenuActive() && game.extrasSelection() == 0 &&
              game.vm().var(0x2388) == 0,
          "SYS 212 did not open the native extras menu at item 0");
    game.extrasMove(-1);
    check(game.extrasSelection() == 4,
          "SYS 212 up from the initial item must wrap to EXIT");
    game.extrasMove(1);
    check(game.extrasSelection() == 1,
          "SYS 212 down from EXIT must wrap to CG MODE");
    game.extrasHover(3);
    check(game.extrasChoose(3) && !game.extrasMenuActive() &&
              game.vm().var(0x2388) == 3,
          "SYS 212 did not write the selected mode back to its variable");
    call(212, {extrasVar});
    game.extrasHover(2);
    game.extrasMove(1);
    check(game.extrasSelection() == 3,
          "SYS 212 keyboard navigation did not advance from the hovered item");
    check(game.extrasChoose(4) && game.vm().var(0x2388) == 4,
          "SYS 212 Escape/EXIT did not return mode 4");

    // SYS 225/226 -> pmail.exe:0x422830 posts WM_COMMAND 0xbba (load) and
    // 0xbbb (save), the original dialogs. The VM stays blocked until the
    // host reports the dialog closed.
    bool hostDialogLoad = false;
    game.openSaveMenu = [&](bool load) {
      hostDialogLoad = load;
      return true;
    };
    call(225, {});
    check(game.saveMenuWaitActive() && game.saveMenuWaitLoad() &&
              hostDialogLoad && game.vm().blocked(),
          "SYS 225 did not open the native load-dialog path");
    game.resumeFromSaveMenu();
    check(!game.saveMenuWaitActive() && !game.vm().blocked(),
          "SYS 225 did not resume after the load dialog closed");
    call(226, {});
    check(game.saveMenuWaitActive() && !game.saveMenuWaitLoad() &&
              game.vm().blocked(),
          "SYS 226 did not open the native save-dialog path");
    game.resumeFromSaveMenu();
    check(!game.saveMenuWaitActive() && !game.vm().blocked(),
          "SYS 226 did not resume after the save dialog closed");

    // SYS 209 -> pmail.exe:0x405390. CG MODE lists the 129-entry retail CG
    // table in eight category tabs, 12 thumbnails per page. Selecting a cell
    // opens the full view; Escape returns to the grid and then to the menu.
    using ViewerMode = Kotonoha::PmailPort::PmailGame::ExtrasViewerMode;
    call(209, {});
    check(game.extrasViewerActive() &&
              game.extrasViewerMode() == ViewerMode::CgGrid,
          "SYS 209 did not open CG MODE");
    check(game.extrasViewerCategory() == 0 && game.extrasViewerPage() == 0 &&
              game.extrasViewerPageCount() == 1 &&
              !game.extrasViewerSlotLocked(1),
          "SYS 209 started with the wrong ETC category/page");
    auto cgCells = game.extrasViewerCells();
    check(cgCells.size() == 12 && cgCells[0] == "open01" &&
              cgCells[4] == "saku13" && cgCells[6] == "endi01" &&
              cgCells[7].empty(),
          "SYS 209 did not enumerate the retail ETC CG table");
    check(game.extrasViewerActivate() && game.extrasViewerFullView(),
          "SYS 209 did not open the selected CG");
    game.extrasViewerBack();
    check(!game.extrasViewerFullView() && game.extrasViewerActive(),
          "SYS 209 did not return from the CG full view");
    game.extrasViewerSetCategory(1);
    cgCells = game.extrasViewerCells();
    check(game.extrasViewerCategory() == 1 && cgCells[0] == "hiro01" &&
              game.extrasViewerPageCount() == 4,
          "SYS 209 category tab did not switch to HIRO");
    game.extrasViewerPageBy(1);
    check(game.extrasViewerPage() == 1 && game.extrasViewerSlot() == 1,
          "SYS 209 page arrow did not advance the grid");
    game.extrasViewerBack();
    check(!game.extrasViewerActive() && !game.vm().blocked(),
          "SYS 209 did not close back to the extras menu");

    // SYS 210 -> pmail.exe:0x42a4c0. KAIROKU MODE lists only unlocked slots
    // and returns the chosen slot (1..32) through its variable; cancelling
    // writes 0.
    const Pmail::Arg recallVar = {uint32_t(1u << 16) | 0x2389};
    game.vm().setVar(0x2389, 0);
    call(210, {recallVar});
    check(game.extrasViewerActive() &&
              game.extrasViewerMode() == ViewerMode::Recollection &&
              game.extrasViewerCategory() == 0 && game.extrasViewerSlot() == 1,
          "SYS 210 did not open KAIROKU MODE on the unlocked entry");
    auto recallCells = game.extrasViewerCells();
    check(recallCells[0] == "open00" && recallCells[1].empty(),
          "SYS 210 did not filter the recollection list by unlock state");
    check(game.extrasViewerActivate() && !game.extrasViewerActive() &&
              game.vm().var(0x2389) == 1 && !game.vm().blocked(),
          "SYS 210 did not return the selected recollection slot");
    call(210, {recallVar});
    check(game.extrasViewerSlot() == 1,
          "SYS 210 did not preselect the slot from its variable");
    game.extrasViewerBack();
    check(!game.extrasViewerActive() && game.vm().var(0x2389) == 0,
          "SYS 210 did not clear the slot variable on cancel");

    // SYS 208 -> pmail.exe:0x41d810. MUSIC MODE keeps a 24-row list, transport
    // controls, repeat flag and routes playback through the SYS 112 BGM slot.
    game.musicTracks = [] {
      return std::vector<std::string>{"sound/mm01", "sound/mm02", "sound/mm03"};
    };
    std::string playedTrack;
    bool stoppedMusic = false;
    game.playMusic = [&](const std::string &id) { playedTrack = id; };
    game.stopMusic = [&] { stoppedMusic = true; };
    call(208, {});
    check(game.extrasViewerActive() &&
              game.extrasViewerMode() == ViewerMode::Music &&
              game.extrasViewerTrackCount() == 3 &&
              game.extrasViewerTrack() == 1,
          "SYS 208 did not open MUSIC MODE with the host catalog");
    game.extrasViewerTrackBy(1);
    check(game.extrasViewerActivate() && playedTrack == "sound/mm02",
          "SYS 208 did not route playback through the BGM bus");
    game.extrasViewerToggleRepeat();
    check(game.extrasViewerRepeat(), "SYS 208 repeat toggle did not change");
    game.extrasViewerStopMusic();
    game.extrasViewerBack();
    check(!game.extrasViewerActive() && stoppedMusic &&
              !game.extrasViewerRepeat() && !game.vm().blocked(),
          "SYS 208 did not stop playback and close MUSIC MODE");

    game.setPlayerNames("Alice", "Bob");
    check(game.expandPlayerNames("$NO and $HO / $NO") ==
              "Alice and Bob / Alice",
          "Chosen player names were not substituted");
    // Earlier in the test all ten native text slots were filled. A script
    // scene change releases them before the next positioned text is shown.
    call(48, {string(6)});
    call(52, {string(5), integer(1), integer(2), integer(50), integer(30),
              integer(16), integer(8)});
    check(!game.computerText().empty() &&
              game.computerText().back().text == "Alice meets Bob",
          "SYS 52 did not resolve the player's chosen name");
    call(4, {string(5)});
    check(game.waitingForClick() && game.message() == "Alice meets Bob",
          "Dialogue did not expand customized $NO / $HO");
    call(160, {integer(1), string(5)});
    check(game.hasComputerOverlay() &&
              game.chatHistory().back().text == "Alice meets Bob",
          "Chat customization did not expand player names");
    call(48, {string(6)});
    check(!game.hasComputerOverlay() && game.computerText().empty() &&
              game.chatHistory().empty() && game.computerClock().empty(),
          "Changing the scene retained previous computer overlays");

    call(160, {integer(2), string(3)});
    check(game.hasComputerOverlay(), "Chat overlay did not activate");
    call(69, {integer(0), integer(0), integer(0)});
    check(!game.hasComputerOverlay(),
          "Clearing the scene retained computer overlays");

    // The original SYS 50 stores 25 icon-slot positions.
    call(50, {integer(24), integer(335), integer(188)});
    call(50, {integer(25), integer(10), integer(20)}); // ignored: > 24
    const auto icons = game.computerIcons();
    check(icons[24].active && icons[24].x == 335 && icons[24].y == 188 &&
              !icons[23].active,
          "SYS 50 did not preserve original icon-slot bounds");

    // The two text families use independent native renderers in the EXE.
    call(176, {integer(1), string(5)});
    call(178, {integer(2), string(3)});
    call(192, {integer(3), string(5)});
    call(194, {integer(4), string(4)});
    check(game.chatPanel().entries.size() == 2 &&
              game.chatPanel().entries.front().text == "Alice meets Bob",
          "SYS 176/178 chat-family stream is missing");
    check(game.mailPanel().entries.size() == 2 &&
              game.mailPanel().entries.front().text == "Alice meets Bob",
          "SYS 192/194 mail-family stream is missing");
    call(179, {integer(1), integer(2), integer(3), integer(4), integer(5),
               integer(6), integer(7)});
    call(195, {integer(13)});
    check(game.chatPanel().params[6] == 7 && game.mailPanel().selection == 13,
          "SYS 179/195 did not retain original native numeric parameters");
    const auto saved = game.serialize();
    check(saved.find("PMAIL-GAME-5") != std::string::npos &&
              saved.find("Alice") != std::string::npos,
          "V5 save did not include panel state and player customization");
    call(177, {});
    check(!game.chatPanel().active && game.chatPanel().entries.empty() &&
              game.mailPanel().entries.size() == 2,
          "SYS 177 reset unexpectedly affected the mail-family panel");
    // Native 0x417d80 keeps at most 0x3c (60) records and shifts the oldest
    // one out before appending, unlike the former 64-entry port limit.
    for (int i = 0; i < 65; ++i)
      call(178, {integer(1), string(1)});
    check(Kotonoha::PmailPort::PmailGame::ComputerPanelLimit == 60 &&
              game.chatPanel().entries.size() == 60,
          "SYS 178 panel history must retain the newest 60 entries");
    call(193, {});
    check(!game.mailPanel().active && game.mailPanel().entries.empty(),
          "SYS 193 did not reset the mail-family state");
    call(48, {string(6)});
    check(!game.computerIcons()[24].active && !game.hasComputerOverlay(),
          "Scene changes retained native computer icon/panel state");
    std::vector<std::string> messages;
    game.trace = [&](const std::string &message) {
      messages.push_back(message);
    };
    // Missing script objects must not create an unmatched return frame.
    const auto beforeScript = game.currentScript();
    call(16, {string(6)});
    check(game.currentScript() == beforeScript,
          "Missing scene script changed the active VM object");
    check(!messages.empty() &&
              messages.back().find("Missing PMAIL script") != std::string::npos,
          "Missing script objects were silently ignored");

    // SYS 7 (interactive choice menu), SYS 4 (first string only), SYS 21
    // (debug trace), SYS 214/211 (ending vignettes and staff roll), SYS 64(7)
    // (hiro17.DIB tall CG pan).
    game.vm().lines().push_back({3, 3});
    game.vm().lines().push_back({3, 4});
    const Pmail::Arg choiceVar = {uint32_t(1u << 16) | 0x34e4};
    std::string chosenBacklog;
    game.onChoiceSelected = [&](const std::string &text) {
      chosenBacklog = text;
    };
    call(7, {choiceVar});
    check(game.choosing() &&
              game.choices() == std::vector<std::string>({"one", "two"}) &&
              game.vm().lines().empty(),
          "SYS 7 did not populate choice options from queued lines");
    check(game.choose(1) && !game.choosing() && game.vm().var(0x34e4) == 2 &&
              chosenBacklog == "two",
          "SYS 7 did not write 1-based choice index or record backlog");
    call(2, {});
    call(4, {integer(1), string(3), string(7), string(8)});
    check(game.waitingForClick() && game.message() == "one",
          "SYS 4 concatenated trailing metadata string arguments");
    game.click();
    call(21, {string(0)});
    check(!game.waitingForClick() && game.unhandledNativeIds().empty(),
          "SYS 21 blocked or was marked unhandled");
    call(214, {integer(3), string(7), string(0), string(0)});
    call(214, {integer(3), string(8), string(0), string(0)});
    check(game.endingVignettes(3) ==
              std::vector<std::string>({"etc00", "pm01"}),
          "SYS 214 overwrote per-category ending vignette list");
    call(211, {integer(3), integer(23)});
    check(game.background() == "END03_03" && !game.vm().blocked(),
          "SYS 211 non-interactive staff roll did not set ending card");
    call(64, {integer(7), integer(0), integer(3)});
    check(game.background() == "hiro17" && game.lastTransition().type == 7,
          "SYS 64(7) did not select hiro17 tall CG");

    // Unrecognized native IDs are recorded once per ID, not silently
    // treated as confirmed engine behavior.
    call(96, {});  // handled by the audio callback, not an unknown native
    call(112, {}); // background music is also owned by PmailAudio
    call(250, {}); // explicitly unknown opcode for coverage diagnostics
    call(250, {});
    const auto unknown = game.unhandledNativeIds();
    check(unknown == std::vector<int>({250}),
          "Unhandled command coverage does not track unique IDs");
    int count = 0;
    for (const auto &message : messages)
      if (message.find("Unhandled PMAIL SYS 250") != std::string::npos)
        ++count;
    check(count == 1, "Unknown native syscall warnings were duplicated");
    for (const int id : {96, 97, 98, 99, 100, 112, 115, 116, 128, 132, 137})
      check(Kotonoha::PmailPort::PmailAudio::HandlesSyscall(id),
            "Audio subsystem must handle the documented audio native IDs");
    for (const int id : {17, 18, 19, 20, 24, 32, 34, 65, 212, 216, 250})
      check(!Kotonoha::PmailPort::PmailAudio::HandlesSyscall(id),
            "Non-audio calls must never be sent to the audio decoder");

    // Only genuinely unidentified opcodes remain in the diagnostic ledger.
    // 17, 18, 19, 20, 24, 25, 32, 34, 65, 212, 216 and 137 now have recovered
    // native semantics.
    for (const int id : {251}) {
      call(id, {integer(id), string(5)});
      call(id, {integer(id), string(5)});
    }
    const auto sites = game.unhandledNativeSites();
    const auto expected = std::vector<int>({250, 251});
    check(game.unhandledNativeIds() == expected,
          "Missing native commands were accidentally marked as supported");
    check(sites.size() == expected.size(),
          "Unsupported call-site ledger failed to index all native IDs");
    for (const auto &site : sites) {
      check(site.hits == 2 && site.pc == 0,
            "Call-site counter did not coalesce repeat invocations");
      if (site.id == 250)
        check(site.signature.find("argc=0") != std::string::npos,
              "Zero-argument native signature changed");
      else
        check(
            site.signature.find("argc=2") != std::string::npos &&
                site.signature.find("strref:3:5") != std::string::npos,
            "Unsupported calls must retain their arguments by type/reference");
      check(site.signature.find("$NO") == std::string::npos &&
                site.signature.find("$HO") == std::string::npos,
            "Native evidence must never copy story or player-name strings");
    }
    int warnings = 0;
    for (const auto &line : messages)
      if (line.find("Unhandled PMAIL SYS ") != std::string::npos)
        ++warnings;
    check(warnings == int(expected.size()),
          "Duplicate unknown-native warnings were not suppressed");

    std::cout << "Pure Mail computer UI, player names, scene transitions"
                 " and save format passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
