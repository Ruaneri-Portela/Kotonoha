#include <Kotonoha/routing/SchoolDaysFlowController.hpp>

#include <cstdio>

namespace Kotonoha {

bool SchoolDaysFlowController::Fail(Kotonoha_KtrfError* error,
                                    Kotonoha_KtrfErrorCode code,
                                    const char* message) {
    if (error != nullptr) {
        error->code = code;
        std::snprintf(error->message, sizeof(error->message), "%s",
                      message != nullptr ? message :
                                           "School Days outer-flow error");
    }
    return false;
}

void SchoolDaysFlowController::Reset() {
    state = State::Gameplay;
    pending = {};
}

bool SchoolDaysFlowController::ObserveStep(
    const SchoolDaysKtrfGameplayBridge::StepResult& step,
    Kotonoha_KtrfError* error) {
    if (error != nullptr) {
        error->code = KOTONOHA_KTRF_OK;
        error->message[0] = '\0';
    }
    if (state != State::Gameplay) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days outer flow already has a pending operation");
    }

    if (step.kind == SchoolDaysKtrfGameplayBridge::StepKind::PendingHandoff) {
        pending.terminal = false;
        pending.transitionIndex = step.transitionIndex;
        state = State::DeferredHandoff;
        return true;
    }
    if (step.kind == SchoolDaysKtrfGameplayBridge::StepKind::Terminal) {
        pending.terminal = true;
        pending.transitionIndex = step.transitionIndex;
        state = State::DeferredTerminal;
        return true;
    }

    // Ordinary gameplay results do not alter outer-flow ownership.
    return true;
}

bool SchoolDaysFlowController::ConsumeDeferredSignal(
    Kotonoha_KtrfError* error) {
    if (state == State::DeferredHandoff) {
        state = State::HandoffPrompt;
        return true;
    }
    if (state == State::DeferredTerminal) {
        state = State::TerminalPrompt;
        return true;
    }
    return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                "School Days outer flow has no deferred signal to consume");
}

bool SchoolDaysFlowController::SubmitSaveFeelResult(
    SaveFeelResult result, Action* action, Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days outer-flow action output is null");
    }
    *action = Action::None;
    if (state != State::HandoffPrompt && state != State::TerminalPrompt) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days SaveFeel result arrived outside its prompt");
    }
    if (result == SaveFeelResult::Skip) {
        state = pending.terminal ? State::TerminalContinuePending
                                 : State::HandoffContinuePending;
        *action = Action::ContinueHandoff;
        return true;
    }
    if (result == SaveFeelResult::OpenSave) {
        state = State::SaveLoad;
        *action = Action::EnterSaveLoad;
        return true;
    }
    return Fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                "School Days SaveFeel returned an unsupported result code");
}

bool SchoolDaysFlowController::SubmitSaveLoadResult(
    SaveLoadResult result, Action* action, Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days SaveLoad action output is null");
    }
    *action = Action::None;
    if (state != State::SaveLoad) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days SaveLoad result arrived outside SaveLoad");
    }
    if (result == SaveLoadResult::StayOpen) {
        // Successful save does not exit the view.
        return true;
    }
    if (result == SaveLoadResult::Close) {
        // The original outer state machine continues directly; it does not
        // recreate mode-10 SaveFeel. Move straight to a non-visual continuation
        // state so CompleteHandoff cannot be called while SaveLoad is still open.
        state = pending.terminal ? State::TerminalContinuePending
                                 : State::HandoffContinuePending;
        *action = Action::ContinueHandoff;
        return true;
    }
    return Fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                "School Days SaveLoad returned an unsupported result code");
}

bool SchoolDaysFlowController::EnterGameplaySave(
    Action* action, Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days Gameplay SAVE action output is null");
    }
    *action = Action::None;
    if (state != State::Gameplay) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days Gameplay SAVE was requested outside Gameplay");
    }
    pending = {};
    state = State::GameplaySave;
    *action = Action::EnterSaveLoad;
    return true;
}

bool SchoolDaysFlowController::EnterGameplayLoad(
    Action* action, Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days Gameplay LOAD action output is null");
    }
    *action = Action::None;
    if (state != State::Gameplay) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days Gameplay LOAD was requested outside Gameplay");
    }
    pending = {};
    state = State::GameplayLoad;
    *action = Action::EnterLoadSaveLoad;
    return true;
}

bool SchoolDaysFlowController::EnterGameplayConfig(
    Action* action, Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days Gameplay Config action output is null");
    }
    *action = Action::None;
    if (state != State::Gameplay) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days Gameplay Config was requested outside Gameplay");
    }
    pending = {};
    state = State::GameplayConfig;
    *action = Action::EnterConfig;
    return true;
}

bool SchoolDaysFlowController::EnterGameplayExit(
    Action* action, Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days Gameplay MainMenu action output is null");
    }
    *action = Action::None;
    if (state != State::Gameplay) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days Gameplay MainMenu was requested outside Gameplay");
    }
    pending = {};
    state = State::GameplayExit;
    *action = Action::EnterExit;
    return true;
}

bool SchoolDaysFlowController::EnterGameplayRouteMap(
    Action* action, Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days Gameplay RouteMap action output is null");
    }
    *action = Action::None;
    if (state != State::GameplayLoad) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days Gameplay RouteMap was requested outside Gameplay LOAD");
    }
    state = State::GameplayRouteMap;
    *action = Action::EnterRouteMap;
    return true;
}

bool SchoolDaysFlowController::ReturnGameplayRouteMapToLoad(
    Action* action, Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days Gameplay RouteMap return action output is null");
    }
    *action = Action::None;
    if (state != State::GameplayRouteMap) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days Gameplay RouteMap return arrived outside RouteMap");
    }
    state = State::GameplayLoad;
    *action = Action::EnterLoadSaveLoad;
    return true;
}

bool SchoolDaysFlowController::CloseGameplayModal(
    Action* action, Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days Gameplay modal close action output is null");
    }
    *action = Action::None;
    if (state != State::GameplaySave && state != State::GameplayLoad &&
        state != State::GameplayConfig && state != State::GameplayExit) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days Gameplay modal close arrived outside a Gameplay modal");
    }
    state = State::Gameplay;
    pending = {};
    return true;
}

bool SchoolDaysFlowController::SubmitGameplayExit(
    bool confirm, Action* action, Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days Gameplay MainMenu result action output is null");
    }
    *action = Action::None;
    if (state != State::GameplayExit) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days Gameplay MainMenu result arrived outside ExitView");
    }
    pending = {};
    if (!confirm) {
        state = State::Gameplay;
        return true;
    }
    state = State::Title;
    *action = Action::EnterTitle;
    return true;
}

bool SchoolDaysFlowController::EnterTitleLoad(
    Action* action, Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days Title LOAD action output is null");
    }
    *action = Action::None;
    if (state != State::Title) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days Title LOAD was requested outside TitleView");
    }
    state = State::TitleLoad;
    *action = Action::EnterLoadSaveLoad;
    return true;
}

bool SchoolDaysFlowController::CancelTitleLoad(
    Action* action, Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days Title LOAD cancel action output is null");
    }
    *action = Action::None;
    if (state != State::TitleLoad) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days Title LOAD cancel arrived outside LOAD view");
    }
    state = State::Title;
    *action = Action::EnterTitle;
    return true;
}

bool SchoolDaysFlowController::EnterTitleRouteMap(
    Action* action, Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days RouteMap action output is null");
    }
    *action = Action::None;
    if (state != State::TitleLoad) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days RouteMap was requested outside Title LOAD");
    }
    state = State::TitleRouteMap;
    *action = Action::EnterRouteMap;
    return true;
}

bool SchoolDaysFlowController::ReturnTitleRouteMapToLoad(
    Action* action, Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days RouteMap return action output is null");
    }
    *action = Action::None;
    if (state != State::TitleRouteMap) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days RouteMap return arrived outside RouteMap");
    }
    state = State::TitleLoad;
    *action = Action::EnterLoadSaveLoad;
    return true;
}

bool SchoolDaysFlowController::LeaveRouteMapForGameplay(
    Kotonoha_KtrfError* error) {
    if (state != State::TitleRouteMap && state != State::GameplayRouteMap) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days RouteMap cannot enter gameplay from this state");
    }
    Reset();
    return true;
}

bool SchoolDaysFlowController::LeaveTitleForGameplay(
    Kotonoha_KtrfError* error) {
    if (state != State::Title && state != State::TitleLoad &&
        state != State::TitleRouteMap) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days frontend cannot enter gameplay from this state");
    }
    Reset();
    return true;
}

bool SchoolDaysFlowController::EnterStartupTitle(
    Action* action, Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days startup Title action output is null");
    }
    *action = Action::None;
    if (state != State::Gameplay) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days startup Title requires idle gameplay state");
    }
    pending = {};
    state = State::Title;
    *action = Action::EnterTitle;
    return true;
}

bool SchoolDaysFlowController::EnterTitleConfig(
    Action* action, Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days Config action output is null");
    }
    *action = Action::None;
    if (state != State::Title) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days Config was requested outside TitleView");
    }
    state = State::TitleConfig;
    *action = Action::EnterConfig;
    return true;
}

bool SchoolDaysFlowController::CloseTitleConfig(
    Action* action, Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days Config close action output is null");
    }
    *action = Action::None;
    if (state != State::TitleConfig) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days Config close arrived outside ConfigMenu");
    }
    state = State::Title;
    *action = Action::EnterTitle;
    return true;
}

bool SchoolDaysFlowController::EnterTitleExit(
    Action* action, Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days Exit action output is null");
    }
    *action = Action::None;
    if (state != State::Title) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days Exit was requested outside TitleView");
    }
    state = State::TitleExit;
    *action = Action::EnterExit;
    return true;
}

bool SchoolDaysFlowController::SubmitTitleExit(
    bool confirm, Action* action, Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days Exit result action output is null");
    }
    *action = Action::None;
    if (state != State::TitleExit) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days Exit result arrived outside ExitView");
    }
    if (confirm) {
        *action = Action::ExitApplication;
        return true;
    }
    state = State::Title;
    *action = Action::EnterTitle;
    return true;
}

bool SchoolDaysFlowController::CompleteHandoff(
    const SchoolDaysKtrfGameplayBridge::StepResult& step, Action* action,
    Kotonoha_KtrfError* error) {
    if (action == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days outer-flow action output is null");
    }
    *action = Action::None;
    if (state != State::HandoffContinuePending &&
        state != State::TerminalContinuePending) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days outer flow has no completable handoff");
    }

    if (pending.terminal) {
        if (step.kind != SchoolDaysKtrfGameplayBridge::StepKind::Terminal) {
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days terminal flow received a non-terminal bridge result");
        }
        // Authenticated top-level unwind selects mode 2 when the two blocking
        // getters are zero and the TitleView/SaveLoad sentinel remains -1;
        // SystemInit(2) is MENU::TitleView.  Normal title/load/save flow keeps
        // that sentinel at -1, so a completed terminal requests TitleView here.
        state = State::Title;
        pending = {};
        *action = Action::EnterTitle;
        return true;
    }

    if (step.kind != SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days handoff did not produce a scene swap");
    }
    Reset();
    return true;
}

} // namespace Kotonoha
