// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <string>

namespace KotonohaLauncher::detail {

// std::filesystem::path::u8string returns std::string in C++17 and
// std::u8string in C++20. Preserve the UTF-8 bytes under either standard.
inline std::string pathUtf8(const std::filesystem::path &path) {
  const auto utf8 = path.u8string();
  return std::string(reinterpret_cast<const char *>(utf8.data()), utf8.size());
}

} // namespace KotonohaLauncher::detail
