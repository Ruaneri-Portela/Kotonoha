#pragma once

#include <cstdint>

#include <Kotonoha/routing/SchoolDaysKtrfGameplayBridge.hpp>

namespace Kotonoha {

// Outer School Days flow state.  This class deliberately does not implement
// SysMenu visuals or the authenticated SaveFeel timer; those belong to
// SchoolDaysSystemUi/SchoolDaysSaveFeelModel and feed this machine only the
// native result codes.  Flow models the proved host/RouteProc boundary:
// callback_38 produces a deferred signal, a later outer-loop pass consumes it,
// and the resulting system flow may then ask the gameplay bridge to continue.
// The post-terminal frontend selection is also modeled now that the
// authenticated mode gate to MENU::TitleView is closed; Title presentation
// itself belongs to the system-UI layer, not this state machine.
class SchoolDaysFlowController {
public:
    enum class State : uint8_t {
        Gameplay = 0,
        GameplaySave,
        GameplayLoad,
        GameplayConfig,
        GameplayExit,
        GameplayRouteMap,
        DeferredHandoff,
        HandoffPrompt,
        HandoffContinuePending,
        SaveLoad,
        DeferredTerminal,
        TerminalPrompt,
        TerminalContinuePending,
        Title,
        TitleLoad,
        TitleRouteMap,
        TitleConfig,
        TitleExit,
    };

    enum class Action : uint8_t {
        None = 0,
        ContinueHandoff,
        EnterSaveLoad,
        EnterTitle,
        EnterLoadSaveLoad,
        EnterRouteMap,
        EnterConfig,
        EnterExit,
        ExitApplication,
    };

    // Authenticated MENU::SaveFeelView result codes consumed by the EXE:
    // 0 skips SaveLoad, 4 enters SaveLoad in SAVE mode.
    enum class SaveFeelResult : int {
        Skip = 0,
        OpenSave = 4,
    };

    // MENU::SaveLoad result observed in the authenticated mode-4 handler:
    // successful save work returns 1 and keeps the view open; explicit
    // cancel/back reports 0 and exits to continue the outer flow.
    enum class SaveLoadResult : int {
        Close = 0,
        StayOpen = 1,
    };

    struct PendingInfo {
        bool terminal = false;
        uint32_t transitionIndex = KOTONOHA_KTRF_NULL_INDEX;
    };

    void Reset();

    // Observe a bridge result after routing.  PendingHandoff/Terminal queue a
    // deferred host signal only; they never synchronously open the prompt.
    bool ObserveStep(const SchoolDaysKtrfGameplayBridge::StepResult& step,
                     Kotonoha_KtrfError* error);

    // Consume a deferred callback_38 signal on a later outer-loop iteration.
    bool ConsumeDeferredSignal(Kotonoha_KtrfError* error);

    // Consume the exact SaveFeel result contract.  The UI/model owns manual
    // input and the native 30-second timeout; both produce one of these exact
    // result codes before entering the outer-flow machine.
    bool SubmitSaveFeelResult(SaveFeelResult result, Action* action,
                              Kotonoha_KtrfError* error);
    bool SubmitSaveLoadResult(SaveLoadResult result, Action* action,
                              Kotonoha_KtrfError* error);

    // FILM::MenuBar controls 10/11/13 open SysMenu directly from an ordinary
    // running scene. These modal lifetimes are independent from callback_38's
    // SaveFeel SaveLoad and from Title's LOAD/Config children.
    bool EnterGameplaySave(Action* action, Kotonoha_KtrfError* error);
    bool EnterGameplayLoad(Action* action, Kotonoha_KtrfError* error);
    bool EnterGameplayConfig(Action* action, Kotonoha_KtrfError* error);
    bool EnterGameplayExit(Action* action, Kotonoha_KtrfError* error);
    bool EnterGameplayRouteMap(Action* action, Kotonoha_KtrfError* error);
    bool ReturnGameplayRouteMapToLoad(Action* action,
                                      Kotonoha_KtrfError* error);
    bool CloseGameplayModal(Action* action, Kotonoha_KtrfError* error);
    bool SubmitGameplayExit(bool confirm, Action* action,
                            Kotonoha_KtrfError* error);

    // TitleView result 3 owns a distinct MENU::SaveLoad LOAD lifecycle.  It is
    // intentionally separate from the SAVE-mode handoff/terminal SaveLoad
    // state above because no callback_38/pending bridge transaction exists.
    bool EnterTitleLoad(Action* action, Kotonoha_KtrfError* error);
    bool CancelTitleLoad(Action* action, Kotonoha_KtrfError* error);
    bool EnterTitleRouteMap(Action* action, Kotonoha_KtrfError* error);
    bool ReturnTitleRouteMapToLoad(Action* action, Kotonoha_KtrfError* error);
    bool LeaveRouteMapForGameplay(Kotonoha_KtrfError* error);
    bool LeaveTitleForGameplay(Kotonoha_KtrfError* error);
    bool EnterStartupTitle(Action* action, Kotonoha_KtrfError* error);
    bool EnterTitleConfig(Action* action, Kotonoha_KtrfError* error);
    bool CloseTitleConfig(Action* action, Kotonoha_KtrfError* error);
    bool EnterTitleExit(Action* action, Kotonoha_KtrfError* error);
    bool SubmitTitleExit(bool confirm, Action* action,
                         Kotonoha_KtrfError* error);

    // Feed the result of the bridge action back into the outer-flow machine.
    bool CompleteHandoff(const SchoolDaysKtrfGameplayBridge::StepResult& step,
                         Action* action, Kotonoha_KtrfError* error);

    State CurrentState() const { return state; }
    const PendingInfo& Pending() const { return pending; }
    bool HasDeferredSignal() const {
        return state == State::DeferredHandoff ||
               state == State::DeferredTerminal;
    }
    bool IsPromptVisible() const {
        return state == State::HandoffPrompt ||
               state == State::TerminalPrompt;
    }
    bool IsTerminalFlow() const {
        if (pending.terminal && state != State::Gameplay) return true;
        return state == State::DeferredTerminal ||
               state == State::TerminalPrompt ||
               state == State::TerminalContinuePending ||
               state == State::Title;
    }

private:
    static bool Fail(Kotonoha_KtrfError* error,
                     Kotonoha_KtrfErrorCode code, const char* message);

    State state = State::Gameplay;
    PendingInfo pending{};
};

} // namespace Kotonoha
