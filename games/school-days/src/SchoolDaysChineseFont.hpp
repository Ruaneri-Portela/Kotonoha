#pragma once

#include <Kotonoha/core/SDL.hpp>

#ifdef _WIN32
#include <filesystem>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Kotonoha {

// Keep the returned font alive while it is registered with the primary font.
inline FontPtr AttachSchoolDaysChineseFallback(TTF_Font *primary) {
#ifdef _WIN32
  if (!primary) return {};
  wchar_t directory[MAX_PATH];
  const UINT length = GetSystemWindowsDirectoryW(directory, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return {};

  const auto path =
      (std::filesystem::path(directory) / L"Fonts" / L"msyh.ttc").u8string();
  FontPtr fallback{TTF_OpenFont(reinterpret_cast<const char *>(path.c_str()),
                                TTF_GetFontSize(primary))};
  if (fallback && TTF_AddFallbackFont(primary, fallback.get()))
    return fallback;
#else
  (void)primary;
#endif
  return {};
}

} // namespace Kotonoha
