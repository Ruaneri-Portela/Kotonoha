#include <Kotonoha/Gameplay.hpp>
#include <Kotonoha/routing/SchoolDaysSceneTransaction.hpp>

namespace Kotonoha {

void SchoolDaysSceneTransaction::Reset() {
    presentedFrame.Release();
    preparedOutgoing = nullptr;
    prepared = false;
    leaseActive = false;
}

bool SchoolDaysSceneTransaction::PrepareAutomatic(SDL_Renderer* renderer,
                                                   Gameplay* outgoing) {
    if (renderer == nullptr || outgoing == nullptr || leaseActive || prepared) {
        return false;
    }

    if (!presentedFrame.Capture(renderer)) {
        return false;
    }

    preparedOutgoing = outgoing;
    prepared = true;
    SDL_Log("[SD-TRANSACTION] prepared automatic boundary outgoing=%p",
            static_cast<void*>(outgoing));
    return true;
}

bool SchoolDaysSceneTransaction::PrepareExplicit(SDL_Renderer* renderer,
                                                  Gameplay* outgoing,
                                                  const char* reason) {
    if (renderer == nullptr || outgoing == nullptr || leaseActive) {
        return false;
    }

    if (!presentedFrame.Capture(renderer)) {
        return false;
    }

    preparedOutgoing = outgoing;
    prepared = true;
    SDL_Log("[SD-TRANSACTION] prepared explicit boundary reason=%s outgoing=%p",
            reason != nullptr ? reason : "unknown",
            static_cast<void*>(outgoing));
    return true;
}

bool SchoolDaysSceneTransaction::CommitIfSwapped(SDL_Renderer* renderer,
                                                  Gameplay* outgoing,
                                                  Gameplay* incoming,
                                                  const char* reason) {
    if (outgoing == nullptr || incoming == nullptr || outgoing == incoming) {
        return false;
    }

    if (!prepared || preparedOutgoing != outgoing || !presentedFrame.HasFrame()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[SD-TRANSACTION] swap without prepared presented frame reason=%s outgoing=%p incoming=%p",
                    reason != nullptr ? reason : "unknown",
                    static_cast<void*>(outgoing),
                    static_cast<void*>(incoming));
        prepared = false;
        preparedOutgoing = nullptr;
        return false;
    }

    leaseActive = true;
    prepared = false;
    preparedOutgoing = nullptr;

    if (renderer != nullptr) {
        SDL_SetRenderTarget(renderer, nullptr);
        presentedFrame.Present(renderer);
    }

    SDL_Log("[SD-TRANSACTION] commit reason=%s outgoing=%p incoming=%p lease=active",
            reason != nullptr ? reason : "unknown",
            static_cast<void*>(outgoing),
            static_cast<void*>(incoming));
    return true;
}

void SchoolDaysSceneTransaction::PresentLease(SDL_Renderer* renderer) const {
    if (!HasLease() || renderer == nullptr) {
        return;
    }
    presentedFrame.Present(renderer);
}

bool SchoolDaysSceneTransaction::ReleaseIfIncomingReady(Gameplay* incoming) {
    if (!HasLease() || incoming == nullptr || incoming->drawCanvas == nullptr) {
        return false;
    }

    if (!incoming->drawCanvas->DrewAtOrBelow(1)) {
        return false;
    }

    SDL_Log("[SD-TRANSACTION] incoming primary visual ready; release outgoing composition");
    Reset();
    return true;
}

} // namespace Kotonoha
