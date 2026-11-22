// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <istream>
#include <ostream>
#include <set>
#include <stdexcept>
#include <utility>

namespace RealLive {

// Game-local read-skip history, stored with a RealLive presentation save.
// Old save versions lack this section and remain readable.
struct ReadProgress {
  using Page = std::pair<int, std::size_t>;
  bool bgmScriptPaused = false;
  Page currentPage{0, 0};
  std::set<Page> readPages;
};

inline bool validReadPage(const ReadProgress::Page &page) {
  // SEEN.TXT supports scenario numbers 0..9999. An instruction index
  // above 100M is not a realistic decoded scenario.
  return page.first >= 0 && page.first < 10000 &&
         page.second <= 100000000u;
}

inline void writeReadProgress(std::ostream &out, const ReadProgress &progress) {
  constexpr std::size_t maxPages = 1000000;
  if (!validReadPage(progress.currentPage) ||
      progress.readPages.size() > maxPages)
    throw std::out_of_range("RealLive read-progress data out of range");
  for (const auto &page : progress.readPages)
    if (!validReadPage(page))
      throw std::out_of_range("RealLive saved read page out of range");
  out << int(progress.bgmScriptPaused) << ' ' << progress.currentPage.first
      << ' ' << progress.currentPage.second << ' '
      << progress.readPages.size() << '\n';
  for (const auto &page : progress.readPages)
    out << page.first << ' ' << page.second << '\n';
  if (!out)
    throw std::runtime_error("Failed to serialize RealLive read history");
}

inline ReadProgress readReadProgress(std::istream &in) {
  ReadProgress progress;
  int paused = -1;
  std::size_t count = 0;
  if (!(in >> paused >> progress.currentPage.first >>
        progress.currentPage.second >> count) ||
      (paused != 0 && paused != 1) ||
      !validReadPage(progress.currentPage) || count > 1000000)
    throw std::runtime_error("Invalid RealLive read-progress header");
  progress.bgmScriptPaused = paused != 0;
  for (std::size_t i = 0; i < count; ++i) {
    ReadProgress::Page page;
    if (!(in >> page.first >> page.second) || !validReadPage(page) ||
        !progress.readPages.insert(page).second)
      throw std::runtime_error("Invalid or duplicate RealLive read page");
  }
  return progress;
}

}  // namespace RealLive
