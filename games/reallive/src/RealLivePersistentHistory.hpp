// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include "RealLiveReadProgress.hpp"

#include <cstdint>
#include <iomanip>
#include <istream>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace RealLive {

inline constexpr const char kReadHistoryVersion[] = "REALLIVE-READ-HISTORY-1";
inline constexpr std::size_t kMaxReadHistoryPages = 1000000;

// The REGNAME identity follows an installed game even when its path changes.
// For editions without REGNAME, the caller supplies a canonical mount path.
// Never use untrusted game text directly as part of a filesystem filename.
inline std::string readHistoryFilename(std::string_view identity) {
  if (identity.empty())
    throw std::invalid_argument("RealLive read-history identity is empty");
  std::uint64_t digest = UINT64_C(14695981039346656037);
  for (unsigned char ch : identity) {
    digest ^= ch;
    digest *= UINT64_C(1099511628211);
  }
  std::ostringstream filename;
  filename << "reallive-read-" << std::hex << std::setw(16)
           << std::setfill('0') << digest << ".progress";
  return filename.str();
}

// Global history intentionally contains *only* already-read pages.
// Voice pauses and the currently visible page are save-slot-local state.
inline void writeReadHistory(std::ostream &out,
                             const std::set<ReadProgress::Page> &pages) {
  if (pages.size() > kMaxReadHistoryPages)
    throw std::out_of_range("RealLive read-history page count exceeded");
  for (const auto &page : pages)
    if (!validReadPage(page))
      throw std::out_of_range("Invalid RealLive read-history page");
  out << kReadHistoryVersion << '\n' << pages.size() << '\n';
  for (const auto &page : pages)
    out << page.first << ' ' << page.second << '\n';
  if (!out)
    throw std::runtime_error("Failed to write RealLive read history");
}

inline std::set<ReadProgress::Page> readReadHistory(std::istream &in) {
  std::string version;
  std::size_t count = 0;
  if (!(in >> version >> count) ||
      version != kReadHistoryVersion || count > kMaxReadHistoryPages)
    throw std::runtime_error("Invalid RealLive read-history header");

  std::set<ReadProgress::Page> result;
  for (std::size_t index = 0; index < count; ++index) {
    ReadProgress::Page page;
    if (!(in >> page.first >> page.second) ||
        !validReadPage(page) || !result.insert(page).second)
      throw std::runtime_error("Invalid RealLive read-history page");
  }
  in >> std::ws;
  if (!in.eof())
    throw std::runtime_error("Trailing data in RealLive read history");
  return result;
}

} // namespace RealLive
