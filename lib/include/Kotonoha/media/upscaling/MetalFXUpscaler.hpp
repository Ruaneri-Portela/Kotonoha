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

} // namespace Kotonoha
