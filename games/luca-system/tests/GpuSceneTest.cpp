// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <LucaSystem/GpuSceneComposition.hpp>
#include <LucaSystem/SceneLayers.hpp>
#include <SDL3/SDL.h>
#include <map>

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char *what) {
  if (!ok)
    throw std::runtime_error(what);
}
void paint(SDL_Renderer &renderer, Uint8 red, Uint8 green, Uint8 blue) {
  const SDL_FRect full{0, 0, 64, 32};
  require(SDL_SetRenderDrawColor(&renderer, red, green, blue, 255) &&
              SDL_RenderFillRect(&renderer, &full),
          "Paint synthetic image");
}
void pixel(SDL_Renderer &renderer, int x, int y, Uint8 red, Uint8 green,
           Uint8 blue, int tolerance = 2) {
  SDL_Surface *surface = SDL_RenderReadPixels(&renderer, nullptr);
  require(surface != nullptr, "Read rendered pixels");
  Uint8 r = 0, g = 0, b = 0, a = 0;
  const bool ok = SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a);
  SDL_DestroySurface(surface);
  require(ok && std::abs(int(r) - int(red)) <= tolerance &&
              std::abs(int(g) - int(green)) <= tolerance &&
              std::abs(int(b) - int(blue)) <= tolerance,
          "Synthetic pixel mismatch");
}
void test() {
  require(SDL_Init(SDL_INIT_VIDEO), "Initialize SDL video");
  SDL_Surface *back = SDL_CreateSurface(64, 32, SDL_PIXELFORMAT_RGBA32);
  require(back != nullptr, "Create output surface");
  SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(back);
  require(renderer != nullptr, "Create software GPU-contract renderer");
  const std::uint8_t tinyRgba[] = {11, 22, 33, 255, 11, 22, 33, 255,
                                   11, 22, 33, 255, 11, 22, 33, 255};
  require(LucaSystem::uploadAirStaticRgba(*renderer, 2, 2, nullptr, 8,
                                          SDL_SCALEMODE_NEAREST) == nullptr &&
              LucaSystem::uploadAirStaticRgba(*renderer, 2, 2, tinyRgba, 3,
                                              SDL_SCALEMODE_NEAREST) == nullptr,
          "Reject invalid RGBA uploads before allocating a GPU cache entry");
  SDL_Texture *uploaded = LucaSystem::uploadAirStaticRgba(
      *renderer, 2, 2, tinyRgba, 8, SDL_SCALEMODE_NEAREST);
  require(uploaded != nullptr, "Upload a complete RGBA texture");
  require(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) &&
              SDL_RenderClear(renderer),
          "Clear static texture test");
  const SDL_FRect sampleArea{0, 0, 64, 32};
  require(SDL_RenderTexture(renderer, uploaded, nullptr, &sampleArea),
          "Present uploaded RGBA image");
  pixel(*renderer, 32, 16, 11, 22, 33);
  SDL_DestroyTexture(uploaded);

  SDL_Texture *incoming = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_TARGET, 64, 32);
  require(incoming && SDL_SetTextureBlendMode(incoming, SDL_BLENDMODE_BLEND),
          "Create GPU-compatible incoming framebuffer");

  require(SDL_SetRenderDrawColor(renderer, 7, 13, 29, 255) &&
              SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND),
          "Set caller's renderer state");
  require(LucaSystem::renderAirSceneTarget(
              *renderer, *incoming, [&] { paint(*renderer, 0, 0, 255); }),
          "Prepare an isolated blue scene");
  Uint8 r = 0, g = 0, b = 0, a = 0;
  SDL_BlendMode blend = SDL_BLENDMODE_NONE;
  require(SDL_GetRenderTarget(renderer) == nullptr &&
              SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a) &&
              SDL_GetRenderDrawBlendMode(renderer, &blend) && r == 7 &&
              g == 13 && b == 29 && blend == SDL_BLENDMODE_BLEND,
          "Offscreen scene render must restore caller draw state");

  require(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) &&
              SDL_RenderClear(renderer),
          "Clear presentation");
  paint(*renderer, 255, 0, 0);
  const SDL_FRect full{0, 0, 64, 32};
  require(LucaSystem::blendAirSceneTarget(*renderer, *incoming, 0.5f, full),
          "Blend two composed scenes at half-time");
  pixel(*renderer, 32, 16, 127, 0, 128, 3);
  Uint8 alpha = 0;
  require(SDL_GetTextureAlphaMod(incoming, &alpha) && alpha == 255,
          "Restore framebuffer alpha modification");

  require(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) &&
              SDL_RenderClear(renderer),
          "Clear wipe presentation");
  const SDL_Rect parent{8, 0, 48, 32};
  require(SDL_SetRenderClipRect(renderer, &parent), "Set outer UI clip");
  require(LucaSystem::renderAirHorizontalWipe(
              *renderer, 64, 32, 0.5f, [&] { paint(*renderer, 255, 0, 0); },
              [&] { paint(*renderer, 0, 0, 255); }),
          "Draw non-overlapping WIPE at halfway");
  SDL_Rect actual{};
  require(SDL_RenderClipEnabled(renderer) &&
              SDL_GetRenderClipRect(renderer, &actual) &&
              SDL_RectsEqual(&parent, &actual),
          "WIPE must restore the caller's clip");
  require(SDL_SetRenderClipRect(renderer, nullptr),
          "Remove outer clip for pixel assertions");
  pixel(*renderer, 10, 16, 0, 0, 255);
  pixel(*renderer, 52, 16, 255, 0, 0);
  pixel(*renderer, 6, 16, 0, 0, 0);
  pixel(*renderer, 57, 16, 0, 0, 0);

  // The seen170 regression: transparent foreground holes must reveal the
  // prior exterior, not the framebuffer's black clear after INIT + DRAW.
  struct SceneSlot {
    std::uint8_t group = 0, slot = 0;
    std::uint32_t assetId = 0;
  };
  std::map<std::uint16_t, SceneSlot> previous{{0, {0, 0, 100}}};
  std::map<std::uint16_t, SceneSlot> current{{0, {0, 0, 200}}};
  require(LucaSystem::retainAirBackdrop(
              current, previous,
              [](const SceneSlot &slot) { return slot.assetId == 100; }),
          "Keep original opaque scene underneath alpha-cutout foreground");
  std::uint8_t cutoutPixels[4 * 4 * 4]{};
  for (int p = 0; p < 16; ++p) {
    cutoutPixels[p * 4] = 240;
    cutoutPixels[p * 4 + 1] = 60;
    cutoutPixels[p * 4 + 2] = 30;
    cutoutPixels[p * 4 + 3] =
        ((p / 4 == 1 || p / 4 == 2) && (p % 4 == 1 || p % 4 == 2)) ? 0 : 255;
  }
  SDL_Texture *frameCutout = LucaSystem::uploadAirStaticRgba(
      *renderer, 4, 4, cutoutPixels, 16, SDL_SCALEMODE_NEAREST);
  require(frameCutout != nullptr, "Create alpha-cutout foreground image");
  require(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) &&
              SDL_RenderClear(renderer),
          "Start on black backbuffer");
  const SDL_FRect doorArea{8, 4, 48, 24};
  for (const auto key : LucaSystem::orderedAirLayerKeys(current)) {
    if (current.at(key).assetId == 100)
      paint(*renderer, 25, 120, 70);
    else if (current.at(key).assetId == 200)
      require(SDL_RenderTexture(renderer, frameCutout, nullptr, &doorArea),
              "Render CZ4-shaped transparent foreground above old background");
  }
  pixel(*renderer, 32, 16, 25, 120, 70);
  pixel(*renderer, 10, 7, 240, 60, 30);
  SDL_DestroyTexture(frameCutout);

  SDL_DestroyTexture(incoming);
  SDL_DestroyRenderer(renderer);
  SDL_DestroySurface(back);
  SDL_Quit();
}
} // namespace

int main() {
  try {
    test();
    std::cout << "LucaSystem GPU composition pixel tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "LucaSystem GPU composition failed: " << error.what() << '\n';
    return 1;
  }
}
