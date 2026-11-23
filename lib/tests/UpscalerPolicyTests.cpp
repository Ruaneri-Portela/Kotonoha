#include <Kotonoha/core/Config.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/media/upscaling/MetalFXUpscaler.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace Kotonoha;

void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
}

// No Metal device exists in a headless test, so emulate a whole-frame-only
// pipeline with a real Upscaling instance (Linear, factor 2) whose policy
// reports false. The "kotonoha.upscale-cache" property on the source texture
// tells us whether RenderUpscaledTexture actually entered the pipeline.
class PolicyProbe final : public Upscaling {
public:
  bool perTexture = true;
  using Upscaling::Upscaling;
  bool supportsPerTexture() const override { return perTexture; }
};

void backendPolicy() {
  require(!SupportsPerTexture(UpscaleBackend::MetalFX),
          "MetalFX is whole-frame only");
  require(SupportsPerTexture(UpscaleBackend::MPV),
          "MPV shader chains are per-texture safe");
  require(SupportsPerTexture(UpscaleBackend::Linear),
          "Linear copies are per-texture safe");
  require(SupportsPerTexture(UpscaleBackend::None),
          "No backend has no per-texture pipeline to reject");
  require(SupportsPerTexture(UpscaleBackend::Auto),
          "Auto is resolved to a concrete backend before drawing");
}

void drawPolicy() {
#if KOTONOHA_ENABLE_UPSCALER
  SurfacePtr surface(SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA32));
  RendererPtr renderer(SDL_CreateSoftwareRenderer(surface.get()));
  require(bool(surface) && bool(renderer), "Create headless software renderer");
  PolicyProbe pipeline(*renderer);
  pipeline.configure({UpscaleBackend::Linear, UpscalePreset::Anime4K, 2, {}});
  require(pipeline.activeBackend() == UpscaleBackend::Linear &&
              pipeline.supportsPerTexture() &&
              pipeline.getImplementation()->supportsPerTexture(),
          "Active per-texture backend reports Upscaler and Upscaling policies");
  TexturePtr source(SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_STATIC, 8, 8));
  require(bool(source), "Create source texture");
  const SDL_FRect destination{0, 0, 32, 32};

  // MetalFX policy: no per-texture processing, plain SDL_RenderTexture draw.
  pipeline.perTexture = false;
  require(RenderUpscaledTexture(renderer.get(), source.get(), nullptr,
                                &destination, 1),
          "Whole-frame-only backend draws through the plain SDL path");
  require(!SDL_GetPointerProperty(SDL_GetTextureProperties(source.get()),
                                  "kotonoha.upscale-cache", nullptr),
          "Whole-frame-only backend never creates a per-texture upscale cache");

  // Rotated entry point shares the same policy branch.
  require(RenderUpscaledTextureRotated(renderer.get(), source.get(), nullptr,
                                       &destination, 15.0, nullptr,
                                       SDL_FLIP_NONE, 1),
          "Rotated draw also uses the plain SDL path for MetalFX");
  require(!SDL_GetPointerProperty(SDL_GetTextureProperties(source.get()),
                                  "kotonoha.upscale-cache", nullptr),
          "Rotated whole-frame-only draw never enters the pipeline");

  // A per-texture-capable backend still uses the pipeline (regression guard).
  pipeline.perTexture = true;
  require(RenderUpscaledTexture(renderer.get(), source.get(), nullptr,
                                &destination, 2),
          "Per-texture-capable backend keeps using the pipeline");
  require(SDL_GetPointerProperty(SDL_GetTextureProperties(source.get()),
                                 "kotonoha.upscale-cache", nullptr) != nullptr,
          "Per-texture-capable backend creates its upscale cache");

  // The host-facing whole-frame helper is a safe no-op without a Metal device.
  require(!UpscaleWholeFrameWithMetalFX(renderer.get(), source.get(),
                                        source.get()),
          "Whole-frame MetalFX helper fails safely without MetalFX");
#endif
}
} // namespace

int main() {
  try {
    backendPolicy();
    drawPolicy();
    std::cout
        << "Upscaler per-texture policy and whole-frame fallback passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Upscaler policy tests: " << error.what() << '\n';
    return 1;
  }
}
