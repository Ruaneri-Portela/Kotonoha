#include <Kotonoha/core/Config.hpp>
#include <Kotonoha/media/upscaling/MetalFXUpscaler.hpp>
#include <stdexcept>

namespace Kotonoha {

#if defined(__APPLE__) && defined(KOTONOHA_HAS_METALFX) &&                     \
    KOTONOHA_ENABLE_UPSCALER
// The real implementation (including UpscaleWholeFrameWithMetalFX) is in
// MetalFXUpscaler.mm for Apple builds with the MetalFX framework available.
#else
// No Metal device or no backend: whole-frame upscale is a documented no-op
// so hosts can call it unconditionally once per frame on every platform.
bool UpscaleWholeFrameWithMetalFX(SDL_Renderer *, SDL_Texture *,
                                  SDL_Texture *) {
  return SDL_SetError(
      "UpscaleWholeFrameWithMetalFX: MetalFX is unavailable in this build");
}
#endif

#if (!defined(__APPLE__) || !defined(KOTONOHA_HAS_METALFX)) &&                 \
    KOTONOHA_ENABLE_UPSCALER

MetalFXUpscaler::MetalFXUpscaler(RendererRef renderer) : Upscaler(renderer) {}

MetalFXUpscaler::~MetalFXUpscaler() {}

UpscaleBackend MetalFXUpscaler::backend() const {
  return UpscaleBackend::MetalFX;
}

bool MetalFXUpscaler::isAvaliable() const { return false; }

bool MetalFXUpscaler::isAvaliable(RendererRef) { return false; }

SDL_PixelFormat MetalFXUpscaler::workingFormat() const {
  return SDL_PIXELFORMAT_BGRA32;
}

void MetalFXUpscaler::configureBackend(const UpscaleSettings &) {}

bool MetalFXUpscaler::renderInto(TextureRef, TextureRef) {
  return SDL_SetError("MetalFX is unavailable in this build");
}

void MetalFXUpscaler::clearBackendCache() {}

#endif // !defined(__APPLE__) || !defined(KOTONOHA_HAS_METALFX)

} // namespace Kotonoha
