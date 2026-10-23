#pragma once
#include <Kotonoha/specials/Upscaling.h>

#include <SDL3/SDL.h>

namespace Kotonoha {

// A picture or video frame drawn into a canvas through the engine upscaler.
// The upscaled copy is rebuilt only when the source changes (Touch) or the
// upscaling settings change; turning upscaling off releases it.
class CanvasImage {
public:
  CanvasImage() = default;
  explicit CanvasImage(SDL_Texture *texture, bool owned = true) { Reset(texture, owned); }
  ~CanvasImage() { Reset(nullptr); }
  CanvasImage(const CanvasImage &) = delete;
  CanvasImage &operator=(const CanvasImage &) = delete;
  CanvasImage(CanvasImage &&other) noexcept;
  CanvasImage &operator=(CanvasImage &&other) noexcept;

  // Replaces the source (destroyed with the image when owned).
  void Reset(SDL_Texture *texture, bool owned = true);
  // The source pixels changed, e.g. a new video frame was uploaded.
  void Touch() { ++serial_; }
  SDL_Texture *Source() const { return source_; }
  explicit operator bool() const { return source_ != nullptr; }
  // The upscaled texture when upscaling is on, otherwise the source.
  SDL_Texture *Display(SDL_Renderer *renderer);
  // Like SDL_RenderTexture on the source: src is in source pixels, and its
  // color/alpha mod and blend mode apply.
  bool Draw(SDL_Renderer *renderer, const SDL_FRect *src, const SDL_FRect *dst);

private:
  SDL_Texture *source_ = nullptr;
  bool owned_ = false;
  Uint64 serial_ = 1;
  Kotonoha_UpscaledTexture cache_{};
};

} // namespace Kotonoha
