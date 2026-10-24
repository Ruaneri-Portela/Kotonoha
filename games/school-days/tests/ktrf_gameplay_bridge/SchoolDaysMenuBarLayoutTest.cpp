#include <Kotonoha/routing/SchoolDaysMenuBarPresenter.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

using Presenter = Kotonoha::SchoolDaysMenuBarPresenter;
using Variant = Presenter::Variant;

void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void Near(float value, float expected, const char* message) {
    if (std::fabs(value - expected) > 0.01f) {
        throw std::runtime_error(message);
    }
}

void CheckLayout(Variant variant, int width, int height, float scale,
                 float barWidth, float barHeight) {
    const auto layout = Presenter::LayoutForVariant(variant);
    Check(layout.canvasWidth == width && layout.canvasHeight == height,
          "MenuBar logical canvas drifted");
    Near(layout.layoutScale, scale, "MenuBar authored layout scale drifted");
    Near(layout.barWidth, barWidth, "MenuBar logical bar width drifted");
    Near(layout.barHeight, barHeight, "MenuBar logical bar height drifted");
}

void CheckBackLogRect(Variant variant, float x, float y, float w, float h) {
    const SDL_FRect r = Presenter::BackLogControlRect(variant);
    Near(r.x, x, "MenuBar BackLog x drifted");
    Near(r.y, y, "MenuBar BackLog y drifted");
    Near(r.w, w, "MenuBar BackLog width drifted");
    Near(r.h, h, "MenuBar BackLog height drifted");
}

void CheckControlRect(int control, Variant variant, float x, float y, float w,
                      float h) {
    const SDL_FRect r = Presenter::ControlRect(control, variant);
    Near(r.x, x, "MenuBar control x drifted");
    Near(r.y, y, "MenuBar control y drifted");
    Near(r.w, w, "MenuBar control width drifted");
    Near(r.h, h, "MenuBar control height drifted");
}

} // namespace

int main() {
    try {
        CheckLayout(Variant::Standard, 800, 600, 1.0f, 800.0f, 75.0f);
        CheckLayout(Variant::Wide, 800, 450, 1.0f, 800.0f, 75.0f);
        CheckLayout(Variant::WideFull, 1280, 720, 1.6f, 1280.0f, 120.0f);
        CheckLayout(Variant::WideNote, 1024, 576, 1.28f, 1024.0f, 96.0f);
        // SysMenu stores CMAP-1 in this+0xD4. Internal control12 is therefore
        // raw CMAP value13; its case pushes ReMenu(3), the BackLog view.
        CheckBackLogRect(Variant::Standard, 603.0f, 26.0f, 61.0f, 27.0f);
        CheckBackLogRect(Variant::Wide, 603.0f, 26.0f, 61.0f, 27.0f);
        CheckBackLogRect(Variant::WideFull, 965.0f, 41.0f, 97.0f, 44.0f);
        CheckBackLogRect(Variant::WideNote, 772.0f, 33.0f, 78.0f, 35.0f);

        // SysMenuSDHQ v1.02 records 10..14 are the five ReMenu/top-level
        // controls: SAVE, LOAD, BackLog, Config, Main Menu.
        CheckControlRect(10, Variant::Standard, 475.0f, 26.0f, 61.0f, 27.0f);
        CheckControlRect(11, Variant::Standard, 539.0f, 26.0f, 61.0f, 27.0f);
        CheckControlRect(12, Variant::Standard, 603.0f, 26.0f, 61.0f, 27.0f);
        CheckControlRect(13, Variant::Standard, 667.0f, 26.0f, 61.0f, 27.0f);
        CheckControlRect(14, Variant::Standard, 731.0f, 26.0f, 61.0f, 27.0f);
        CheckControlRect(10, Variant::WideFull, 760.0f, 41.0f, 97.0f, 44.0f);
        CheckControlRect(14, Variant::WideNote, 936.0f, 33.0f, 78.0f,
                         35.0f);

        const auto saveChip = Presenter::ControlChip(10, Variant::Standard);
        Near(saveChip.source.x, 1.0f, "SAVE CHIP source x drifted");
        Near(saveChip.source.y, 29.0f, "SAVE CHIP source y drifted");
        Near(saveChip.source.w, 61.0f, "SAVE CHIP source width drifted");
        Near(saveChip.source.h, 27.0f, "SAVE CHIP source height drifted");
        const auto historyChip = Presenter::ControlChip(12, Variant::Standard);
        Near(historyChip.source.x, 125.0f, "BackLog CHIP source x drifted");
        Near(historyChip.source.y, 29.0f, "BackLog CHIP source y drifted");

        const auto normalSave =
            Presenter::NormalSystemControlChip(10, Variant::Standard);
        Near(normalSave.source.x, 1.0f, "SAVE normal CHIP source x drifted");
        Near(normalSave.source.y, 155.0f, "SAVE normal CHIP source y drifted");
        Near(normalSave.destination.x, 475.0f,
             "SAVE normal CHIP destination x drifted");
        const auto normalMain =
            Presenter::NormalSystemControlChip(14, Variant::Standard);
        Near(normalMain.source.x, 249.0f,
             "MainMenu normal CHIP source x drifted");
        Near(normalMain.source.y, 155.0f,
             "MainMenu normal CHIP source y drifted");
        const auto normalSaveFull =
            Presenter::NormalSystemControlChip(10, Variant::WideFull);
        Near(normalSaveFull.destination.x, 760.0f,
             "WideFull SAVE normal destination x drifted");
        Near(normalSaveFull.destination.y, 41.6f,
             "WideFull SAVE normal destination y drifted");

        Check(Presenter::HitTest(500.0f, 30.0f, Variant::Standard) == 10,
              "SAVE CMAP hit mismatch");
        Check(Presenter::HitTest(610.0f, 30.0f, Variant::Standard) == 12,
              "BackLog CMAP hit mismatch");
        Check(Presenter::HitTest(0.0f, 74.0f, Variant::Standard) == -1,
              "MenuBar background must not invent a control");

        for (int control = 0; control <= 3; ++control) {
            Check(Presenter::ControlAvailableInNormalGameplay(control),
                  "unconditional MenuBar control unexpectedly disabled");
        }
        for (int control = 10; control <= 14; ++control) {
            Check(Presenter::ControlAvailableInNormalGameplay(control),
                  "authenticated normal-Gameplay ReMenu control disabled");
        }
        Check(!Presenter::ControlAvailableInNormalGameplay(4),
              "SuperSkip-off control4 must remain unavailable");
        for (int control = 5; control <= 9; ++control) {
            Check(Presenter::ControlAvailableInNormalGameplay(control),
                  "ordinary Gameplay speed control unexpectedly disabled");
        }
        Check(Presenter::ControlAvailable(4, false, false, true, true),
              "SuperSkip-enabled control4 availability mismatch");
        Check(!Presenter::ControlAvailable(4, false, true, true, true) &&
                  !Presenter::ControlAvailable(4, true, false, true, true),
              "control4 host inhibit/transport-lock gate mismatch");
        Check(Presenter::ControlAvailable(5, false, false, true, false) &&
                  !Presenter::ControlAvailable(5, false, false, false, true) &&
                  !Presenter::ControlAvailable(5, true, false, true, true),
              "speed-row capability/transport-lock gate mismatch");
        Check(!Presenter::ControlAvailable(10, false, true, true, true) &&
                  Presenter::ControlAvailable(13, false, true, true, true),
              "system-control host inhibit classes mismatch");
        Check(!Presenter::ControlAvailableInNormalGameplay(15),
              "replay-latched controls must be disabled in normal Gameplay");
        Check(Presenter::ControlAvailable(15, false, false, true, false, true) &&
                  Presenter::ControlAvailable(24, false, false, true, false, true),
              "replay-latched controls did not follow host +0x98 predicate");

        // Ordinary SceneView HScene playback calls host +0xA4, which writes
        // host predicate +104=1 while leaving +98=0.  FUN_10023FB0 therefore
        // keeps transport/config/main-menu controls, but gates NextChoice,
        // Save/Load/BackLog and the PlayData-only repeat-opacity row.
        for (int control = 0; control <= 3; ++control) {
            Check(Presenter::ControlAvailable(control, false, true, true, true,
                                              false),
                  "ordinary replay transport control unexpectedly disabled");
        }
        Check(!Presenter::ControlAvailable(4, false, true, true, true, false),
              "ordinary replay NextChoice must follow +104 inhibit");
        for (int control = 5; control <= 9; ++control) {
            Check(Presenter::ControlAvailable(control, false, true, true, true,
                                              false),
                  "ordinary replay speed control unexpectedly disabled");
        }
        for (int control = 10; control <= 12; ++control) {
            Check(!Presenter::ControlAvailable(control, false, true, true, true,
                                               false),
                  "ordinary replay Save/Load/BackLog must follow +104 inhibit");
        }
        Check(Presenter::ControlAvailable(13, false, true, true, true, false) &&
                  Presenter::ControlAvailable(14, false, true, true, true, false),
              "ordinary replay Config/MainMenu controls must stay available");
        Check(!Presenter::ControlAvailable(15, false, true, true, true, false) &&
                  !Presenter::ControlAvailable(24, false, true, true, true, false),
              "ordinary replay must not expose PlayData repeat-opacity controls");

        // FUN_100262E0 classifier -> fixed records53..64.  Speeds share one
        // help sprite and all ten repeat-opacity cells share record64.
        const auto pauseHelp = Presenter::HoverHelpChip(1, Variant::Standard);
        const auto backHelp = Presenter::HoverHelpChip(2, Variant::Standard);
        const auto speedHelp5 = Presenter::HoverHelpChip(5, Variant::Standard);
        const auto speedHelp9 = Presenter::HoverHelpChip(9, Variant::Standard);
        const auto saveHelp = Presenter::HoverHelpChip(10, Variant::Standard);
        const auto repeatHelp15 = Presenter::HoverHelpChip(15, Variant::Standard);
        const auto repeatHelp24 = Presenter::HoverHelpChip(24, Variant::Standard);
        Near(pauseHelp.source.x, 1.0f, "pause hover-help source x drifted");
        Near(pauseHelp.source.y, 203.0f, "pause hover-help source y drifted");
        Near(backHelp.source.x, 252.0f, "back hover-help source x drifted");
        Near(speedHelp5.source.x, speedHelp9.source.x,
             "speed controls stopped sharing native hover-help record58");
        Near(speedHelp5.source.y, speedHelp9.source.y,
             "speed controls stopped sharing native hover-help source row");
        Near(saveHelp.source.x, 141.0f, "save hover-help source x drifted");
        Near(repeatHelp15.source.x, repeatHelp24.source.x,
             "repeat opacity controls stopped sharing native help record64");
        Near(repeatHelp15.source.y, 379.0f,
             "repeat opacity hover-help source y drifted");

        Check(Presenter::ReplayOpacityLevelForControl(15) == 0 &&
                  Presenter::ReplayOpacityLevelForControl(16) == 2 &&
                  Presenter::ReplayOpacityLevelForControl(24) == 10,
              "native repeat opacity level map drifted");
        Check(Presenter::ReplayOpacityAlphaForControl(15) == 0 &&
                  Presenter::ReplayOpacityAlphaForControl(16) == 50 &&
                  Presenter::ReplayOpacityAlphaForControl(24) == 250,
              "native repeat opacity alpha map drifted");
        const auto repeatOff = Presenter::FixedChipRecord(69, Variant::Standard);
        const auto repeatOn = Presenter::FixedChipRecord(70, Variant::Standard);
        const auto repeatSelector = Presenter::FixedChipRecord(71, Variant::Standard);
        Near(repeatOff.destination.x, 674.0f,
             "repeat gray strip destination x drifted");
        Near(repeatOff.destination.y, 4.0f,
             "repeat gray strip destination y drifted");
        Near(repeatOff.source.x, 322.0f,
             "repeat gray strip atlas source drifted");
        Near(repeatOn.source.x, 414.0f,
             "repeat cyan strip atlas source drifted");
        Near(repeatOn.source.y, 1.0f,
             "repeat cyan strip atlas row drifted");
        Near(repeatSelector.source.x, 1.0f,
             "repeat selector atlas source x drifted");
        Near(repeatSelector.source.y, 379.0f,
             "repeat selector atlas source y drifted");
        Near(Presenter::ControlRect(15, Variant::Standard).x, 676.0f,
             "repeat opacity first cell drifted");
        Near(Presenter::ControlRect(24, Variant::Standard).x, 784.0f,
             "repeat opacity last cell drifted");

        const auto speedEnabled = Presenter::FixedChipRecord(47, Variant::Standard);
        const auto speedDisabled = Presenter::FixedChipRecord(66, Variant::Standard);
        const auto superSkipEnabled = Presenter::FixedChipRecord(46, Variant::Standard);
        const auto superSkipDisabled = Presenter::FixedChipRecord(65, Variant::Standard);
        Near(speedEnabled.source.y, 127.0f, "enabled speed base record drifted");
        Near(speedDisabled.source.y, 290.0f, "disabled speed base record drifted");
        Near(superSkipEnabled.source.y, 127.0f,
             "enabled next-choice base record drifted");
        Near(superSkipDisabled.source.y, 290.0f,
             "disabled next-choice base record drifted");

        Check(Presenter::ActionForControl(10).kind ==
                  Presenter::ActionKind::OpenSave,
              "MenuBar SAVE action mismatch");
        Check(Presenter::ActionForControl(11).kind ==
                  Presenter::ActionKind::OpenLoad,
              "MenuBar LOAD action mismatch");
        Check(Presenter::ActionForControl(12).kind ==
                  Presenter::ActionKind::OpenBackLog,
              "MenuBar BackLog action mismatch");
        Check(Presenter::ActionForControl(13).kind ==
                  Presenter::ActionKind::OpenConfig,
              "MenuBar Config action mismatch");
        Check(Presenter::ActionForControl(14).kind ==
                  Presenter::ActionKind::OpenMainMenuConfirm,
              "MenuBar MainMenu confirmation action mismatch");
        Check(Presenter::ActionForControl(2).kind ==
                  Presenter::ActionKind::BackOrRestartScene,
              "MenuBar back/current-scene action mismatch");
        Check(Presenter::ActionForControl(15).kind ==
                  Presenter::ActionKind::SetReplayIndicatorOpacity &&
                  Presenter::ActionForControl(15).value == 0 &&
                  Presenter::ActionForControl(24).value == 10,
              "MenuBar repeat opacity action mismatch");

        Check(Presenter::FadeAlpha(0, true, 0) == 0,
              "MenuBar reveal start drifted");
        Check(Presenter::FadeAlpha(0, true, 150) == 127,
              "MenuBar 300ms reveal midpoint drifted");
        Check(Presenter::FadeAlpha(0, true, 300) == 255,
              "MenuBar reveal finish drifted");
        Check(Presenter::FadeAlpha(255, false, 0) == 255,
              "MenuBar hide start drifted");
        Check(Presenter::FadeAlpha(255, false, 500) == 127,
              "MenuBar 1000ms hide midpoint drifted");
        Check(Presenter::FadeAlpha(255, false, 1000) == 0,
              "MenuBar hide finish drifted");
        Check(Presenter::FadeAlpha(100, true, 150) == 177,
              "MenuBar interrupted reveal interpolation drifted");
        Check(Presenter::FadeAlpha(100, false, 500) == 50,
              "MenuBar interrupted hide interpolation drifted");

        const auto standard = Presenter::LayoutForVariant(Variant::Standard);
        auto vp = Presenter::FitViewport(1280, 720, standard);
        Near(vp.scale, 1.2f, "Standard 720p scale mismatch");
        Near(vp.x, 160.0f, "Standard 720p pillarbox mismatch");
        Near(vp.y, 0.0f, "Standard 720p vertical placement mismatch");
        Near(standard.barWidth * vp.scale, 960.0f,
             "Standard 720p MenuBar width mismatch");
        Near(standard.barHeight * vp.scale, 90.0f,
             "Standard 720p MenuBar height mismatch");

        const auto wide = Presenter::LayoutForVariant(Variant::Wide);
        vp = Presenter::FitViewport(1280, 720, wide);
        Near(vp.scale, 1.6f, "Wide 720p scale mismatch");
        Near(vp.x, 0.0f, "Wide 720p unexpectedly pillarboxed");
        Near(wide.barWidth * vp.scale, 1280.0f,
             "Wide 720p MenuBar width mismatch");

        vp = Presenter::FitViewport(3440, 1440, wide);
        Near(vp.scale, 3.2f, "Ultrawide aspect-fit scale mismatch");
        Near(vp.x, 440.0f, "Ultrawide centered pillarbox mismatch");
        Near(vp.y, 0.0f, "Ultrawide vertical placement mismatch");
        Near(wide.barWidth * vp.scale, 2560.0f,
             "Ultrawide MenuBar stretched beyond 16:9 viewport");

        const auto wideFull = Presenter::LayoutForVariant(Variant::WideFull);
        vp = Presenter::FitViewport(3840, 2160, wideFull);
        Near(vp.scale, 3.0f, "WideFull 4K scale mismatch");
        Near(vp.x, 0.0f, "WideFull 4K horizontal placement mismatch");
        Near(wideFull.barHeight * vp.scale, 360.0f,
             "WideFull 4K MenuBar height mismatch");

        const auto wideNote = Presenter::LayoutForVariant(Variant::WideNote);
        vp = Presenter::FitViewport(2560, 1440, wideNote);
        Near(vp.scale, 2.5f, "WideNote 1440p scale mismatch");
        Near(vp.x, 0.0f, "WideNote 1440p horizontal placement mismatch");

        vp = Presenter::FitViewport(1366, 768, standard);
        Near(vp.scale, 1.28f, "Standard 1366x768 scale mismatch");
        Near(vp.x, 171.0f, "Standard 1366x768 pillarbox mismatch");

        vp = Presenter::FitViewport(0, 1080, wide);
        Check(vp.scale == 0.0f, "Invalid output did not fail closed");

        std::cout
            << "SCHOOL DAYS MENUBAR MULTIRES GATE PASS\n"
            << "logical_profiles=PASS\n"
            << "standard_4_3_pillarbox=PASS\n"
            << "wide_16_9_fill=PASS\n"
            << "widefull_4k=PASS\n"
            << "widenote_1440p=PASS\n"
            << "backlog_control_raw13=PASS\n"
            << "controls_0_24_geometry=PASS\n"
            << "system_controls_normal_records_48_52=PASS\n"
            << "normal_gameplay_availability=PASS\n"
            << "native_hover_help_records_53_64=PASS\n"
            << "replay_opacity_levels=PASS\n"
            << "replay_strip_records_69_71=PASS\n"
            << "transport_predicate_classes=PASS\n"
            << "speed_vs_superskip_bases=PASS\n"
            << "remenu_actions_10_14=PASS\n"
            << "ultrawide_aspect_fit=PASS\n"
            << "physical_resolution_independent=PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
