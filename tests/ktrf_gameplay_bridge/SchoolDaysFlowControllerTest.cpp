#include <Kotonoha/routing/SchoolDaysFlowController.hpp>

#include <cstdlib>
#include <iostream>

namespace {

[[noreturn]] void Fail(const char* message) {
    std::cerr << "SchoolDaysFlowControllerTest: " << message << '\n';
    std::exit(2);
}

void Check(bool condition, const char* message) {
    if (!condition) Fail(message);
}

} // namespace

int main() {
    using Bridge = Kotonoha::SchoolDaysKtrfGameplayBridge;
    using Flow = Kotonoha::SchoolDaysFlowController;

    Kotonoha_KtrfError error{};
    Flow flow;
    Bridge::StepResult step;

    // FILM::MenuBar opens SAVE/LOAD/Config directly from ordinary Gameplay.
    // These modal states must return to the same Gameplay state without
    // borrowing callback_38 handoff semantics.
    Flow gameplayMenuFlow;
    Flow::Action gameplayMenuAction = Flow::Action::None;
    Check(gameplayMenuFlow.EnterGameplaySave(&gameplayMenuAction, &error) &&
              gameplayMenuAction == Flow::Action::EnterSaveLoad &&
              gameplayMenuFlow.CurrentState() == Flow::State::GameplaySave,
          "Gameplay SAVE did not enter independent SaveLoad state");
    Check(gameplayMenuFlow.CloseGameplayModal(&gameplayMenuAction, &error) &&
              gameplayMenuAction == Flow::Action::None &&
              gameplayMenuFlow.CurrentState() == Flow::State::Gameplay,
          "Gameplay SAVE close did not return to Gameplay");
    Check(gameplayMenuFlow.EnterGameplayLoad(&gameplayMenuAction, &error) &&
              gameplayMenuAction == Flow::Action::EnterLoadSaveLoad &&
              gameplayMenuFlow.CurrentState() == Flow::State::GameplayLoad,
          "Gameplay LOAD did not enter independent SaveLoad state");
    Check(gameplayMenuFlow.CloseGameplayModal(&gameplayMenuAction, &error) &&
              gameplayMenuFlow.CurrentState() == Flow::State::Gameplay,
          "Gameplay LOAD close did not return to Gameplay");
    Check(gameplayMenuFlow.EnterGameplayLoad(&gameplayMenuAction, &error) &&
              gameplayMenuFlow.EnterGameplayRouteMap(&gameplayMenuAction, &error) &&
              gameplayMenuAction == Flow::Action::EnterRouteMap &&
              gameplayMenuFlow.CurrentState() == Flow::State::GameplayRouteMap,
          "Gameplay LOAD RouteMap did not enter its parented RouteMap state");
    Check(gameplayMenuFlow.ReturnGameplayRouteMapToLoad(&gameplayMenuAction,
                                                        &error) &&
              gameplayMenuAction == Flow::Action::EnterLoadSaveLoad &&
              gameplayMenuFlow.CurrentState() == Flow::State::GameplayLoad,
          "Gameplay RouteMap back did not return to Gameplay LOAD");
    Check(gameplayMenuFlow.EnterGameplayRouteMap(&gameplayMenuAction, &error) &&
              gameplayMenuFlow.LeaveRouteMapForGameplay(&error) &&
              gameplayMenuFlow.CurrentState() == Flow::State::Gameplay,
          "Gameplay RouteMap node load did not return ownership to Gameplay");
    Check(gameplayMenuFlow.EnterGameplayConfig(&gameplayMenuAction, &error) &&
              gameplayMenuAction == Flow::Action::EnterConfig &&
              gameplayMenuFlow.CurrentState() == Flow::State::GameplayConfig,
          "Gameplay Config did not enter independent Config state");
    Check(gameplayMenuFlow.CloseGameplayModal(&gameplayMenuAction, &error) &&
              gameplayMenuFlow.CurrentState() == Flow::State::Gameplay,
          "Gameplay Config close did not return to Gameplay");
    Check(gameplayMenuFlow.EnterGameplayExit(&gameplayMenuAction, &error) &&
              gameplayMenuAction == Flow::Action::EnterExit &&
              gameplayMenuFlow.CurrentState() == Flow::State::GameplayExit,
          "Gameplay MainMenu did not enter confirmation ExitView state");
    Check(gameplayMenuFlow.SubmitGameplayExit(false, &gameplayMenuAction, &error) &&
              gameplayMenuAction == Flow::Action::None &&
              gameplayMenuFlow.CurrentState() == Flow::State::Gameplay,
          "Gameplay MainMenu cancel did not resume Gameplay");
    Check(gameplayMenuFlow.EnterGameplayExit(&gameplayMenuAction, &error) &&
              gameplayMenuFlow.SubmitGameplayExit(true, &gameplayMenuAction, &error) &&
              gameplayMenuAction == Flow::Action::EnterTitle &&
              gameplayMenuFlow.CurrentState() == Flow::State::Title,
          "Gameplay MainMenu confirm did not request Title re-entry");

    // Fresh application startup is frontend-owned after GlobalFlag load. The
    // bridge may keep its first Gameplay pre-created, but outer presentation
    // must enter Title before it is allowed to run.
    Flow startupFlow;
    Flow::Action startupAction = Flow::Action::None;
    Check(startupFlow.EnterStartupTitle(&startupAction, &error) &&
              startupFlow.CurrentState() == Flow::State::Title &&
              startupAction == Flow::Action::EnterTitle,
          "startup did not enter authenticated Title frontend state");
    Check(startupFlow.LeaveTitleForGameplay(&error) &&
              startupFlow.CurrentState() == Flow::State::Gameplay,
          "startup Title could not hand ownership to gameplay");

    Flow frontendFlow;
    Check(frontendFlow.EnterStartupTitle(&startupAction, &error),
          "frontend fixture could not enter Title");
    Check(frontendFlow.EnterTitleConfig(&startupAction, &error) &&
              startupAction == Flow::Action::EnterConfig &&
              frontendFlow.CurrentState() == Flow::State::TitleConfig,
          "Title Config did not enter typed ConfigMenu state");
    Check(frontendFlow.CloseTitleConfig(&startupAction, &error) &&
              startupAction == Flow::Action::EnterTitle &&
              frontendFlow.CurrentState() == Flow::State::Title,
          "ConfigMenu close did not return to Title");
    Check(frontendFlow.EnterTitleExit(&startupAction, &error) &&
              startupAction == Flow::Action::EnterExit &&
              frontendFlow.CurrentState() == Flow::State::TitleExit,
          "Title Exit did not enter typed ExitView state");
    Check(frontendFlow.SubmitTitleExit(false, &startupAction, &error) &&
              startupAction == Flow::Action::EnterTitle &&
              frontendFlow.CurrentState() == Flow::State::Title,
          "ExitView cancel did not return to Title");
    Check(frontendFlow.EnterTitleExit(&startupAction, &error) &&
              frontendFlow.SubmitTitleExit(true, &startupAction, &error) &&
              startupAction == Flow::Action::ExitApplication &&
              frontendFlow.CurrentState() == Flow::State::TitleExit,
          "ExitView confirm did not expose typed application-exit action");

    step.kind = Bridge::StepKind::PendingHandoff;
    step.transitionIndex = 123u;
    Check(flow.ObserveStep(step, &error), "normal handoff observation failed");
    Check(flow.CurrentState() == Flow::State::DeferredHandoff &&
              !flow.IsPromptVisible() && flow.HasDeferredSignal() &&
              flow.Pending().transitionIndex == 123u &&
              !flow.Pending().terminal,
          "callback38 was not represented as a pure deferred signal");

    Check(flow.ConsumeDeferredSignal(&error), "deferred handoff consume failed");
    Check(flow.CurrentState() == Flow::State::HandoffPrompt &&
              flow.IsPromptVisible(),
          "normal handoff did not enter prompt on later tick");

    Flow::Action action = Flow::Action::None;
    Check(flow.SubmitSaveFeelResult(Flow::SaveFeelResult::OpenSave,
                                    &action, &error) &&
              action == Flow::Action::EnterSaveLoad &&
              flow.CurrentState() == Flow::State::SaveLoad,
          "SaveFeel result 4 did not enter SaveLoad SAVE flow");

    Check(flow.SubmitSaveLoadResult(Flow::SaveLoadResult::StayOpen,
                                    &action, &error) &&
              action == Flow::Action::None &&
              flow.CurrentState() == Flow::State::SaveLoad,
          "successful SaveLoad result did not keep SaveLoad open");
    Bridge::StepResult prematureSwap;
    prematureSwap.kind = Bridge::StepKind::SwappedScene;
    Check(!flow.CompleteHandoff(prematureSwap, &action, &error) &&
              flow.CurrentState() == Flow::State::SaveLoad,
          "open SaveLoad incorrectly allowed handoff completion");
    Check(flow.SubmitSaveLoadResult(Flow::SaveLoadResult::Close,
                                    &action, &error) &&
              action == Flow::Action::ContinueHandoff &&
              flow.CurrentState() == Flow::State::HandoffContinuePending,
          "SaveLoad close did not continue outer flow directly");

    Bridge::StepResult savedSwap;
    savedSwap.kind = Bridge::StepKind::SwappedScene;
    Check(flow.CompleteHandoff(savedSwap, &action, &error) &&
              flow.CurrentState() == Flow::State::Gameplay,
          "SaveLoad close continuation did not return to gameplay");

    // Exercise the independently proven SaveFeel skip branch from a fresh
    // equivalent deferred signal.
    flow.Reset();
    Check(flow.ObserveStep(step, &error) && flow.ConsumeDeferredSignal(&error),
          "normal handoff re-entry for skip branch failed");

    Check(flow.SubmitSaveFeelResult(Flow::SaveFeelResult::Skip,
                                    &action, &error) &&
              action == Flow::Action::ContinueHandoff &&
              flow.CurrentState() == Flow::State::HandoffContinuePending,
          "SaveFeel result 0 did not request bridge continuation");
    Bridge::StepResult swapped;
    swapped.kind = Bridge::StepKind::SwappedScene;
    Check(flow.CompleteHandoff(swapped, &action, &error) &&
              action == Flow::Action::None &&
              flow.CurrentState() == Flow::State::Gameplay,
          "normal handoff completion did not return to gameplay");

    Bridge::StepResult terminal;
    terminal.kind = Bridge::StepKind::Terminal;
    terminal.transitionIndex = 999u;
    Check(flow.ObserveStep(terminal, &error), "terminal observation failed");
    Check(flow.CurrentState() == Flow::State::DeferredTerminal &&
              !flow.IsPromptVisible() && flow.IsTerminalFlow(),
          "terminal callback38 was not deferred");
    Check(flow.ConsumeDeferredSignal(&error) &&
              flow.CurrentState() == Flow::State::TerminalPrompt,
          "terminal deferred signal did not enter terminal prompt");
    Check(flow.SubmitSaveFeelResult(Flow::SaveFeelResult::Skip,
                                    &action, &error) &&
              action == Flow::Action::ContinueHandoff &&
              flow.CurrentState() == Flow::State::TerminalContinuePending,
          "terminal SaveFeel skip did not request bridge acknowledgement");
    Check(flow.CompleteHandoff(terminal, &action, &error) &&
              flow.CurrentState() == Flow::State::Title &&
              action == Flow::Action::EnterTitle && flow.IsTerminalFlow(),
          "terminal acknowledgement did not request authenticated TitleView flow");
    Check(!flow.Pending().terminal &&
              flow.Pending().transitionIndex == KOTONOHA_KTRF_NULL_INDEX,
          "completed terminal flow retained stale pending metadata");

    // Title item1 (CARREGAR) enters the LOAD flavour of MENU::SaveLoad without
    // borrowing the callback_38 SAVE state. Explicit close returns to Title.
    Check(flow.EnterTitleLoad(&action, &error) &&
              action == Flow::Action::EnterLoadSaveLoad &&
              flow.CurrentState() == Flow::State::TitleLoad,
          "Title LOAD did not enter its independent SaveLoad state");
    Check(flow.EnterTitleRouteMap(&action, &error) &&
              action == Flow::Action::EnterRouteMap &&
              flow.CurrentState() == Flow::State::TitleRouteMap,
          "Title LOAD RouteMap action did not enter modal RouteMap state");
    Flow routeLoadFlow = flow;
    Check(routeLoadFlow.LeaveTitleForGameplay(&error) &&
              routeLoadFlow.CurrentState() == Flow::State::Gameplay,
          "RouteMap result8 could not hand ownership to gameplay");
    Check(flow.ReturnTitleRouteMapToLoad(&action, &error) &&
              action == Flow::Action::EnterLoadSaveLoad &&
              flow.CurrentState() == Flow::State::TitleLoad,
          "RouteMap back did not return to Title LOAD");
    Check(flow.CancelTitleLoad(&action, &error) &&
              action == Flow::Action::EnterTitle &&
              flow.CurrentState() == Flow::State::Title,
          "Title LOAD cancel did not return to TitleView");
    Check(flow.LeaveTitleForGameplay(&error) &&
              flow.CurrentState() == Flow::State::Gameplay,
          "Title start did not return outer ownership to Gameplay");

    // The save branch converges on the same authenticated outer unwind only
    // after SaveLoad explicitly closes.  A successful save (result 1) must not
    // enter Title while the SaveLoad view remains open.
    flow.Reset();
    Check(flow.ObserveStep(terminal, &error) && flow.ConsumeDeferredSignal(&error),
          "terminal re-entry for SaveLoad branch failed");
    Check(flow.SubmitSaveFeelResult(Flow::SaveFeelResult::OpenSave,
                                    &action, &error) &&
              action == Flow::Action::EnterSaveLoad &&
              flow.CurrentState() == Flow::State::SaveLoad &&
              flow.IsTerminalFlow(),
          "terminal SaveFeel result 4 did not enter terminal SaveLoad flow");
    Check(flow.SubmitSaveLoadResult(Flow::SaveLoadResult::StayOpen,
                                    &action, &error) &&
              action == Flow::Action::None &&
              flow.CurrentState() == Flow::State::SaveLoad &&
              flow.IsTerminalFlow(),
          "terminal successful save incorrectly left SaveLoad");
    Check(flow.SubmitSaveLoadResult(Flow::SaveLoadResult::Close,
                                    &action, &error) &&
              action == Flow::Action::ContinueHandoff &&
              flow.CurrentState() == Flow::State::TerminalContinuePending,
          "terminal SaveLoad close did not request outer continuation");
    Check(flow.CompleteHandoff(terminal, &action, &error) &&
              flow.CurrentState() == Flow::State::Title &&
              action == Flow::Action::EnterTitle,
          "terminal SaveLoad branch did not converge on TitleView request");

    // No timer/countdown API exists here by design.  The unresolved visual
    // countdown cannot accidentally become a guessed routing decision.
    std::cout << "SCHOOL DAYS OUTER FLOW GATE 8 PASS\n"
              << "startup_title_before_gameplay=PASS\n"
              << "gameplay_menu_modal_states=PASS\n"
              << "title_config_exit_states=PASS\n"
              << "callback38_deferred_signal=PASS\n"
              << "prompt_later_tick=PASS\n"
              << "savefeel_code4_saveload=PASS\n"
              << "savefeel_code0_skip=PASS\n"
              << "saveload_code1_stays_open=PASS\n"
              << "saveload_code0_continues_outer=PASS\n"
              << "normal_handoff_continue=PASS\n"
              << "terminal_title_request=PASS\n"
              << "title_load_state_cancel=PASS\n"
              << "title_load_routemap_cycle=PASS\n"
              << "routemap_result8_gameplay_state=PASS\n"
              << "title_start_gameplay_state=PASS\n"
              << "terminal_saveload_title_request=PASS\n"
              << "countdown_owned_by_savefeel_model=PASS\n";
    return 0;
}
