// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace KotonohaLauncher::detail {

// std::filesystem::path::u8string returns std::string in C++17 and
// std::u8string in C++20. Preserve the UTF-8 bytes under either standard.
inline std::string pathUtf8(const std::filesystem::path &path) {
  const auto utf8 = path.u8string();
  return std::string(reinterpret_cast<const char *>(utf8.data()), utf8.size());
}

// Decode UTF-8 without relying on the process code page or the deprecated
// C++20 u8path overload.
inline std::filesystem::path pathFromUtf8(std::string_view utf8) {
#if defined(__cpp_char8_t)
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
#else
  return std::filesystem::u8path(utf8);
#endif
}

} // namespace KotonohaLauncher::detail
