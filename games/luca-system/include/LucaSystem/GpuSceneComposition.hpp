// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <utility>

namespace LucaSystem {


// Upload the image completely before it enters any shared texture cache.
// On an SDL/GPU upload or sampler-state failure, destroy the incomplete
// texture immediately; otherwise subsequent frames may display random
// uninitialized VRAM from a partially constructed SDL_Texture.
inline SDL_Texture *uploadAirStaticRgba(SDL_Renderer &renderer, int width,
                                        int height, const void *rgba, int pitch,
                                        SDL_ScaleMode scale) {
  if (!rgba || width < 1 || height < 1 ||
      width > 16384 || height > 16384 || pitch < width * 4) {
    SDL_SetError("Invalid LucaSystem RGBA texture dimensions or stride");
    return nullptr;
  }
  SDL_Texture *candidate = SDL_CreateTexture(
      &renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
      width, height);
  if (!candidate)
    return nullptr;
  if (!SDL_UpdateTexture(candidate, nullptr, rgba, pitch) ||
      !SDL_SetTextureScaleMode(candidate, scale) ||
      !SDL_SetTextureBlendMode(candidate, SDL_BLENDMODE_BLEND)) {
    SDL_DestroyTexture(candidate);
    return nullptr;
  }
  return candidate; // caller owns via Kotonoha::TexturePtr (SDL_DestroyTexture)
}

// Render an *entire* scene to an SDL3 GPU/SDL render target while preserving
// the caller's texture target, draw color and draw blend mode. SDL retains
// viewport/clip/logical state per render target; on the private target they
// are always set to pixel-perfect 1:1 coordinates.
template <class Painter>
bool renderAirSceneTarget(SDL_Renderer &renderer, SDL_Texture &target,
                          Painter &&paint) {
  auto *previous = SDL_GetRenderTarget(&renderer);
  Uint8 r = 0, g = 0, b = 0, a = 0;
  SDL_BlendMode oldBlend = SDL_BLENDMODE_NONE;
  if (!SDL_GetRenderDrawColor(&renderer, &r, &g, &b, &a) ||
      !SDL_GetRenderDrawBlendMode(&renderer, &oldBlend) ||
      !SDL_SetRenderTarget(&renderer, &target))
    return false;
  const bool ready =
      SDL_SetRenderLogicalPresentation(
          &renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED) &&
      SDL_SetRenderViewport(&renderer, nullptr) &&
      SDL_SetRenderClipRect(&renderer, nullptr) &&
      SDL_SetRenderScale(&renderer, 1.0f, 1.0f) &&
      SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_NONE) &&
      SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255) &&
      SDL_RenderClear(&renderer);
  if (ready)
    std::forward<Painter>(paint)();
  const bool restoredTarget = SDL_SetRenderTarget(&renderer, previous);
  const bool restoredBlend = SDL_SetRenderDrawBlendMode(&renderer, oldBlend);
  const bool restoredColor = SDL_SetRenderDrawColor(&renderer, r, g, b, a);
  return ready && restoredTarget && restoredBlend && restoredColor;
}

// Alpha applies to the *finished scene*, never to each constituent sprite.
inline bool blendAirSceneTarget(SDL_Renderer &renderer, SDL_Texture &frame,
                                float fraction, const SDL_FRect &destination) {
  if (!std::isfinite(fraction))
    return false;
  Uint8 oldAlpha = 255;
  if (!SDL_GetTextureAlphaMod(&frame, &oldAlpha))
    return false;
  const Uint8 alpha = static_cast<Uint8>(std::lround(
      std::clamp(fraction, 0.0f, 1.0f) * 255.0f));
  const bool copied = SDL_SetTextureAlphaMod(&frame, alpha) &&
                      SDL_RenderTexture(&renderer, &frame, nullptr,
                                        &destination);
  const bool restored = SDL_SetTextureAlphaMod(&frame, oldAlpha);
  return copied && restored;
}

// Two non-overlapping device-space clip regions. In the left region paint
// *only* the incoming scene; in the right paint *only* the outgoing scene.
// This avoids dark halos from double-compositing partially transparent CZ4.
template <class DrawOutgoing, class DrawIncoming>
bool renderAirHorizontalWipe(SDL_Renderer &renderer, int width, int height,
                             float fraction, DrawOutgoing &&oldScene,
                             DrawIncoming &&newScene) {
  if (width <= 0 || height <= 0 || !std::isfinite(fraction))
    return false;
  const int x = std::clamp(
      static_cast<int>(std::lround(width *
          std::clamp(fraction, 0.0f, 1.0f))), 0, width);
  SDL_Rect prior{};
  const bool hadClip = SDL_RenderClipEnabled(&renderer);
  if (hadClip && !SDL_GetRenderClipRect(&renderer, &prior))
    return false;
  const auto draw = [&](SDL_Rect region, auto &&paint) {
    if (region.w <= 0 || region.h <= 0)
      return true;
    if (hadClip) {
      SDL_Rect intersection{};
      if (!SDL_GetRectIntersection(&region, &prior, &intersection))
        return true;
      region = intersection;
    }
    if (!SDL_SetRenderClipRect(&renderer, &region))
      return false;
    std::forward<decltype(paint)>(paint)();
    return true;
  };
  const bool oldOk = draw({x, 0, width - x, height},
                          std::forward<DrawOutgoing>(oldScene));
  const bool newOk = draw({0, 0, x, height},
                          std::forward<DrawIncoming>(newScene));
  const bool restored = SDL_SetRenderClipRect(
      &renderer, hadClip ? &prior : nullptr);
  return oldOk && newOk && restored;
}
} // namespace LucaSystem
