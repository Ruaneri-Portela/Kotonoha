#include <Kotonoha/media/upscaling/MetalFXUpscaler.hpp>
#include <stdexcept>

#if !defined(__APPLE__)

namespace Kotonoha {

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

} // namespace Kotonoha

#endif // !defined(__APPLE__)
