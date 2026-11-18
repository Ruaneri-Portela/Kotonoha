#include "SceneTexture.hpp"

#include <SDL3/SDL.h>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool success, const char *why) {
  if (!success)
    throw std::runtime_error(why);
}
void test() {
  check(SDL_Init(SDL_INIT_VIDEO), "Initialize SDL video");
  SDL_Window *window =
      SDL_CreateWindow("RealLive synthetic scene", 48, 36, SDL_WINDOW_HIDDEN);
  check(window != nullptr, "Create isolated SDL window");
  SDL_Renderer *renderer = SDL_CreateRenderer(window, "software");
  check(renderer != nullptr, "Create software renderer for scene test");
  SDL_Texture *scene = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_TARGET, 48, 36);
  check(
      RealLive::configureSceneTexture(scene),
      "Completed RealLive DC must use opaque replacement and linear filtering");
  SDL_BlendMode blend = SDL_BLENDMODE_BLEND;
  check(SDL_GetTextureBlendMode(scene, &blend) && blend == SDL_BLENDMODE_NONE,
        "The composition must not alpha-disappear when presented");

  // Draw a vivid synthetic scene with intentionally zero alpha, matching
  // intermediate DCs whose RGB is meaningful but transparency is not.
  check(SDL_SetRenderTarget(renderer, scene) &&
            SDL_SetRenderDrawColor(renderer, 135, 83, 211, 0) &&
            SDL_RenderClear(renderer),
        "Fill synthetic DC composition");
  check(SDL_SetRenderTarget(renderer, nullptr) &&
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) &&
            SDL_RenderClear(renderer) &&
            SDL_RenderTexture(renderer, scene, nullptr, nullptr),
        "Present the finished scene without blending with black clear");
  SDL_Surface *result = SDL_RenderReadPixels(renderer, nullptr);
  check(result != nullptr, "Read back SDL software raster");
  Uint8 r = 0, g = 0, b = 0, a = 0;
  check(SDL_ReadSurfacePixel(result, 24, 18, &r, &g, &b, &a),
        "Sample the center of the composed frame");
  check(r == 135 && g == 83 && b == 211,
        "Zero-alpha intermediate scene must retain its RGB at presentation");
  SDL_DestroySurface(result);
  SDL_DestroyTexture(scene);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}
} // namespace

int main() {
  try {
    test();
    std::cout << "RealLive.SceneTexture PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RealLive.SceneTexture FAIL: " << error.what() << '\n';
    return 1;
  }
}
