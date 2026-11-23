#include <Kotonoha/routing/SchoolDaysSystemPanelTransition.hpp>
#include <SDL3/SDL.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using Transition = Kotonoha::SchoolDaysSystemPanelTransition;

void Require(bool ok, const std::string &message) {
  if (!ok)
    throw std::runtime_error(message);
}

bool Near(float a, float b, float eps = 0.05f) {
  return std::fabs(a - b) <= eps;
}

void ReadRgb(SDL_Renderer *renderer, int x, int y, Uint8 *r, Uint8 *g,
             Uint8 *b) {
  const SDL_Rect sample{x, y, 1, 1};
  SDL_Surface *surface = SDL_RenderReadPixels(renderer, &sample);
  Require(surface != nullptr,
          std::string("SDL_RenderReadPixels failed: ") + SDL_GetError());
  Uint8 a = 0;
  const bool ok = SDL_ReadSurfacePixel(surface, 0, 0, r, g, b, &a);
  SDL_DestroySurface(surface);
  Require(ok, std::string("SDL_ReadSurfacePixel failed: ") + SDL_GetError());
}

void VerifySoftwareFadeViewport(int screenW, int screenH, int virtualW,
                                int virtualH, const char *label) {
  SDL_Surface *canvas =
      SDL_CreateSurface(screenW, screenH, SDL_PIXELFORMAT_RGBA32);
  Require(canvas != nullptr,
          std::string("SDL_CreateSurface failed: ") + SDL_GetError());
  SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(canvas);
  if (!renderer) {
    SDL_DestroySurface(canvas);
    throw std::runtime_error(
        std::string("SDL_CreateSoftwareRenderer failed: ") + SDL_GetError());
  }

  const auto vp = Transition::ComputeFittedViewport(static_cast<float>(screenW),
                                                    static_cast<float>(screenH),
                                                    virtualW, virtualH);
  Require(vp.Valid(), std::string(label) + ": viewport must be valid");
  const SDL_FRect area{vp.x, vp.y, vp.w, vp.h};

  Transition transition;
  transition.Begin(Transition::Target::Config, 1000u);
  const Uint8 alpha = transition.WhiteAlpha(1375u); // 50% opening white fade
  Require(alpha >= 120u && alpha <= 135u,
          std::string(label) + ": mid-opening white alpha drifted");

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  SDL_SetRenderDrawColor(renderer, 255, 255, 255, alpha);
  SDL_RenderFillRect(renderer, &area);
  SDL_RenderPresent(renderer);

  Uint8 r = 0, g = 0, b = 0;
  // Center inside the game viewport must reflect the white fade.
  ReadRgb(renderer, screenW / 2, screenH / 2, &r, &g, &b);
  Require(r >= 120u && g >= 120u && b >= 120u,
          std::string(label) + ": center of viewport did not fade white");

  // Pillarboxes (left/right) must remain pure black when horizontal margins
  // exist.
  if (vp.x >= 4.0f) {
    ReadRgb(renderer, 1, screenH / 2, &r, &g, &b);
    Require(r == 0u && g == 0u && b == 0u,
            std::string(label) + ": left pillarbox leaked white fade");
    ReadRgb(renderer, screenW - 2, screenH / 2, &r, &g, &b);
    Require(r == 0u && g == 0u && b == 0u,
            std::string(label) + ": right pillarbox leaked white fade");
  }

  // Letterboxes (top/bottom) must remain pure black when vertical margins
  // exist.
  if (vp.y >= 4.0f) {
    ReadRgb(renderer, screenW / 2, 1, &r, &g, &b);
    Require(r == 0u && g == 0u && b == 0u,
            std::string(label) + ": top letterbox leaked white fade");
    ReadRgb(renderer, screenW / 2, screenH - 2, &r, &g, &b);
    Require(r == 0u && g == 0u && b == 0u,
            std::string(label) + ": bottom letterbox leaked white fade");
  }

  SDL_DestroyRenderer(renderer);
  SDL_DestroySurface(canvas);
}

} // namespace

int main() {
  try {
    // 1. State machine & retail timing/alpha envelope parity.
    Transition t;
    t.Begin(Transition::Target::Save, 1000u);
    Require(t.OpeningFromBlack() && t.InputLocked(), "Opening state");
    Require(t.WhiteAlpha(1000u) == 0u && t.WhiteAlpha(1375u) == 127u &&
                t.WhiteAlpha(1750u) == 255u,
            "Opening 750ms alpha curve");
    Require(t.Update(1749u) == Transition::Event::None &&
                t.Update(1750u) == Transition::Event::OpenPanel &&
                t.CurrentPhase() == Transition::Phase::Open && !t.InputLocked(),
            "OpenPanel event at 750ms");
    t.BeginClose(2000u);
    Require(t.CurrentPhase() == Transition::Phase::Closing && t.InputLocked() &&
                !t.OpeningFromBlack() && t.WhiteAlpha(2250u) == 127u,
            "Closing 500ms alpha curve");
    Require(t.Update(2499u) == Transition::Event::None &&
                t.Update(2500u) == Transition::Event::ClosePanel &&
                t.CurrentPhase() == Transition::Phase::Revealing,
            "ClosePanel event at 500ms");
    Require(t.WhiteAlpha(2500u) == 255u && t.WhiteAlpha(2750u) == 128u &&
                t.WhiteAlpha(3000u) == 0u,
            "Revealing 500ms alpha curve");
    t.Update(3000u);
    Require(!t.Active() && t.CurrentPhase() == Transition::Phase::Idle,
            "Revealing returns to Idle");

    // 2. Geometry across ultrawide Android, 4:3, 16:9, Standard FULL,
    // letterbox.
    const auto pocoWide =
        Transition::ComputeFittedViewport(2712.0f, 1220.0f, 1280, 720);
    Require(pocoWide.Valid() && Near(pocoWide.x, 271.56f) &&
                Near(pocoWide.y, 0.0f) && Near(pocoWide.w, 2168.89f) &&
                Near(pocoWide.h, 1220.0f),
            "POCO 2712x1220 16:9 fitted viewport drifted");

    const auto pocoStandard =
        Transition::ComputeFittedViewport(2712.0f, 1220.0f, 800, 600);
    Require(pocoStandard.Valid() && Near(pocoStandard.x, 542.67f) &&
                Near(pocoStandard.y, 0.0f) && Near(pocoStandard.w, 1626.67f) &&
                Near(pocoStandard.h, 1220.0f),
            "POCO 2712x1220 4:3 fitted viewport drifted");

    const auto desktopStandardFull =
        Transition::ComputeFittedViewport(1920.0f, 1080.0f, 1280, 720, 0.75f);
    Require(desktopStandardFull.Valid() && Near(desktopStandardFull.x, 0.0f) &&
                Near(desktopStandardFull.y, 135.0f) &&
                Near(desktopStandardFull.w, 1920.0f) &&
                Near(desktopStandardFull.h, 810.0f),
            "Desktop Standard FULL 0.75x height scale drifted");

    const auto stretched = Transition::ComputeFittedViewport(
        1920.0f, 1080.0f, 800, 600, 0.75f, true);
    Require(stretched.Valid() && Near(stretched.x, 0.0f) &&
                Near(stretched.y, 0.0f) && Near(stretched.w, 1920.0f) &&
                Near(stretched.h, 1080.0f),
            "Standard FULL Config stretch drifted");

    const auto invalid =
        Transition::ComputeFittedViewport(0.0f, 1080.0f, 1280, 720);
    Require(!invalid.Valid(), "Zero-width screen must yield invalid viewport");

    // 3. Real SDL software renderer pixel verification (pillarbox + letterbox).
    VerifySoftwareFadeViewport(1356, 610, 1280, 720, "Ultrawide 16:9");
    VerifySoftwareFadeViewport(1356, 610, 800, 600, "Ultrawide 4:3");
    VerifySoftwareFadeViewport(800, 600, 1280, 720,
                               "4:3 Screen 16:9 Letterbox");

    std::cout << "School Days system panel transition viewport tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysSystemPanelTransitionTest: " << error.what()
              << '\n';
    return 1;
  }
}
