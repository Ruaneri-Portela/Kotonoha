#include <Kotonoha/Kotonoha.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfAppController.hpp>
#include <Kotonoha/routing/SchoolDaysPlayDataSession.hpp>
#include <Kotonoha/routing/SchoolDaysReplayPlaybackSession.hpp>
#include <Kotonoha/routing/SchoolDaysRouteMapModel.hpp>
#include <Kotonoha/routing/SchoolDaysRouteMapOracle.hpp>

#include <new>

namespace Kotonoha {

SchoolDaysKtrfAppController::~SchoolDaysKtrfAppController() {
    Close();
}

Gameplay* SchoolDaysKtrfAppController::CreateGameplay(
    const char* orsPath, Kotonoha_Game* context, void* userdata) {
    auto* self = static_cast<SchoolDaysKtrfAppController*>(userdata);
    if (self == nullptr || self->engine == nullptr || context == nullptr ||
        orsPath == nullptr || *orsPath == '\0') {
        return nullptr;
    }

    try {
        TTF_Font* preferredFont = nullptr;
        const auto fontIt = self->engine->fonts.find("ConcertOne-Regular");
        if (fontIt != self->engine->fonts.end()) {
            preferredFont = fontIt->second;
        }

        // The outgoing Gameplay may currently be paused only because its I/O
        // monitor is loading.  Capture the explicit MenuBar pause intent while
        // that owner is still alive; CreateAndSwap calls the factory before it
        // destroys the old Gameplay.  This prevents the shared effective
        // config.paused bit from turning a transient loading pause into a user
        // pause when the next physical ORS is created.
        const Gameplay* previous = self->bridge.CurrentGameplay();
        const bool inheritUserPause = previous != nullptr;
        const bool userPaused =
            inheritUserPause ? previous->UserPaused(context) : false;

        auto* gameplay = new Gameplay(orsPath, context, preferredFont);
        if (inheritUserPause) {
            gameplay->SetUserPaused(context, userPaused);
        }
        return gameplay;
    }
    catch (...) {
        return nullptr;
    }
}

void SchoolDaysKtrfAppController::DestroyGameplay(Gameplay* gameplay,
                                                   void* userdata) {
    auto* self = static_cast<SchoolDaysKtrfAppController*>(userdata);
    if (gameplay == nullptr) return;
    if (self != nullptr && self->engine != nullptr) {
        self->engine->DeleteGameplay(gameplay);
        return;
    }
    delete gameplay;
}

bool SchoolDaysKtrfAppController::ResolveReplayScene(
    const char* sceneKey, SchoolDaysKtrfScene* scene, void* userdata,
    Kotonoha_KtrfError* error) {
    auto* self = static_cast<SchoolDaysKtrfAppController*>(userdata);
    if (self == nullptr || sceneKey == nullptr || *sceneKey == '\0') {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days replay SceneKey resolver argument is invalid");
        }
        return false;
    }
    return self->bridge.Session().ResolveSceneKey(sceneKey, scene, error);
}

bool SchoolDaysKtrfAppController::BuildDebugSceneCatalog(
    Kotonoha_KtrfError* error) {
    debugScenes.clear();

    const auto* adapter = bridge.Session().Adapter();
    if (adapter == nullptr || adapter->document == nullptr) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days debug catalog has no bound adapter");
        }
        return false;
    }

    const Kotonoha_KtrfSection* nodes =
        Kotonoha_KtrfFindSection(adapter->document, "NODE");
    if (nodes == nullptr) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_MISSING_SECTION;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days debug catalog is missing NODE section");
        }
        return false;
    }

    debugScenes.reserve(nodes->item_count);
    for (uint32_t nodeIndex = 0; nodeIndex < nodes->item_count; ++nodeIndex) {
        Kotonoha_KtrfNodeView view{};
        if (!Kotonoha_KtrfGetNodeView(adapter, nodeIndex, &view, error)) {
            debugScenes.clear();
            return false;
        }
        if (view.kind != KOTONOHA_SDHQ_NODE_SCENE) continue;
        if (view.scene_key == nullptr || view.scene_key_length == 0u) {
            if (error != nullptr) {
                error->code = KOTONOHA_KTRF_ERROR_FORMAT;
                SDL_snprintf(error->message, sizeof(error->message),
                             "School Days debug scene %u has no scene key",
                             static_cast<unsigned>(nodeIndex));
            }
            debugScenes.clear();
            return false;
        }

        DebugSceneEntry entry;
        entry.nodeIndex = nodeIndex;
        entry.sceneKey.assign(view.scene_key, view.scene_key_length);
        debugScenes.push_back(std::move(entry));
    }

    if (debugScenes.empty()) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_FORMAT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days debug catalog contains no physical scenes");
        }
        return false;
    }

    return true;
}

bool SchoolDaysKtrfAppController::Open(
    const char* ktrfPath, const char* orsRoot, Kotonoha* requestedEngine,
    Kotonoha_Game* requestedContext, Kotonoha_KtrfError* error) {
    Close();

    if (requestedEngine == nullptr || requestedContext == nullptr) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days app controller engine/context is null");
        }
        return false;
    }

    engine = requestedEngine;
    gameContext = requestedContext;
    this->ktrfPath = ktrfPath != nullptr ? ktrfPath : "";
    this->orsRoot = orsRoot != nullptr ? orsRoot : "";

    SchoolDaysKtrfGameplayBridge::Factory factory;
    factory.create = &SchoolDaysKtrfAppController::CreateGameplay;
    factory.destroy = &SchoolDaysKtrfAppController::DestroyGameplay;
    factory.userdata = this;

    if (!bridge.Open(ktrfPath, orsRoot, gameContext, factory, error)) {
        engine = nullptr;
        gameContext = nullptr;
        this->ktrfPath.clear();
        this->orsRoot.clear();
        return false;
    }

    if (!BuildDebugSceneCatalog(error)) {
        Close();
        return false;
    }

    flow.Reset();
    gameContext->config.scene = 0;
    gameContext->config.next = false;
    gameContext->config.back = false;
    opened = true;

    SDL_Log("[KTRF-APP] enabled scene=%s ors=%s debug_scenes=%d",
            bridge.CurrentScene().sceneKey.c_str(),
            bridge.CurrentScene().orsPath.c_str(),
            static_cast<int>(debugScenes.size()));
    return true;
}

void SchoolDaysKtrfAppController::Close() {
    debugScenes.clear();
    flow.Reset();
    bridge.Close();
    opened = false;
    engine = nullptr;
    gameContext = nullptr;
    ktrfPath.clear();
    orsRoot.clear();
}

const SchoolDaysKtrfAppController::DebugSceneEntry*
SchoolDaysKtrfAppController::DebugSceneAt(std::size_t ordinal) const {
    if (ordinal >= debugScenes.size()) return nullptr;
    return &debugScenes[ordinal];
}

int SchoolDaysKtrfAppController::DebugCurrentSceneOrdinal() const {
    if (!opened) return -1;
    const uint32_t currentNode = bridge.CurrentScene().nodeIndex;
    for (std::size_t i = 0; i < debugScenes.size(); ++i) {
        if (debugScenes[i].nodeIndex == currentNode) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool SchoolDaysKtrfAppController::DebugJumpToScene(
    std::size_t ordinal, Kotonoha_KtrfError* error) {
    if (!opened || gameContext == nullptr || ordinal >= debugScenes.size()) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_RANGE;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days debug scene ordinal is out of range");
        }
        return false;
    }

    SchoolDaysKtrfGameplayBridge::StepResult step;
    if (!bridge.DebugJumpToNode(debugScenes[ordinal].nodeIndex, &step, error)) {
        return false;
    }

    gameContext->config.scene = 0;
    gameContext->config.next = false;
    gameContext->config.back = false;
    SDL_Log("[KTRF-DEBUG] jump ordinal=%d node=%u scene=%s ors=%s",
            static_cast<int>(ordinal),
            static_cast<unsigned>(step.scene.nodeIndex),
            step.scene.sceneKey.c_str(), step.scene.orsPath.c_str());
    return true;
}

bool SchoolDaysKtrfAppController::DebugJumpRelative(
    int delta, Kotonoha_KtrfError* error) {
    const int current = DebugCurrentSceneOrdinal();
    if (current < 0 || debugScenes.empty()) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_RANGE;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days current scene is not in debug catalog");
        }
        return false;
    }

    const long long target = static_cast<long long>(current) + delta;
    if (target < 0 || target >= static_cast<long long>(debugScenes.size())) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_RANGE;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days debug relative scene is out of range");
        }
        return false;
    }

    return DebugJumpToScene(static_cast<std::size_t>(target), error);
}

bool SchoolDaysKtrfAppController::ExecuteFlowAction(
    SchoolDaysFlowController::Action action, Kotonoha_KtrfError* error) {
    if (!opened) return false;
    if (action == SchoolDaysFlowController::Action::None ||
        action == SchoolDaysFlowController::Action::EnterSaveLoad ||
        action == SchoolDaysFlowController::Action::EnterLoadSaveLoad ||
        action == SchoolDaysFlowController::Action::EnterRouteMap ||
        action == SchoolDaysFlowController::Action::EnterConfig ||
        action == SchoolDaysFlowController::Action::EnterExit ||
        action == SchoolDaysFlowController::Action::ExitApplication) {
        return true;
    }
    if (action == SchoolDaysFlowController::Action::EnterTitle) {
        // Flow owns the semantic edge; Main/SchoolDaysSystemUi owns the
        // authenticated TitleView presentation while this action keeps the
        // controller in the Title outer state.
        SDL_Log("[KTRF-FLOW] authenticated TitleView requested");
        return true;
    }
    if (action != SchoolDaysFlowController::Action::ContinueHandoff ||
        !bridge.HasPendingHandoff()) {
        return false;
    }

    SchoolDaysKtrfGameplayBridge::StepResult step;
    if (!bridge.ContinueHandoff(&step, error)) return false;
    SchoolDaysFlowController::Action nextAction =
        SchoolDaysFlowController::Action::None;
    if (!flow.CompleteHandoff(step, &nextAction, error)) {
        return false;
    }

    if (step.kind == SchoolDaysKtrfGameplayBridge::StepKind::Terminal) {
        return ExecuteFlowAction(nextAction, error);
    }
    if (step.kind != SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene) return false;

    gameContext->config.scene = 0;
    gameContext->config.next = false;
    gameContext->config.back = false;
    SDL_Log("[KTRF-HANDOFF] episode continue transition=%u destination=%s",
            step.transitionIndex, step.scene.sceneKey.c_str());
    return true;
}

bool SchoolDaysKtrfAppController::SubmitSaveFeelResult(
    SchoolDaysFlowController::SaveFeelResult result,
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.HasPendingHandoff()) return false;
    SchoolDaysFlowController::Action action =
        SchoolDaysFlowController::Action::None;
    if (!flow.SubmitSaveFeelResult(result, &action, error)) return false;
    return ExecuteFlowAction(action, error);
}

bool SchoolDaysKtrfAppController::SubmitSaveLoadResult(
    SchoolDaysFlowController::SaveLoadResult result,
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.HasPendingHandoff()) return false;
    SchoolDaysFlowController::Action action =
        SchoolDaysFlowController::Action::None;
    if (!flow.SubmitSaveLoadResult(result, &action, error)) return false;
    return ExecuteFlowAction(action, error);
}

bool SchoolDaysKtrfAppController::EnterGameplaySave(
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen() || bridge.HasPendingHandoff()) return false;
    SchoolDaysFlowController::Action action = SchoolDaysFlowController::Action::None;
    if (!flow.EnterGameplaySave(&action, error)) return false;
    return ExecuteFlowAction(action, error);
}

bool SchoolDaysKtrfAppController::EnterGameplayLoad(
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen() || bridge.HasPendingHandoff()) return false;
    SchoolDaysFlowController::Action action = SchoolDaysFlowController::Action::None;
    if (!flow.EnterGameplayLoad(&action, error)) return false;
    return ExecuteFlowAction(action, error);
}

bool SchoolDaysKtrfAppController::EnterGameplayConfig(
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen() || bridge.HasPendingHandoff()) return false;
    SchoolDaysFlowController::Action action = SchoolDaysFlowController::Action::None;
    if (!flow.EnterGameplayConfig(&action, error)) return false;
    return ExecuteFlowAction(action, error);
}

bool SchoolDaysKtrfAppController::EnterGameplayExit(
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen() || bridge.HasPendingHandoff()) return false;
    SchoolDaysFlowController::Action action = SchoolDaysFlowController::Action::None;
    if (!flow.EnterGameplayExit(&action, error)) return false;
    return ExecuteFlowAction(action, error);
}

bool SchoolDaysKtrfAppController::SkipToNextChoice(
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen() || bridge.HasPendingHandoff() ||
        flow.CurrentState() != SchoolDaysFlowController::State::Gameplay) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_FORMAT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days next-choice skip is unavailable now");
        }
        return false;
    }

    SchoolDaysKtrfGameplayBridge::StepResult step;
    if (!bridge.AdvanceToNextChoice("ktrf:next", &step, error)) return false;

    switch (step.kind) {
    case SchoolDaysKtrfGameplayBridge::StepKind::BlockedChoice: {
        Gameplay* target = bridge.CurrentGameplay();
        if (target != nullptr) {
            // Native SuperSkip chains code5 (cross-script advance) into code2
            // when the destination is the choice-bearing script.  Reuse the
            // authenticated atomic SkipFRAME target instead of replaying the
            // skipped ORS interval.
            (void)target->SkipSchoolDaysToEnd();
        }
        gameContext->config.scene = 0;
        gameContext->config.next = false;
        gameContext->config.back = false;
        return true;
    }
    case SchoolDaysKtrfGameplayBridge::StepKind::PendingHandoff:
    case SchoolDaysKtrfGameplayBridge::StepKind::Terminal:
        return flow.ObserveStep(step, error);
    case SchoolDaysKtrfGameplayBridge::StepKind::Unresolved:
        return true;
    case SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene:
    case SchoolDaysKtrfGameplayBridge::StepKind::BackBoundary:
    case SchoolDaysKtrfGameplayBridge::StepKind::BackTrialPv:
    case SchoolDaysKtrfGameplayBridge::StepKind::BackNoScript:
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_FORMAT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days next-choice skip returned an invalid step");
        }
        return false;
    }
    return false;
}

bool SchoolDaysKtrfAppController::EnterGameplayRouteMap(
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen()) return false;
    SchoolDaysFlowController::Action action = SchoolDaysFlowController::Action::None;
    if (!flow.EnterGameplayRouteMap(&action, error)) return false;
    return ExecuteFlowAction(action, error);
}

bool SchoolDaysKtrfAppController::ReturnGameplayRouteMapToLoad(
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen()) return false;
    SchoolDaysFlowController::Action action = SchoolDaysFlowController::Action::None;
    if (!flow.ReturnGameplayRouteMapToLoad(&action, error)) return false;
    return ExecuteFlowAction(action, error);
}

bool SchoolDaysKtrfAppController::CloseGameplayModal(
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen()) return false;
    SchoolDaysFlowController::Action action = SchoolDaysFlowController::Action::None;
    if (!flow.CloseGameplayModal(&action, error)) return false;
    return ExecuteFlowAction(action, error);
}

bool SchoolDaysKtrfAppController::SubmitGameplayExit(
    bool confirm, bool* returnToTitle, Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen() || returnToTitle == nullptr ||
        flow.CurrentState() != SchoolDaysFlowController::State::GameplayExit) {
        return false;
    }
    *returnToTitle = false;

    if (!confirm) {
        SchoolDaysFlowController::Action action = SchoolDaysFlowController::Action::None;
        if (!flow.SubmitGameplayExit(false, &action, error)) return false;
        return ExecuteFlowAction(action, error);
    }

    // EXE state5 confirmation writes FLAG_LOGO through the global VT_BOOL
    // setter before Script.Free tears down the active scene. Keep the persisted
    // type explicit; type11 true is canonical VARIANT_BOOL -1.
    KtrfStateStore::Snapshot stateBefore;
    if (!bridge.Session().State().Capture(&stateBefore, error)) return false;
    const SchoolDaysPersistence persistenceBefore = bridge.Session().Persistence();
    if (!bridge.Session().Persistence().WriteGlobalInteger(
            "FLAG_LOGO", 11, -1, &bridge.Session().State(), error)) {
        return false;
    }

    SchoolDaysFlowController::Action action = SchoolDaysFlowController::Action::None;
    if (!flow.SubmitGameplayExit(true, &action, error)) {
        Kotonoha_KtrfError ignored{};
        (void)bridge.Session().State().Restore(stateBefore, &ignored);
        bridge.Session().Persistence() = persistenceBefore;
        return false;
    }
    if (!bridge.ReleaseCurrentGameplayForFrontend(error)) {
        Kotonoha_KtrfError ignored{};
        (void)bridge.Session().State().Restore(stateBefore, &ignored);
        bridge.Session().Persistence() = persistenceBefore;
        flow.Reset();
        return false;
    }
    if (!ExecuteFlowAction(action, error)) return false;
    gameContext->config.scene = 0;
    gameContext->config.next = false;
    gameContext->config.back = false;
    *returnToTitle = true;
    return true;
}

bool SchoolDaysKtrfAppController::EnterTitleLoad(
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen()) return false;
    SchoolDaysFlowController::Action action =
        SchoolDaysFlowController::Action::None;
    if (!flow.EnterTitleLoad(&action, error)) return false;
    return ExecuteFlowAction(action, error);
}

bool SchoolDaysKtrfAppController::CancelTitleLoad(
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen()) return false;
    SchoolDaysFlowController::Action action =
        SchoolDaysFlowController::Action::None;
    if (!flow.CancelTitleLoad(&action, error)) return false;
    return ExecuteFlowAction(action, error);
}

bool SchoolDaysKtrfAppController::EnterTitleRouteMap(
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen()) return false;
    SchoolDaysFlowController::Action action =
        SchoolDaysFlowController::Action::None;
    if (!flow.EnterTitleRouteMap(&action, error)) return false;
    return ExecuteFlowAction(action, error);
}

bool SchoolDaysKtrfAppController::ReturnTitleRouteMapToLoad(
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen()) return false;
    SchoolDaysFlowController::Action action =
        SchoolDaysFlowController::Action::None;
    if (!flow.ReturnTitleRouteMapToLoad(&action, error)) return false;
    return ExecuteFlowAction(action, error);
}

bool SchoolDaysKtrfAppController::LoadRouteMapSelector(
    int selector, Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen() || gameContext == nullptr ||
        (flow.CurrentState() != SchoolDaysFlowController::State::TitleRouteMap &&
         flow.CurrentState() != SchoolDaysFlowController::State::GameplayRouteMap)) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days RouteMap load requires an open RouteMap frontend");
        }
        return false;
    }

    SchoolDaysKtrfGameplayBridge::StepResult step;
    if (!bridge.LoadRouteMapSelector(selector, &step, error) ||
        step.kind != SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene) {
        return false;
    }
    if (!flow.LeaveRouteMapForGameplay(error)) return false;

    gameContext->config.scene = 0;
    gameContext->config.next = false;
    gameContext->config.back = false;
    SDL_Log("[KTRF-ROUTEMAP] selector=%d restored scene=%s ors=%s", selector,
            step.scene.sceneKey.c_str(), step.scene.orsPath.c_str());
    return true;
}

bool SchoolDaysKtrfAppController::StartNewGame(
    int selector, Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen() || gameContext == nullptr) return false;
    // Only Title item0's selector0 semantics are closed for the normal game.
    // AC item5 uses selector1 and remains fail-closed until that start variant
    // is authenticated separately.
    if (selector != 0 || flow.CurrentState() != SchoolDaysFlowController::State::Title) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_UNSUPPORTED;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days start selector is not supported here");
        }
        return false;
    }

    SchoolDaysKtrfGameplayBridge::StepResult step;
    if (!bridge.ResetNewGame(&step, error) ||
        step.kind != SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene) {
        return false;
    }
    if (!flow.LeaveTitleForGameplay(error)) return false;
    gameContext->config.scene = 0;
    gameContext->config.next = false;
    gameContext->config.back = false;
    SDL_Log("[KTRF-START] new game scene=%s ors=%s",
            step.scene.sceneKey.c_str(), step.scene.orsPath.c_str());
    return true;
}

bool SchoolDaysKtrfAppController::EnterStartupTitle(
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen()) return false;
    SchoolDaysFlowController::Action action =
        SchoolDaysFlowController::Action::None;
    if (!flow.EnterStartupTitle(&action, error)) return false;
    return ExecuteFlowAction(action, error);
}

bool SchoolDaysKtrfAppController::EnterTitleConfig(
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen()) return false;
    SchoolDaysFlowController::Action action =
        SchoolDaysFlowController::Action::None;
    if (!flow.EnterTitleConfig(&action, error)) return false;
    return ExecuteFlowAction(action, error);
}

bool SchoolDaysKtrfAppController::CloseTitleConfig(
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen()) return false;
    SchoolDaysFlowController::Action action =
        SchoolDaysFlowController::Action::None;
    if (!flow.CloseTitleConfig(&action, error)) return false;
    return ExecuteFlowAction(action, error);
}

bool SchoolDaysKtrfAppController::EnterTitleExit(
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen()) return false;
    SchoolDaysFlowController::Action action =
        SchoolDaysFlowController::Action::None;
    if (!flow.EnterTitleExit(&action, error)) return false;
    return ExecuteFlowAction(action, error);
}

bool SchoolDaysKtrfAppController::SubmitTitleExit(
    bool confirm, bool* exitApplication, Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen() || exitApplication == nullptr) return false;
    *exitApplication = false;
    SchoolDaysFlowController::Action action =
        SchoolDaysFlowController::Action::None;
    if (!flow.SubmitTitleExit(confirm, &action, error)) return false;
    if (action == SchoolDaysFlowController::Action::ExitApplication) {
        *exitApplication = true;
        return true;
    }
    return ExecuteFlowAction(action, error);
}

bool SchoolDaysKtrfAppController::LoadGlobalFlag(
    const std::vector<uint8_t>& bytes, Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen()) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days GlobalFlag load requires an open app controller");
        }
        return false;
    }
    return bridge.Session().LoadGlobalFlag(bytes, error);
}

bool SchoolDaysKtrfAppController::HasGlobalFlag() const {
    return opened && bridge.IsOpen() &&
           bridge.Session().Persistence().HasGlobalFlag();
}

bool SchoolDaysKtrfAppController::SerializeGlobalFlag(
    std::vector<uint8_t>* bytes, Kotonoha_KtrfError* error) const {
    if (!opened || !bridge.IsOpen()) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days GlobalFlag save requires an open app controller");
        }
        return false;
    }
    return bridge.Session().SerializeGlobalFlag(bytes, error);
}

bool SchoolDaysKtrfAppController::LoadSaveFile(
    const std::vector<uint8_t>& bytes, Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen() || gameContext == nullptr) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days SaveFile load requires an open app controller");
        }
        return false;
    }

    SchoolDaysKtrfGameplayBridge::StepResult step;
    if (!bridge.LoadSave(bytes, &step, error)) return false;
    if (step.kind != SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_FORMAT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days SaveFile load did not resolve a physical scene");
        }
        return false;
    }

    // Loading replaces the narrative/outer-flow continuation.  The bridge has
    // already restored tag1/FlgH/router state transactionally and swapped the
    // physical Gameplay before we publish the reset to the application layer.
    flow.Reset();
    gameContext->config.scene = 0;
    gameContext->config.next = false;
    gameContext->config.back = false;
    SDL_Log("[KTRF-SAVE] loaded scene=%s ors=%s",
            step.scene.sceneKey.c_str(), step.scene.orsPath.c_str());
    return true;
}

bool SchoolDaysKtrfAppController::PlanSaveSlot(
    int slot, const std::u16string& primaryMetadata,
    const std::u16string& subMetadata, SchoolDaysSaveService::Plan* plan,
    Kotonoha_KtrfError* error) const {
    if (!opened || !bridge.IsOpen()) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days save plan requires an open app controller");
        }
        return false;
    }
    return SchoolDaysSaveService::PlanSlot(
        slot, primaryMetadata, subMetadata, bridge.Session().Persistence(),
        bridge.Session().State(), plan, error);
}

bool SchoolDaysKtrfAppController::PlanSaveSlot(
    const SchoolDaysSaveProfile& profile, int slot,
    const std::u16string& primaryMetadata,
    const std::u16string& subMetadata, SchoolDaysSaveService::Plan* plan,
    Kotonoha_KtrfError* error) const {
    if (!opened || !bridge.IsOpen()) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days save plan requires an open app controller");
        }
        return false;
    }
    return SchoolDaysSaveService::PlanSlot(
        profile, slot, primaryMetadata, subMetadata,
        bridge.Session().Persistence(), bridge.Session().State(), plan, error);
}

bool SchoolDaysKtrfAppController::CommitSavePlan(
    const SchoolDaysSaveService::Plan& plan, Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen()) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days save commit requires an open app controller");
        }
        return false;
    }
    return SchoolDaysSaveService::Commit(
        plan, &bridge.Session().Persistence(), error);
}

bool SchoolDaysKtrfAppController::ReadSaveSlotMetadata(
    const SchoolDaysSaveProfile& profile, int slot, bool fileOpenable,
    SchoolDaysSaveService::SlotMetadata* metadata,
    Kotonoha_KtrfError* error) const {
    if (!opened || !bridge.IsOpen()) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days SaveLoad metadata read requires an open app controller");
        }
        return false;
    }
    return SchoolDaysSaveService::ReadSlotMetadata(
        profile, slot, fileOpenable, bridge.Session().Persistence(), metadata,
        error);
}

bool SchoolDaysKtrfAppController::BuildSaveMetadata(
    const SchoolDaysSaveMetadata::LocalTime& time, bool useEnglish,
    const std::u16string& comment, SchoolDaysSaveMetadata::Result* result,
    Kotonoha_KtrfError* error) const {
    if (!opened || !bridge.IsOpen() || result == nullptr) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days save metadata requires an open app controller");
        }
        return false;
    }
    return SchoolDaysSaveMetadata::Build(
        bridge.Session().State(), time, useEnglish, comment, result, error);
}

bool SchoolDaysKtrfAppController::ResolveTitleBackground(
    const SchoolDaysEndBackgroundResolver::Profile& profile,
    SchoolDaysEndBackgroundResolver::Result* result,
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen()) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days title background requires an open app controller");
        }
        return false;
    }
    KtrfStateStore::Snapshot stateBefore;
    if (!bridge.Session().State().Capture(&stateBefore, error)) return false;
    const SchoolDaysPersistence persistenceBefore = bridge.Session().Persistence();

    if (!SchoolDaysEndBackgroundResolver::Resolve(
            profile, &bridge.Session().State(), result, error)) {
        return false;
    }
    if (!result->promotedAllClear) return true;

    // EXE 0x0041FEE0 reaches the global VT_BOOL setter.  Keep the visual
    // promotion and persisted FlagsHash mutation in one transaction; native
    // type11 true is -1 even though the resolver only needs nonzero semantics.
    if (!bridge.Session().Persistence().WriteGlobalInteger(
            "AllClear", 11, -1, &bridge.Session().State(), error)) {
        Kotonoha_KtrfError ignored{};
        (void)bridge.Session().State().Restore(stateBefore, &ignored);
        bridge.Session().Persistence() = persistenceBefore;
        return false;
    }
    return true;
}

bool SchoolDaysKtrfAppController::TitleEndClearAvailable(
    bool* available, Kotonoha_KtrfError* error) const {
    if (!opened || !bridge.IsOpen() || available == nullptr) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days Title EndClear query is invalid");
        }
        return false;
    }
    int64_t value = 0;
    if (!bridge.Session().State().ReadGlobalInt("EndClear", &value, error)) {
        return false;
    }
    *available = value != 0;
    return true;
}

bool SchoolDaysKtrfAppController::BuildInitialRouteMapProjection(
    RouteMapProjection* projection, Kotonoha_KtrfError* error) const {
    if (!opened || !bridge.IsOpen() || projection == nullptr) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days RouteMap initial projection is invalid");
        }
        return false;
    }

    const KtrfStateStore& state = bridge.Session().State();
    int64_t route = 0;
    int64_t endClear = 0;
    if (!state.ReadSessionInt("ROUTE", &route, error) ||
        !state.ReadGlobalInt("EndClear", &endClear, error)) {
        return false;
    }

    const int story = SchoolDaysSaveMetadata::StoryFor(route, endClear);
    const int rawMajor = story - 1;

    // Native RouteMap init queries the page with GetStory()-1 before its sole
    // major==-1 normalization. Preserve that ordering exactly.
    Kotonoha_KtrfError pageError{};
    const int subpage =
        SchoolDaysRouteMapOracle::GetRouteMapPage(state, rawMajor, &pageError);
    if (pageError.code != KOTONOHA_KTRF_OK) {
        if (error != nullptr) *error = pageError;
        return false;
    }
    const int major = rawMajor == -1 ? 0 : rawMajor;
    if (major < 0 || major > 5) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_FORMAT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days RouteMap initial major is outside 0..5");
        }
        return false;
    }
    return BuildRouteMapProjection(major, subpage, projection, error);
}

bool SchoolDaysKtrfAppController::BuildRouteMapProjection(
    int major, int subpage, RouteMapProjection* projection,
    Kotonoha_KtrfError* error) const {
    if (!opened || !bridge.IsOpen() || projection == nullptr) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days RouteMap projection is invalid");
        }
        return false;
    }

    const auto page = SchoolDaysRouteMapModel::Page(major, subpage);
    if (major < 0 || major > 5 || subpage < 0 ||
        subpage >= SchoolDaysRouteMapModel::SubpageCountForMajor(major) ||
        page.nodeCount <= 0 ||
        page.nodeCount > static_cast<int>(RouteMapProjection{}.available.size())) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days RouteMap page projection is outside the native table");
        }
        return false;
    }

    RouteMapProjection next;
    next.major = major;
    next.subpage = subpage;
    // Normal Title LOAD -> RouteMap owns the unrestricted host path.
    next.alternateHost = false;

    const KtrfStateStore& state = bridge.Session().State();
    SchoolDaysRouteMapOracle::Completion completion;
    if (!SchoolDaysRouteMapOracle::ComputeCompletion(
            state, bridge.Session().Adapter()->document, &completion, error)) {
        return false;
    }
    next.completionPercent = completion.percent;
    for (int i = 0; i < page.nodeCount; ++i) {
        const std::string key = SchoolDaysRouteMapModel::NodeSpKey(major, subpage, i);
        int64_t value = 0;
        if (!state.ReadSessionInt(key.c_str(), &value, error)) return false;
        next.available[static_cast<size_t>(i)] = value != 0;
    }

    Kotonoha_KtrfError oracleError{};
    const int nativePage =
        SchoolDaysRouteMapOracle::GetRouteMapPage(state, major, &oracleError);
    if (oracleError.code != KOTONOHA_KTRF_OK) {
        if (error != nullptr) *error = oracleError;
        return false;
    }
    if (nativePage == subpage) {
        const int check = SchoolDaysRouteMapOracle::CheckScript(
            state, major, &oracleError);
        if (oracleError.code != KOTONOHA_KTRF_OK) {
            if (error != nullptr) *error = oracleError;
            return false;
        }
        next.currentMarker = check - page.nodeOffset;
    }

    *projection = next;
    if (error != nullptr) {
        error->code = KOTONOHA_KTRF_OK;
        error->message[0] = '\0';
    }
    return true;
}

bool SchoolDaysKtrfAppController::StartOrdinaryReplayPlayback(
    int hSceneIndex, SchoolDaysReplayPlaybackSession* playback,
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen() || gameContext == nullptr || playback == nullptr) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days ordinary replay playback dependencies are unavailable");
        }
        return false;
    }

    SchoolDaysReplayPlaybackSession::Resolver resolver;
    resolver.resolve = &SchoolDaysKtrfAppController::ResolveReplayScene;
    resolver.userdata = this;

    SchoolDaysKtrfGameplayBridge::Factory factory;
    factory.create = &SchoolDaysKtrfAppController::CreateGameplay;
    factory.destroy = &SchoolDaysKtrfAppController::DestroyGameplay;
    factory.userdata = this;

    return playback->StartOrdinary(hSceneIndex, gameContext, resolver, factory,
                                   error);
}

bool SchoolDaysKtrfAppController::StartSpecialReplayPlayback(
    int hSceneIndex, int choice, SchoolDaysReplayPlaybackSession* playback,
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen() || gameContext == nullptr || playback == nullptr) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days special replay playback dependencies are unavailable");
        }
        return false;
    }

    SchoolDaysReplayPlaybackSession::Resolver resolver;
    resolver.resolve = &SchoolDaysKtrfAppController::ResolveReplayScene;
    resolver.userdata = this;

    SchoolDaysKtrfGameplayBridge::Factory factory;
    factory.create = &SchoolDaysKtrfAppController::CreateGameplay;
    factory.destroy = &SchoolDaysKtrfAppController::DestroyGameplay;
    factory.userdata = this;

    return playback->StartSpecialChoice(hSceneIndex, choice, gameContext,
                                        resolver, factory, error);
}

bool SchoolDaysKtrfAppController::StartPlayDataPlayback(
    const std::vector<uint8_t>& saveBytes, SchoolDaysPlayDataSession* playback,
    Kotonoha_KtrfError* error) {
    if (!opened || !bridge.IsOpen() || gameContext == nullptr || playback == nullptr ||
        ktrfPath.empty() || orsRoot.empty()) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "School Days PlayData dependencies are unavailable");
        }
        return false;
    }

    std::vector<uint8_t> globalFlagBytes;
    const std::vector<uint8_t>* globalSnapshot = nullptr;
    if (bridge.Session().Persistence().HasGlobalFlag()) {
        if (!bridge.Session().SerializeGlobalFlag(&globalFlagBytes, error)) {
            return false;
        }
        globalSnapshot = &globalFlagBytes;
    }

    SchoolDaysKtrfGameplayBridge::Factory factory;
    factory.create = &SchoolDaysKtrfAppController::CreateGameplay;
    factory.destroy = &SchoolDaysKtrfAppController::DestroyGameplay;
    factory.userdata = this;

    return playback->Start(ktrfPath.c_str(), orsRoot.c_str(), saveBytes,
                           globalSnapshot, gameContext, factory, error);
}

bool SchoolDaysKtrfAppController::ContinueHandoff(Kotonoha_KtrfError* error) {
    return SubmitSaveFeelResult(SchoolDaysFlowController::SaveFeelResult::Skip,
                                error);
}

SDL_AppResult SchoolDaysKtrfAppController::Main(Gameplay** out,
                                                 bool sceneAdvancePrepared) {
    if (out != nullptr) *out = nullptr;
    if (!opened || engine == nullptr || gameContext == nullptr ||
        !bridge.IsOpen()) {
        SDL_LogError(0, "[KTRF-APP] controller is not open");
        return SDL_APP_FAILURE;
    }

    if (flow.CurrentState() == SchoolDaysFlowController::State::GameplaySave ||
        flow.CurrentState() == SchoolDaysFlowController::State::GameplayLoad ||
        flow.CurrentState() == SchoolDaysFlowController::State::GameplayConfig ||
        flow.CurrentState() == SchoolDaysFlowController::State::GameplayExit ||
        flow.CurrentState() == SchoolDaysFlowController::State::GameplayRouteMap ||
        flow.CurrentState() == SchoolDaysFlowController::State::Title ||
        flow.CurrentState() == SchoolDaysFlowController::State::TitleLoad ||
        flow.CurrentState() == SchoolDaysFlowController::State::TitleRouteMap ||
        flow.CurrentState() == SchoolDaysFlowController::State::TitleConfig ||
        flow.CurrentState() == SchoolDaysFlowController::State::TitleExit) {
        // Frontend/SysMenu owns the presentation surface. Do not expose the
        // frozen narrative Gameplay to timestamp/loading/system overlays while
        // TitleView or any of its authenticated frontend child views is active.
        return SDL_APP_CONTINUE;
    }

    Gameplay* current = bridge.CurrentGameplay();
    if (current == nullptr) {
        SDL_LogError(0, "[KTRF-APP] no active Gameplay");
        return SDL_APP_FAILURE;
    }

    if (bridge.HasPendingHandoff()) {
        if (flow.HasDeferredSignal()) {
            Kotonoha_KtrfError flowError{};
            if (!flow.ConsumeDeferredSignal(&flowError)) {
                SDL_LogError(0, "[KTRF-FLOW] deferred handoff consume failed: %s",
                             flowError.message);
                return SDL_APP_FAILURE;
            }
            SDL_Log("[KTRF-FLOW] deferred %s signal entered outer prompt transition=%u",
                    flow.IsTerminalFlow() ? "terminal" : "handoff",
                    static_cast<unsigned>(flow.Pending().transitionIndex));
        }
        if (gameContext->config.back) {
            SDL_Log("[KTRF-BACK] ignored while episode handoff is pending");
            gameContext->config.back = false;
        }
        // Deferred/prompt/SaveLoad/terminal states are system-UI ownership.
        // Keep Gameplay alive for continuation, but do not publish it as the
        // currently presented surface.
        return SDL_APP_CONTINUE;
    }

    const auto expectedScene = bridge.CurrentScene();
    if (current->scriptPath != expectedScene.orsPath) {
        SDL_LogError(0,
                     "[KTRF-APP] Gameplay mismatch expected=%s actual=%s",
                     expectedScene.orsPath.c_str(), current->scriptPath.c_str());
        return SDL_APP_FAILURE;
    }

    if (gameContext->config.back) {
        if (!sceneAdvancePrepared) {
            // Back replaces the live Gameplay immediately. Require the same
            // outgoing-frame lease guarantee as forward scene advance so the
            // bridge never destroys the only renderable copy first.
            SDL_Log("[KTRF-BACK] deferred until outgoing frame is prepared scene=%s",
                    expectedScene.sceneKey.c_str());
            if (out != nullptr) *out = current;
            return SDL_APP_CONTINUE;
        }

        Kotonoha_KtrfError error{};
        SchoolDaysKtrfGameplayBridge::StepResult backStep;
        if (!bridge.Back(&backStep, &error)) {
            SDL_LogError(0, "[KTRF-BACK] route failed scene=%s: %s",
                         expectedScene.sceneKey.c_str(), error.message);
            return SDL_APP_FAILURE;
        }
        gameContext->config.back = false;
        gameContext->config.next = false;

        switch (backStep.kind) {
        case SchoolDaysKtrfGameplayBridge::StepKind::Unresolved:
            SDL_Log("[KTRF-BACK] no observed predecessor scene=%s",
                    expectedScene.sceneKey.c_str());
            if (out != nullptr) *out = current;
            return SDL_APP_CONTINUE;

        case SchoolDaysKtrfGameplayBridge::StepKind::BackBoundary:
            SDL_Log("[KTRF-BACK] RouteProc boundary committed ROUTE=-1 scene=%s",
                    expectedScene.sceneKey.c_str());
            for (const auto& hook : backStep.hooks) {
                SDL_Log("[KTRF-BACK] hook=%s", hook.c_str());
            }
            if (out != nullptr) *out = current;
            return SDL_APP_CONTINUE;

        case SchoolDaysKtrfGameplayBridge::StepKind::BackTrialPv:
            SDL_Log("[KTRF-BACK] Trial/PV special committed script=PV/Trial_PV");
            for (const auto& hook : backStep.hooks) {
                SDL_Log("[KTRF-BACK] hook=%s", hook.c_str());
            }
            if (out != nullptr) *out = current;
            return SDL_APP_CONTINUE;

        case SchoolDaysKtrfGameplayBridge::StepKind::BackNoScript:
            SDL_Log("[KTRF-BACK] handler completed without predecessor script scene=%s",
                    expectedScene.sceneKey.c_str());
            for (const auto& hook : backStep.hooks) {
                SDL_Log("[KTRF-BACK] hook=%s", hook.c_str());
            }
            if (out != nullptr) *out = current;
            return SDL_APP_CONTINUE;

        case SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene: {
            Gameplay* previous = current;
            Gameplay* next = bridge.CurrentGameplay();
            if (next == nullptr || next == previous) {
                SDL_LogError(0, "[KTRF-BACK] swap produced invalid Gameplay");
                return SDL_APP_FAILURE;
            }
            gameContext->config.scene = 0;
            for (const auto& hook : backStep.hooks) {
                SDL_Log("[KTRF-BACK] hook=%s", hook.c_str());
            }
            SDL_Log("[KTRF-BACK] scene swap destination=%s ors=%s",
                    backStep.scene.sceneKey.c_str(),
                    backStep.scene.orsPath.c_str());
            if (out != nullptr) *out = next;
            return SDL_APP_CONTINUE;
        }

        case SchoolDaysKtrfGameplayBridge::StepKind::BlockedChoice:
        case SchoolDaysKtrfGameplayBridge::StepKind::PendingHandoff:
        case SchoolDaysKtrfGameplayBridge::StepKind::Terminal:
            SDL_LogError(0, "[KTRF-BACK] invalid Back result kind=%d",
                         static_cast<int>(backStep.kind));
            return SDL_APP_FAILURE;
        }
    }

    const SDL_AppResult result = current->Main(gameContext);
    if (result == SDL_APP_FAILURE) return result;

    if (current->prompt != nullptr && SchoolDaysChoiceShouldSubmit(
            current->choiceCommitSubmitted, current->prompt->Result())) {
        Kotonoha_KtrfError error{};
        int accepted = 0;
        const int64_t value = static_cast<int64_t>(current->prompt->Result());
        if (!bridge.CommitChoice(value, &accepted, &error) || !accepted) {
            SDL_LogError(0, "[KTRF-APP] choice commit failed value=%lld: %s",
                         static_cast<long long>(value), error.message);
            return SDL_APP_FAILURE;
        }
        current->choiceCommitSubmitted = true;
    }

    if (result != SDL_APP_CONTINUE || gameContext->config.next) {
        gameContext->config.next = false;
        if (!current->eventManager->CheckEnd(current)) {
            SDL_Log("[KTRF-APP] next ignored before ORS end scene=%s",
                    expectedScene.sceneKey.c_str());
            if (out != nullptr) *out = current;
        }
        else if (current->prompt != nullptr &&
                 current->prompt->SchoolDaysPresentationTailActive()) {
            // The native choice commits immediately, but its state2/state3
            // presentation remains alive for 24 ticks after timeout or 48
            // ticks after an accepted selection.  Keep the current Gameplay
            // physically alive until that visual tail completes.
            SDL_Log("[KTRF-APP] scene advance deferred for School Days choice tail scene=%s",
                    expectedScene.sceneKey.c_str());
            if (out != nullptr) *out = current;
        }
        else if (!sceneAdvancePrepared) {
            /* readyToEnd can be published by the worker after SDL_AppIterate's
             * pre-clear check.  Defer the route one frame so the outgoing
             * presented composition is captured before CreateAndSwap deletes
             * the current Gameplay. */
            SDL_Log("[KTRF-APP] scene advance deferred until transition frame is prepared scene=%s",
                    expectedScene.sceneKey.c_str());
            if (out != nullptr) *out = current;
        }
        else {
            Kotonoha_KtrfError error{};
            SchoolDaysKtrfGameplayBridge::StepResult step;
            if (!bridge.Advance("ktrf:next", &step, &error)) {
                SDL_LogError(0, "[KTRF-APP] route advance failed: %s",
                             error.message);
                return SDL_APP_FAILURE;
            }

            switch (step.kind) {
            case SchoolDaysKtrfGameplayBridge::StepKind::Unresolved:
                SDL_Log("[KTRF-APP] unresolved transition scene=%s",
                        expectedScene.sceneKey.c_str());
                if (out != nullptr) *out = current;
                break;

            case SchoolDaysKtrfGameplayBridge::StepKind::BlockedChoice:
                SDL_Log("[KTRF-APP] blocked by pending choice scene=%s",
                        expectedScene.sceneKey.c_str());
                if (out != nullptr) *out = current;
                break;

            case SchoolDaysKtrfGameplayBridge::StepKind::BackBoundary:
                SDL_LogError(0, "[KTRF-APP] forward advance returned BackBoundary");
                return SDL_APP_FAILURE;
            case SchoolDaysKtrfGameplayBridge::StepKind::BackTrialPv:
                SDL_LogError(0, "[KTRF-APP] forward advance returned BackTrialPv");
                return SDL_APP_FAILURE;
            case SchoolDaysKtrfGameplayBridge::StepKind::BackNoScript:
                SDL_LogError(0, "[KTRF-APP] forward advance returned BackNoScript");
                return SDL_APP_FAILURE;

            case SchoolDaysKtrfGameplayBridge::StepKind::PendingHandoff:
                if (!flow.ObserveStep(step, &error)) {
                    SDL_LogError(0, "[KTRF-FLOW] handoff observation failed: %s",
                                 error.message);
                    return SDL_APP_FAILURE;
                }
                SDL_Log("[KTRF-HANDOFF] episode pending transition=%u destination=%s",
                        step.transitionIndex, step.scene.sceneKey.c_str());
                if (out != nullptr) *out = current;
                break;

            case SchoolDaysKtrfGameplayBridge::StepKind::Terminal:
                if (!flow.ObserveStep(step, &error)) {
                    SDL_LogError(0, "[KTRF-FLOW] terminal observation failed: %s",
                                 error.message);
                    return SDL_APP_FAILURE;
                }
                SDL_Log("[KTRF-HANDOFF] terminal pending transition=%u endings=%d",
                        step.transitionIndex,
                        static_cast<int>(step.endings.size()));
                if (out != nullptr) *out = current;
                break;

            case SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene: {
                Gameplay* next = bridge.CurrentGameplay();
                if (next == nullptr) {
                    SDL_LogError(0, "[KTRF-APP] scene swap produced no Gameplay");
                    return SDL_APP_FAILURE;
                }
                gameContext->config.scene = 0;
                gameContext->config.back = false;
                SDL_Log("[KTRF-APP] scene swap transition=%u destination=%s ors=%s",
                        step.transitionIndex, step.scene.sceneKey.c_str(),
                        step.scene.orsPath.c_str());
                if (out != nullptr) *out = next;
                break;
            }
            }
        }
    }
    else if (out != nullptr) {
        *out = current;
    }

    return SDL_APP_CONTINUE;
}

} // namespace Kotonoha
