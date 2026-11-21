#pragma once

#include <Kotonoha/core/Settings.hpp>

namespace SchoolDays {

inline void applyMobileDisplayDefaults(Kotonoha::EngineSettings &settings) {
  settings.mode = Kotonoha::WindowMode::Borderless;
  settings.vsync = true;
  settings.renderScale = 0.75f;
  // The scaled Novel render target uses SDL linear sampling at factor 1.
  settings.upscaling.backend = Kotonoha::UpscaleBackend::Linear;
  settings.upscaling.factor = 1.0f;
}

} // namespace SchoolDays
