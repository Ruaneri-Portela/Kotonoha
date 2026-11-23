// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include "RealLiveReadProgress.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class F> void rejects(F operation, const char *message) {
  bool threw = false;
  try {
    operation();
  } catch (const std::exception &) {
    threw = true;
  }
  require(threw, message);
}
RealLive::ReadProgress parse(const std::string &bytes) {
  std::istringstream in(bytes);
  auto progress = RealLive::readReadProgress(in);
  in >> std::ws;
  require(in.eof(), "Read-progress must not consume trailing save fields");
  return progress;
}
} // namespace

int main() {
  try {
    RealLive::ReadProgress progress;
    progress.bgmScriptPaused = true;
    progress.currentPage = {9037, 118};
    progress.readPages = {{0, 0}, {9037, 118}, {9030, 52}, {8005, 2739}};
    std::ostringstream out;
    RealLive::writeReadProgress(out, progress);
    const auto restored = parse(out.str());
    require(restored.bgmScriptPaused == progress.bgmScriptPaused &&
                restored.currentPage == progress.currentPage &&
                restored.readPages == progress.readPages,
            "Read-skip history and script BGM pause must round-trip");

    RealLive::ReadProgress empty;
    std::ostringstream outEmpty;
    RealLive::writeReadProgress(outEmpty, empty);
    const auto reloadedEmpty = parse(outEmpty.str());
    require(reloadedEmpty.readPages.empty() && !reloadedEmpty.bgmScriptPaused,
            "Empty read history is valid");
    const auto plain = parse("0 9032 860 2\n9031 100\n9032 860\n");
    require(plain.currentPage == RealLive::ReadProgress::Page(9032, 860) &&
                plain.readPages.size() == 2,
            "Known native scenario/page addresses are retained");

    rejects([] { (void)parse("2 1 4 0\n"); },
            "script-paused flag must be binary");
    rejects([] { (void)parse("1 -1 4 0\n"); },
            "current scenario cannot be negative");
    rejects([] { (void)parse("0 10000 4 0\n"); },
            "current scenario must fit SEEN archive TOC");
    rejects([] { (void)parse("0 9037 100000001 0\n"); },
            "instruction offsets have a bounded limit");
    rejects([] { (void)parse("0 9032 100 1000001\n"); },
            "corrupt page count is bounded before allocation");
    rejects([] { (void)parse("0 9032 100 2\n9032 12\n"); },
            "truncated read history rejected");
    rejects([] { (void)parse("0 9032 100 2\n9032 12\n9032 12\n"); },
            "duplicate pages rejected");
    rejects([] { (void)parse("0 9032 100 1\n-1 1\n"); },
            "corrupt entry scenario rejected");

    auto invalid = progress;
    invalid.readPages.insert({10000, 0});
    rejects(
        [&] {
          std::ostringstream bad;
          RealLive::writeReadProgress(bad, invalid);
        },
        "writer rejects impossible scenario IDs");
    invalid = progress;
    invalid.currentPage = {-1, 100};
    rejects(
        [&] {
          std::ostringstream bad;
          RealLive::writeReadProgress(bad, invalid);
        },
        "writer rejects invalid current page");

    // Loading a previous save merges read pages into the current playthrough.
    auto current = plain.readPages;
    current.insert(restored.readPages.begin(), restored.readPages.end());
    require(current.size() == 6 && current.count({9037, 118}) &&
                current.count({9032, 860}),
            "restoring an older save does not roll back read progression");

    std::cout << "RealLive progress persistence regressions passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "RealLive progress regression: " << e.what() << '\n';
    return 1;
  }
}
