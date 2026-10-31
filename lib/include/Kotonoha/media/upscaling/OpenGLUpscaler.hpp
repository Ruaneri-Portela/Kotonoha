#pragma once

#include <Kotonoha/media/upscaling/Upscaler.hpp>

namespace Kotonoha {

class OpenGLUpscaler final : public Upscaler {
protected:
  void configureBackend(const UpscaleSettings &value) override;
  bool renderInto(TextureRef input, TextureRef output) override;
  Uint64 retryDeadline() const override;

public:
  explicit OpenGLUpscaler(RendererRef renderer);
  ~OpenGLUpscaler() override;
  UpscaleBackend backend() const override;
  bool isAvaliable() const override;
  static bool isAvaliable(RendererRef renderer);
};

} // namespace Kotonoha
