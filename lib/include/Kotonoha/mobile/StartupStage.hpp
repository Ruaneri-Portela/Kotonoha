#pragma once

namespace Kotonoha::Mobile {
// Numeric values are part of the Android Activity result protocol.
enum class StartupStage {
  Ready = 0,
  Files = 1,
  Script = 2,
  Runtime = 3,
  Language = 4
};
#ifdef __ANDROID__
void setStartupStage(StartupStage stage) noexcept;
#else
inline void setStartupStage(StartupStage) noexcept {}
#endif
} // namespace Kotonoha::Mobile
