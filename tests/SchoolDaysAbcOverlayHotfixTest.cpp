#include <Kotonoha/components/Image.hpp>
#include <Kotonoha/SchoolDaysSceneTime.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::vector<Uint8> Pixels(SDL_Surface* surface) {
    Check(surface != nullptr && SDL_LockSurface(surface), "lock render surface");
    const auto* begin = static_cast<const Uint8*>(surface->pixels);
    std::vector<Uint8> pixels(begin, begin + surface->pitch * surface->h);
    SDL_UnlockSurface(surface);
    return pixels;
}

std::vector<Uint8> Draw(Kotonoha::Image& image, SDL_Renderer* renderer,
                        SDL_Surface* canvas) {
    Check(Kotonoha::Image::Render(nullptr, renderer, nullptr, &image, nullptr) ==
          KOTONOHA_SCENE_DRAW, "active base was not drawn");
    Check(SDL_RenderPresent(renderer), "software present");
    return Pixels(canvas);
}

void CheckAlphaComposition(const std::vector<Uint8>& before,
                           const std::vector<Uint8>& after,
                           SDL_Surface* canvas, const fs::path& overlayPath) {
    SDL_Surface* overlay = Kotonoha_imageCreateSurface(
        overlayPath.string().c_str(), -1, -1);
    Check(overlay && overlay->w == canvas->w && overlay->h == canvas->h,
          "real ABC overlay decode and dimensions");
    Check(SDL_LockSurface(overlay), "lock ABC source");
    const auto* source = static_cast<const Uint8*>(overlay->pixels);
    size_t transparentChanges = 0, opaqueChanges = 0;
    for (int y = 0; y < canvas->h; ++y) {
        for (int x = 0; x < canvas->w; ++x) {
            const size_t dest = static_cast<size_t>(y) * canvas->pitch + x * 4;
            const size_t src = static_cast<size_t>(y) * overlay->pitch + x * 4;
            const bool changed = before[dest] != after[dest] ||
                before[dest + 1] != after[dest + 1] ||
                before[dest + 2] != after[dest + 2] ||
                before[dest + 3] != after[dest + 3];
            if (changed && source[src + 3] == 0) ++transparentChanges;
            if (changed && source[src + 3] == 255) ++opaqueChanges;
        }
    }
    SDL_UnlockSurface(overlay);
    SDL_DestroySurface(overlay);
    Check(transparentChanges == 0, "transparent ABC pixels altered the base");
    Check(opaqueChanges > 0, "opaque mouth pixels did not alter the base");
}

void TestRealA01A02(const fs::path& assets) {
    const fs::path a01 = assets / "Event00/00-00/00-00-A01";
    const fs::path a02 = assets / "Event00/00-00/00-00-A02";
    const auto base1 = a01 / "00-00-A01-004.PNG";
    const auto base2 = a02 / "00-00-A02-001B.PNG";
    const auto makB = a01 / "00-00-A01-004MAK.B.PNG";
    const auto taiB = a01 / "00-00-A01-004TAI.B.PNG";
    const auto a02MakB = a02 / "00-00-A02-001BMAK.B.PNG";
    for (const auto& path : {base1, base2, makB, taiB, a02MakB})
        Check(fs::is_regular_file(path), "required read-only A01/A02 asset missing");

    Kotonoha_time* clock = Kotonoha_timeNew(false);
    SDL_Surface* canvas = SDL_CreateSurface(800, 452, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* renderer = canvas ? SDL_CreateSoftwareRenderer(canvas) : nullptr;
    Check(clock && canvas && renderer, "SDL software renderer fixture");
    Kotonoha_timeSet(clock, 1000);
    {
        Kotonoha::Image image(clock);
        const Uint64 first = image.Register(base1.string().c_str(), 0, 5000, 0);
        auto* early = image.FindAbcGroup("mak", 1000);
        Check(first != 0 && early == nullptr &&
              image.ActiveBaseGeneration() == 0,
              "prepared ABC became an active base");
        SDL_SetRenderDrawColor(renderer, 19, 37, 53, 255);
        Check(SDL_RenderClear(renderer) && SDL_RenderPresent(renderer),
              "initial software clear");
        const auto unchanged = Pixels(canvas);
        Check(Kotonoha::Image::Render(nullptr, renderer, nullptr, &image,
              nullptr) != KOTONOHA_SCENE_DRAW,
              "prepared base/ABC presented before START");
        Check(SDL_RenderPresent(renderer) && Pixels(canvas) == unchanged,
              "prepared ABC changed presented pixels");

        Check(image.ActivateBase(first), "activate first CreateBG generation");
        auto* mak = image.FindAbcGroup("mak", 1000);
        auto* tai = image.FindAbcGroup("tai", 1000);
        Check(mak && tai && mak != tai && !mak->bound && !tai->bound,
              "prepared groups were already bound");
        const auto basePixels = Draw(image, renderer, canvas);
        image.SelectAbcState(mak, 1);
        Check(Draw(image, renderer, canvas) == basePixels,
              "unbound MAK group was visible");
        Check(image.BindAbcGroup(mak, first, 0, 5000), "bind MAK to active base");
        image.SelectAbcState(mak, 1);
        const auto makPixels = Draw(image, renderer, canvas);
        CheckAlphaComposition(basePixels, makPixels, canvas, makB);
        Check(image.BindAbcGroup(tai, first, 0, 5000), "bind TAI to active base");
        image.SelectAbcState(tai, 1);
        const auto makTaiPixels = Draw(image, renderer, canvas);
        CheckAlphaComposition(makPixels, makTaiPixels, canvas, taiB);

        const Uint64 second = image.Register(base2.string().c_str(), 0, 5000, 0);
        Check(second != first && Draw(image, renderer, canvas) == makTaiPixels,
              "prepared replacement base or ABC became visible");
        Check(image.ActivateBase(second), "activate A02 CreateBG generation");
        Check(!mak->bound && !tai->bound &&
              !image.IsActiveGroup(mak, 1000) &&
              !image.BindAbcGroup(mak, second, 0, 5000),
              "base switch retained or rebound an orphan ABC");
        // In A02 the MAK voice spans CreateBG 001B: ticks 40..77 and switch 55.
        Kotonoha_timeSet(clock, Kotonoha_SceneTickToMillisecondsCeil(55));
        const auto a02Pixels = Draw(image, renderer, canvas);
        auto* a02Mak = image.FindAbcGroup("mak", 1000);
        Check(a02Mak && !a02Mak->bound && a02Mak->ownerBaseGeneration == second,
              "A02 group has wrong owner generation");
        Check(Draw(image, renderer, canvas) == a02Pixels,
              "old overlay persisted after base switch");
        Check(image.BindAbcGroup(a02Mak, second,
              Kotonoha_SceneTickToMillisecondsCeil(40),
              Kotonoha_SceneTickToMillisecondsCeil(77)),
              "new base rejected a still-active A02 voice binding");
        image.SelectAbcState(a02Mak, 1);
        const auto a02Mouth = Draw(image, renderer, canvas);
        CheckAlphaComposition(a02Pixels, a02Mouth, canvas, a02MakB);
        Kotonoha_timeSet(clock, 5000);
        Check(Kotonoha::Image::Render(nullptr, renderer, nullptr, &image,
              nullptr) == KOTONOHA_SCENE_DRAW_LAST &&
              SDL_RenderPresent(renderer) && Pixels(canvas) == a02Pixels,
              "CreateBG END retained an orphan ABC patch");
        image.Reset();
        Check(image.ActiveBaseGeneration() == 0 &&
              image.FindAbcGroup("mak", 1000) == nullptr,
              "reset retained ABC ownership");

        Kotonoha_timeSet(clock, 1000);
        const Uint64 repeated1 = image.Register(base1.string().c_str(), 0, 5000, 0);
        const Uint64 repeated2 = image.Register(base1.string().c_str(), 0, 5000, 0);
        Check(repeated1 != repeated2 && image.ActivateBase(repeated1),
              "same-path CreateBG instances share an identity");
        auto* oldMak = image.FindAbcGroup("mak", 1000);
        Check(oldMak && image.BindAbcGroup(oldMak, repeated1, 0, 5000),
              "same-path first bind");
        Check(image.ActivateBase(repeated2) && !oldMak->bound &&
              !image.BindAbcGroup(oldMak, repeated2, 0, 5000),
              "same-path base switch retained old overlay");
        auto* newMak = image.FindAbcGroup("mak", 1000);
        Check(newMak && newMak != oldMak && !newMak->bound,
              "same-path replacement displayed prepared group");
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(canvas);
    Kotonoha_timeDestroy(clock);
}
} // namespace

int main(int argc, char** argv) {
    try {
        Check(argc == 2, "usage: SchoolDaysAbcOverlayHotfixTest ASSETS_ROOT");
        SDL_SetHint("SDL_VIDEODRIVER", "dummy");
        Check(SDL_Init(SDL_INIT_VIDEO), "SDL video init");
        TestRealA01A02(argv[1]);
        SDL_Quit();
        std::cout << "SchoolDaysAbcOverlayHotfixTest PASS\n";
        return 0;
    } catch (const std::exception& error) {
        SDL_Quit();
        std::cerr << "SchoolDaysAbcOverlayHotfixTest FAIL: " << error.what() << '\n';
        return 1;
    }
}
