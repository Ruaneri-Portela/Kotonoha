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

    gameContext->scene = 0;
    gameContext->next = false;
    gameContext->back = false;
    opened = true;

    SDL_Log("[KTRF-APP] enabled scene=%s ors=%s",
            bridge.CurrentScene().sceneKey.c_str(),
            bridge.CurrentScene().orsPath.c_str());
    return true;
}

void SchoolDaysKtrfAppController::Close() {
    bridge.Close();
    opened = false;
    engine = nullptr;
    gameContext = nullptr;
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

    if (current->prompt != nullptr && current->prompt->Result() != -2) {
        Kotonoha_KtrfError error{};
        int accepted = 0;
        const int64_t value = static_cast<int64_t>(current->prompt->Result());
        if (!bridge.CommitChoice(value, &accepted, &error) || !accepted) {
            SDL_LogError(0, "[KTRF-APP] choice commit failed value=%lld: %s",
                         static_cast<long long>(value), error.message);
            return SDL_APP_FAILURE;
        }
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
