#include <Kotonoha/routing/SchoolDaysKtrfGameplayBridge.hpp>

#include <cstdio>

namespace Kotonoha {

SchoolDaysKtrfGameplayBridge::~SchoolDaysKtrfGameplayBridge() {
    Close();
}

void SchoolDaysKtrfGameplayBridge::SetError(Kotonoha_KtrfError* error,
                                             Kotonoha_KtrfErrorCode code,
                                             const char* message) {
    if (error == nullptr) return;
    error->code = code;
    std::snprintf(error->message, sizeof(error->message), "%s",
                  message != nullptr ? message : "School Days gameplay bridge error");
}

bool SchoolDaysKtrfGameplayBridge::IsCallback38(
    const std::vector<std::string>& hooks) {
    for (const auto& hook : hooks) {
        if (hook == "overflow.sdhq:callback_38") return true;
    }
    return false;
}

bool SchoolDaysKtrfGameplayBridge::CreateAndSwap(
    const SchoolDaysKtrfScene& scene, Kotonoha_KtrfError* error) {
    if (factory.create == nullptr || factory.destroy == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days gameplay factory is incomplete");
        return false;
    }
    if (scene.orsPath.empty()) {
        SetError(error, KOTONOHA_KTRF_ERROR_FORMAT,
                 "School Days gameplay scene has no ORS path");
        return false;
    }

    Gameplay* next = factory.create(scene.orsPath.c_str(), gameContext,
                                    factory.userdata);
    if (next == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_IO,
                 "School Days gameplay factory failed to create scene");
        return false;
    }

    Gameplay* previous = currentGameplay;
    currentGameplay = next;
    currentScene = scene;
    if (previous != nullptr) factory.destroy(previous, factory.userdata);
    return true;
}

bool SchoolDaysKtrfGameplayBridge::Open(
    const char* ktrfPath, const char* assetsRoot, Kotonoha_Game* context,
    const Factory& requestedFactory, Kotonoha_KtrfError* error) {
    Close();

    if (context == nullptr || requestedFactory.create == nullptr ||
        requestedFactory.destroy == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days gameplay bridge context/factory is invalid");
        return false;
    }

    gameContext = context;
    factory = requestedFactory;

    if (!session.Open(ktrfPath, assetsRoot, error)) {
        gameContext = nullptr;
        factory = {};
        return false;
    }

    SchoolDaysKtrfScene first;
    if (!session.ResetNewGame(&first, error) || !CreateAndSwap(first, error)) {
        Close();
        return false;
    }

    pendingStep = {};
    pendingHandoff = false;
    opened = true;
    return true;
}

void SchoolDaysKtrfGameplayBridge::Close() {
    if (currentGameplay != nullptr && factory.destroy != nullptr) {
        factory.destroy(currentGameplay, factory.userdata);
    }
    currentGameplay = nullptr;
    currentScene = {};
    pendingStep = {};
    pendingHandoff = false;
    session.Close();
    factory = {};
    gameContext = nullptr;
    opened = false;
}

bool SchoolDaysKtrfGameplayBridge::CommitChoice(
    int64_t value, int* accepted, Kotonoha_KtrfError* error) {
    if (!opened || pendingHandoff) {
        SetError(error, KOTONOHA_KTRF_ERROR_FORMAT,
                 "School Days gameplay bridge cannot commit choice now");
        return false;
    }
    return session.CommitChoice(value, accepted, error);
}

bool SchoolDaysKtrfGameplayBridge::Advance(
    const char* trigger, StepResult* out, Kotonoha_KtrfError* error) {
    if (!opened || out == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days gameplay bridge is not open or result is null");
        return false;
    }
    if (pendingHandoff) {
        SetError(error, KOTONOHA_KTRF_ERROR_FORMAT,
                 "School Days gameplay bridge has a pending handoff");
        return false;
    }

    *out = {};
    SchoolDaysKtrfSession::StepResult step;
    if (!session.Advance(trigger, &step, error)) return false;

    out->transitionIndex = step.transitionIndex;
    out->hopCount = step.hopCount;
    out->scene = step.scene;
    out->hooks = step.hooks;
    out->endings = step.endings;

    switch (step.kind) {
    case SchoolDaysKtrfSession::StepKind::Unresolved:
        out->kind = StepKind::Unresolved;
        return true;
    case SchoolDaysKtrfSession::StepKind::BlockedChoice:
        out->kind = StepKind::BlockedChoice;
        return true;
    case SchoolDaysKtrfSession::StepKind::Terminal:
        out->kind = StepKind::Terminal;
        pendingStep = step;
        pendingHandoff = true;
        return true;
    case SchoolDaysKtrfSession::StepKind::Scene:
        if (IsCallback38(step.hooks)) {
            out->kind = StepKind::PendingHandoff;
            pendingStep = step;
            pendingHandoff = true;
            return true;
        }
        if (!CreateAndSwap(step.scene, error)) return false;
        out->kind = StepKind::SwappedScene;
        return true;
    }

    SetError(error, KOTONOHA_KTRF_ERROR_FORMAT,
             "School Days gameplay bridge received unknown session step");
    return false;
}

bool SchoolDaysKtrfGameplayBridge::ContinueHandoff(
    StepResult* out, Kotonoha_KtrfError* error) {
    if (!opened || out == nullptr || !pendingHandoff) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days gameplay bridge has no pending handoff");
        return false;
    }

    *out = {};
    out->transitionIndex = pendingStep.transitionIndex;
    out->hopCount = pendingStep.hopCount;
    out->scene = pendingStep.scene;
    out->hooks = pendingStep.hooks;
    out->endings = pendingStep.endings;

    if (pendingStep.kind == SchoolDaysKtrfSession::StepKind::Terminal) {
        out->kind = StepKind::Terminal;
        pendingStep = {};
        pendingHandoff = false;
        return true;
    }
    if (pendingStep.kind != SchoolDaysKtrfSession::StepKind::Scene) {
        SetError(error, KOTONOHA_KTRF_ERROR_FORMAT,
                 "School Days gameplay bridge pending handoff is malformed");
        return false;
    }

    if (!CreateAndSwap(pendingStep.scene, error)) return false;
    out->kind = StepKind::SwappedScene;
    pendingStep = {};
    pendingHandoff = false;
    return true;
}

} // namespace Kotonoha
