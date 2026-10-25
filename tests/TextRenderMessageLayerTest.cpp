#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/renders/TextRender.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

void Check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main() {
    SDL_Surface *surface = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *target = nullptr;
    try {
        Check(SDL_Init(0), "SDL_Init failed");
        surface = SDL_CreateSurface(64, 48, SDL_PIXELFORMAT_RGBA32);
        Check(surface != nullptr, "SDL_CreateSurface failed");
        renderer = SDL_CreateSoftwareRenderer(surface);
        Check(renderer != nullptr, "SDL_CreateSoftwareRenderer failed");
        target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                   SDL_TEXTUREACCESS_TARGET, 64, 48);
        Check(target != nullptr, "SDL_CreateTexture target failed");
        Check(SDL_SetTextureBlendMode(target, SDL_BLENDMODE_NONE),
              "SDL_SetTextureBlendMode failed");

        // Seed the subtitle target with a visible stale frame.  This models the
        // exact state that used to remain on-screen after Display subtitles=OFF.
        Check(SDL_SetRenderTarget(renderer, target), "set target failed");
        Check(SDL_SetRenderDrawColor(renderer, 255, 32, 16, 255),
              "set seed color failed");
        Check(SDL_RenderClear(renderer), "seed clear failed");
        Check(SDL_SetRenderTarget(renderer, nullptr), "restore target failed");

        Kotonoha_Game game{};
        Kotonoha_SetMessageLayerEnabled(&game, false);

        Kotonoha_Subtitles subtitles{};
        // The hidden-layer path intentionally does not dereference libass/time;
        // non-null sentinels keep this test focused on visibility semantics.
        subtitles.ass_renderer = reinterpret_cast<ASS_Renderer *>(uintptr_t{1});
        subtitles.track = reinterpret_cast<ASS_Track *>(uintptr_t{1});
        subtitles.time = reinterpret_cast<Kotonoha_Time *>(uintptr_t{1});
        subtitles.gameCtx = &game;
        subtitles.hasRenderedFrame = true;
        subtitles.lastFrameHasSubtitle = true;

        const auto status = KotonohaTextRenderDraw(
            reinterpret_cast<SDL_Window *>(uintptr_t{1}), renderer, &subtitles,
            target);
        Check(status == KOTONOHA_SCENE_COMPLETE,
              "hidden subtitle layer must release retained canvas frame");
        Check(subtitles.messageLayerHiddenLastFrame,
              "hidden subtitle layer did not arm redraw-on-enable");
        Check(!subtitles.hasRenderedFrame && !subtitles.lastFrameHasSubtitle,
              "hidden subtitle layer retained stale frame state");

        Check(SDL_SetRenderTarget(renderer, target), "read target failed");
        SDL_Surface *pixels = SDL_RenderReadPixels(renderer, nullptr);
        Check(pixels != nullptr, "SDL_RenderReadPixels failed");
        Check(SDL_LockSurface(pixels), "SDL_LockSurface failed");
        const auto *bytes = static_cast<const Uint8 *>(pixels->pixels);
        bool transparent = true;
        for (int y = 0; y < pixels->h && transparent; ++y) {
            for (int x = 0; x < pixels->w; ++x) {
                const auto *pixel = bytes + y * pixels->pitch + x * 4;
                if (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0 ||
                    pixel[3] != 0) {
                    transparent = false;
                    break;
                }
            }
        }
        SDL_UnlockSurface(pixels);
        SDL_DestroySurface(pixels);
        Check(transparent, "hidden subtitle layer did not clear stale pixels");

        SDL_SetRenderTarget(renderer, nullptr);
        SDL_DestroyTexture(target);
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
        SDL_Quit();
        std::cout << "TextRenderMessageLayerTest PASS\n";
        return 0;
    } catch (const std::exception &error) {
        if (renderer != nullptr) SDL_SetRenderTarget(renderer, nullptr);
        if (target != nullptr) SDL_DestroyTexture(target);
        if (renderer != nullptr) SDL_DestroyRenderer(renderer);
        if (surface != nullptr) SDL_DestroySurface(surface);
        SDL_Quit();
        std::cerr << "TextRenderMessageLayerTest FAIL: " << error.what() << '\n';
        return 1;
    }
}
