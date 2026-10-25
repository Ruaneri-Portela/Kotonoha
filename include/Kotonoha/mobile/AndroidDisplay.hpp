#pragma once

#include <vector>

namespace Kotonoha {

struct AndroidDisplayMode {
  int modeId = 0;
  int width = 0;
  int height = 0;
  float refreshRate = 0.0f;
};

#if defined(__ANDROID__) || defined(ANDROID)

std::vector<AndroidDisplayMode> AndroidEnumerateDisplayModes();
int AndroidCurrentDisplayModeId();
bool AndroidRequestDisplayMode(int modeId);

#else

inline std::vector<AndroidDisplayMode> AndroidEnumerateDisplayModes() {
  return {};
}
inline int AndroidCurrentDisplayModeId() { return 0; }
inline bool AndroidRequestDisplayMode(int) { return false; }

#endif

} // namespace Kotonoha
