#pragma once

#include <Kotonoha/media/upscaling/Upscaler.hpp>

namespace Kotonoha {

class MetalFXUpscaler final : public Upscaler {
protected:
  void configureBackend(const UpscaleSettings &value) override;
  bool renderInto(TextureRef input, TextureRef output) override;
  void clearBackendCache() override;
  SDL_PixelFormat workingFormat() const override;

public:
  explicit MetalFXUpscaler(RendererRef renderer);
  ~MetalFXUpscaler() override;
  UpscaleBackend backend() const override;
  bool isAvaliable() const override;
  static bool isAvaliable(RendererRef renderer);
};

// Apply MetalFX to a WHOLE rendered frame, once per presented frame.
//
// MetalFX is a temporal spatial scaler: it must see the complete composed
// scene. It is therefore never used by RenderUpscaledTexture()/Rotated(),
// which draw textures individually and now use the plain SDL path while the
// MetalFX backend is active. Hosts that select MetalFX must wire this helper
// into their frame presentation instead, in this order:
//
//   1. Render the entire low-resolution scene into `source` (a target texture
//      at the logical/scaled render size).
//   2. Call UpscaleWholeFrameWithMetalFX(renderer, source, output) once, where
//      `output` is a target texture at the final display size (it must be
//      strictly larger than `source`).
//   3. Present `output` with SDL_RenderTexture() to the window/backbuffer.
//
// Guarantees: safe to call on any renderer; returns false without throwing
// when MetalFX is unavailable (software renderer, unsupported OS/SDK or a
// build with the upscaler disabled). In that case present `source` directly
// and let SDL apply ordinary scaling. Initializes the MetalFX device on
// demand, so no MetalFXUpscaler instance is required. The temporal scaler
// cache is reused across calls for identical input/output dimensions.
bool UpscaleWholeFrameWithMetalFX(SDL_Renderer *renderer, SDL_Texture *source,
                                  SDL_Texture *output);

} // namespace Kotonoha
