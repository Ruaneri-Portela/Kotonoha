#pragma once

#include <SDL3/SDL.h>
#include <Kotonoha/routing/SchoolDaysPresentedFrame.hpp>

namespace Kotonoha {

class Gameplay;

// Owns visual continuity for School Days Current Scene swaps.
// It never invents a fade or transition color; BlackFade/WhiteFade remain
// entirely ORS-driven. This object only retains the last presented composition
// until the incoming Gameplay has produced a primary visual (image/video).
class SchoolDaysSceneTransaction {
public:
    SchoolDaysSceneTransaction() = default;

    SchoolDaysSceneTransaction(const SchoolDaysSceneTransaction&) = delete;
    SchoolDaysSceneTransaction& operator=(const SchoolDaysSceneTransaction&) = delete;

    void Reset();

    // Called before the frame clear when the outgoing Gameplay is at its ORS
    // terminal point. Captures the already-presented backbuffer but does not
    // activate the lease until a swap is actually observed.
    bool PrepareAutomatic(SDL_Renderer* renderer, Gameplay* outgoing);

    // Same capture step for swaps initiated outside the normal frame advance,
    // such as the current development handoff path.
    bool PrepareExplicit(SDL_Renderer* renderer, Gameplay* outgoing,
                         const char* reason);

    // Called after routing/action code. If outgoing != incoming, the prepared
    // composition becomes the active lease and is redrawn in the same frame.
    bool CommitIfSwapped(SDL_Renderer* renderer, Gameplay* outgoing,
                         Gameplay* incoming, const char* reason);

    // Draw retained composition beneath the incoming Gameplay while its
    // primary scene visual is not ready.
    void PresentLease(SDL_Renderer* renderer) const;

    // Release only after image/video (canvas z <= 1) actually drew.
    bool ReleaseIfIncomingReady(Gameplay* incoming);

    bool HasLease() const { return leaseActive && presentedFrame.HasFrame(); }
    bool HasPreparedFrame() const { return prepared && presentedFrame.HasFrame(); }

private:
    SchoolDaysPresentedFrame presentedFrame;
    Gameplay* preparedOutgoing = nullptr;
    bool prepared = false;
    bool leaseActive = false;
};

} // namespace Kotonoha
