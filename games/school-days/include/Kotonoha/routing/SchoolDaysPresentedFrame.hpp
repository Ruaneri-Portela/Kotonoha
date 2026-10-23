#pragma once

#include <SDL3/SDL.h>

namespace Kotonoha {

// Retains the last fully presented School Days composition across a Current
// Scene swap. This is intentionally a composition snapshot, not a video-frame
// policy: the outgoing visual may be video, an image, or a Black/WhiteFade
// result. The retained frame exists only to prevent a renderer clear from
// becoming visible while the incoming scene has not produced a primary visual.
class SchoolDaysPresentedFrame {
public:
    SchoolDaysPresentedFrame() = default;
    ~SchoolDaysPresentedFrame();

    SchoolDaysPresentedFrame(const SchoolDaysPresentedFrame&) = delete;
    SchoolDaysPresentedFrame& operator=(const SchoolDaysPresentedFrame&) = delete;

    bool Capture(SDL_Renderer* renderer);
    bool Present(SDL_Renderer* renderer) const;
    void Release();

    bool HasFrame() const { return texture != nullptr; }

private:
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
};

} // namespace Kotonoha
