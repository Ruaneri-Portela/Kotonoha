#include <Kotonoha/Gameplay.hpp>
#include <Kotonoha/routing/KtrfSceneTransaction.hpp>

namespace Kotonoha {

void KtrfSceneTransaction::Reset() {
    presentedFrame.Release();
    preparedOutgoing = nullptr;
    prepared = false;
    leaseActive = false;
}

bool KtrfSceneTransaction::PrepareAutomatic(SDL_Renderer* renderer,
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

bool KtrfSceneTransaction::PrepareExplicit(SDL_Renderer* renderer,
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

bool KtrfSceneTransaction::CommitIfSwapped(Gameplay* outgoing,
                                                  Gameplay* incoming,
                                                  const char* reason) {
    if (outgoing == nullptr || incoming == nullptr || outgoing == incoming) {
        return false;
    }

    /* If the incoming scene never produced a primary visual before it reached
       its own end (slow I/O or an aggressive seek), the active lease is still
       the last valid presented composition. Carry it across the swap instead
       of requiring a frame from a scene that was never visibly ready. */
    if (HasLease()) {
        SDL_Log("[SD-TRANSACTION] carry retained frame across swap reason=%s outgoing=%p incoming=%p",
                reason != nullptr ? reason : "unknown",
                static_cast<void*>(outgoing),
                static_cast<void*>(incoming));
        return true;
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

    SDL_Log("[SD-TRANSACTION] commit reason=%s outgoing=%p incoming=%p lease=active",
            reason != nullptr ? reason : "unknown",
            static_cast<void*>(outgoing),
            static_cast<void*>(incoming));
    return true;
}

bool KtrfSceneTransaction::CancelPreparedFor(Gameplay* outgoing,
                                                   const char* reason) {
    if (leaseActive || !prepared || outgoing == nullptr ||
        preparedOutgoing != outgoing) {
        return false;
    }
    SDL_Log("[SD-TRANSACTION] cancel prepared frame reason=%s outgoing=%p",
            reason != nullptr ? reason : "unknown",
            static_cast<void*>(outgoing));
    presentedFrame.Release();
    preparedOutgoing = nullptr;
    prepared = false;
    return true;
}

void KtrfSceneTransaction::PresentLease(SDL_Renderer* renderer) const {
    if (!HasLease() || renderer == nullptr) {
        return;
    }
    presentedFrame.Present(renderer);
}

bool KtrfSceneTransaction::PresentPrepared(
    SDL_Renderer* renderer, const Gameplay* outgoing) const {
    if (renderer == nullptr || outgoing == nullptr || leaseActive || !prepared ||
        preparedOutgoing != outgoing || !presentedFrame.HasFrame()) {
        return false;
    }
    presentedFrame.Present(renderer);
    return true;
}

bool KtrfSceneTransaction::ReleaseIfIncomingReady(Gameplay* incoming) {
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
