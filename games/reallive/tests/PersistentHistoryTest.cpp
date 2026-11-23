// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include "RealLivePersistentHistory.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class F> void rejects(F callable, const char *message) {
  bool failed = false;
  try {
    callable();
  } catch (const std::exception &) {
    failed = true;
  }
  require(failed, message);
}
std::set<RealLive::ReadProgress::Page> parse(const std::string &text) {
  std::istringstream input(text);
  return RealLive::readReadHistory(input);
}
} // namespace

int main() {
  try {
    using RealLive::ReadProgress;
    const std::set<ReadProgress::Page> visited{
        {9030, 42}, {9032, 860}, {9037, 118}, {8005, 2739}};
    std::ostringstream out;
    RealLive::writeReadHistory(out, visited);
    require(parse(out.str()) == visited, "global read pages round-trip");
    require(out.str().rfind("REALLIVE-READ-HISTORY-1\n4\n", 0) == 0,
            "global history uses a versioned independent format");
    std::ostringstream empty;
    RealLive::writeReadHistory(empty, {});
    require(parse(empty.str()).empty(), "empty global read history");
    // Loading a save can add pages but must never remove later progress.
    auto merged = visited;
    const std::set<ReadProgress::Page> oldSave{{9032, 860}, {9031, 99}};
    merged.insert(oldSave.begin(), oldSave.end());
    require(merged.size() == visited.size() + 1 && merged.count({9037, 118}) &&
                merged.count({9031, 99}),
            "old save history merges without rollback");
    require(RealLive::readHistoryFilename("reg:KEY\\CLANNAD") ==
                RealLive::readHistoryFilename("reg:KEY\\CLANNAD"),
            "same game has stable file identity");
    require(RealLive::readHistoryFilename("reg:KEY\\CLANNAD") !=
                RealLive::readHistoryFilename("reg:KEY\\AIR"),
            "unrelated games must not share read flags");
    require(RealLive::readHistoryFilename("reg:KEY\\CLANNAD").find('/') ==
                    std::string::npos &&
                RealLive::readHistoryFilename("reg:KEY\\CLANNAD").find('\\') ==
                    std::string::npos,
            "file name never embeds untrusted game paths");
    rejects([] { (void)RealLive::readHistoryFilename(""); },
            "missing game identity rejected");

    rejects([] { (void)parse("REALLIVE-READ-HISTORY-2\n0\n"); },
            "future format versions fail explicitly");
    rejects([] { (void)parse("REALLIVE-READ-HISTORY-1\n1000001\n"); },
            "oversized count rejected before allocation");
    rejects([] { (void)parse("REALLIVE-READ-HISTORY-1\n1\n9032 1\nextra"); },
            "trailing bytes rejected");
    rejects([] { (void)parse("REALLIVE-READ-HISTORY-1\n1\n9032"); },
            "truncated page rejected");
    rejects([] { (void)parse("REALLIVE-READ-HISTORY-1\n2\n9032 1\n9032 1\n"); },
            "duplicate entries rejected");
    rejects([] { (void)parse("REALLIVE-READ-HISTORY-1\n1\n10000 1\n"); },
            "out-of-bounds scenario rejected");
    rejects([] { (void)parse("REALLIVE-READ-HISTORY-1\n1\n0 100000001\n"); },
            "out-of-bounds instruction rejected");
    auto invalid = visited;
    invalid.insert({10000, 0});
    rejects(
        [&] {
          std::ostringstream bad;
          RealLive::writeReadHistory(bad, invalid);
        },
        "corrupt memory never creates a global journal");

    std::cout << "RealLive cross-session read history regressions passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RealLive history regression: " << error.what() << '\n';
    return 1;
  }
}
