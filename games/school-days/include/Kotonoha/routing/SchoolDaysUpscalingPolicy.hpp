#pragma once

#include <Kotonoha/core/Settings.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace Kotonoha {

// School Days' Windows setup and its in-game Display page share this policy.
// SDL Direct3D12/Direct3D11 can upscale with Linear, but cannot execute the
// GLSL/MPV Anime4K and ArtCNN presets (which require an OpenGL context).
struct SchoolDaysUpscalingPolicy {
  static bool IsOpenGLRenderer(const std::string &renderer) {
    return renderer == "opengl" || renderer == "opengles2";
  }

  static bool IncompatibleMpvSelection(const EngineSettings &settings) {
    return settings.upscaling.backend == UpscaleBackend::MPV &&
           !IsOpenGLRenderer(settings.renderer);
  }

  // A manual renderer choice wins if made AFTER a shader selection. Keep the
  // preset for a future OpenGL selection, but do not retain an impossible MPV
  // backend that the Engine would silently downgrade only in memory.
  static void SelectRenderer(EngineSettings &settings,
                             std::string renderer) {
    settings.renderer = std::move(renderer);
    if (IncompatibleMpvSelection(settings))
      settings.upscaling.backend = UpscaleBackend::Auto;
  }

  // Choosing a GLSL preset is an explicit request to USE that effect, not to
  // save its name while effectively disabling it on Direct3D12 or at 1.0x.
  static void SelectPreset(EngineSettings &settings, UpscalePreset preset) {
    settings.renderer = "opengl";
    settings.upscaling.backend = UpscaleBackend::MPV;
    settings.upscaling.preset = preset;
    settings.upscaling.shaderFiles.clear();
    settings.upscaling.factor = std::max(2.0f, settings.upscaling.factor);
  }

  static bool UsesShaders(const EngineSettings &settings) {
    return settings.upscaling.enabled() &&
           IsOpenGLRenderer(settings.renderer) &&
           (settings.upscaling.backend == UpscaleBackend::MPV ||
            settings.upscaling.backend == UpscaleBackend::Auto);
  }
};

} // namespace Kotonoha
