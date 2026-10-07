#include <Kotonoha/Kotonoha.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfAppController.hpp>

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
        return new Gameplay(orsPath, context);
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
        Kotonoha_SchoolDaysKtrfNodeView view{};
        if (!Kotonoha_SchoolDaysKtrfGetNodeView(adapter, nodeIndex, &view, error)) {
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

    SchoolDaysKtrfGameplayBridge::Factory factory;
    factory.create = &SchoolDaysKtrfAppController::CreateGameplay;
    factory.destroy = &SchoolDaysKtrfAppController::DestroyGameplay;
    factory.userdata = this;

    if (!bridge.Open(ktrfPath, orsRoot, gameContext, factory, error)) {
        engine = nullptr;
        gameContext = nullptr;
        return false;
    }

    if (!BuildDebugSceneCatalog(error)) {
        Close();
        return false;
    }

    gameContext->scene = 0;
    gameContext->next = false;
    gameContext->back = false;
    opened = true;

    SDL_Log("[KTRF-APP] enabled scene=%s ors=%s debug_scenes=%d",
            bridge.CurrentScene().sceneKey.c_str(),
            bridge.CurrentScene().orsPath.c_str(),
            static_cast<int>(debugScenes.size()));
    return true;
}

void SchoolDaysKtrfAppController::Close() {
    debugScenes.clear();
    bridge.Close();
    opened = false;
    engine = nullptr;
    gameContext = nullptr;
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

    gameContext->scene = 0;
    gameContext->next = false;
    gameContext->back = false;
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

bool SchoolDaysKtrfAppController::ContinueHandoff(Kotonoha_KtrfError* error) {
    if (!opened || !bridge.HasPendingHandoff()) return false;
    if (bridge.PendingHandoffIsTerminal()) {
        SDL_Log("[KTRF-HANDOFF] terminal acknowledged; title transition pending implementation");
        return false;
    }

    SchoolDaysKtrfGameplayBridge::StepResult step;
    if (!bridge.ContinueHandoff(&step, error)) return false;
    if (step.kind != SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene) {
        return false;
    }

    gameContext->scene = 0;
    gameContext->next = false;
    gameContext->back = false;
    SDL_Log("[KTRF-HANDOFF] episode continue transition=%u destination=%s",
            step.transitionIndex, step.scene.sceneKey.c_str());
    return true;
}

SDL_AppResult SchoolDaysKtrfAppController::Main(Gameplay** out) {
    if (out != nullptr) *out = nullptr;
    if (!opened || engine == nullptr || gameContext == nullptr ||
        !bridge.IsOpen()) {
        SDL_LogError(0, "[KTRF-APP] controller is not open");
        return SDL_APP_FAILURE;
    }

    Gameplay* current = bridge.CurrentGameplay();
    if (current == nullptr) {
        SDL_LogError(0, "[KTRF-APP] no active Gameplay");
        return SDL_APP_FAILURE;
    }

    if (bridge.HasPendingHandoff()) {
        if (out != nullptr) *out = current;
        Kotonoha_eventFree(&gameContext->eventQueu);
        return SDL_APP_CONTINUE;
    }

    const auto expectedScene = bridge.CurrentScene();
    if (current->scriptPath != expectedScene.orsPath) {
        SDL_LogError(0,
                     "[KTRF-APP] Gameplay mismatch expected=%s actual=%s",
                     expectedScene.orsPath.c_str(), current->scriptPath.c_str());
        return SDL_APP_FAILURE;
    }

    if (gameContext->back) {
        SDL_Log("[KTRF-APP] back routing not implemented");
        gameContext->back = false;
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

    if (result != SDL_APP_CONTINUE || gameContext->next) {
        gameContext->next = false;
        if (!current->eventManager->CheckEnd(current)) {
            SDL_Log("[KTRF-APP] next ignored before ORS end scene=%s",
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

            case SchoolDaysKtrfGameplayBridge::StepKind::PendingHandoff:
                SDL_Log("[KTRF-HANDOFF] episode pending transition=%u destination=%s",
                        step.transitionIndex, step.scene.sceneKey.c_str());
                if (out != nullptr) *out = current;
                break;

            case SchoolDaysKtrfGameplayBridge::StepKind::Terminal:
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
                gameContext->scene = 0;
                gameContext->back = false;
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

    Kotonoha_eventFree(&gameContext->eventQueu);
    return SDL_APP_CONTINUE;
}

} // namespace Kotonoha
