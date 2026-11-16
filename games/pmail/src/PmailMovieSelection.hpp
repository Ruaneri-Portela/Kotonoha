#pragma once
#include <array>
#include <string>
#include <string_view>

namespace Kotonoha::PmailPort {

// Original pmail.exe table at 0x441358 contains all four movie filenames.
// Exact CPU/configuration-dependent preference is still under investigation.
// Native builds prefer retail AVI; browsers prefer converted sidecars.
template <class Exists>
std::string selectOpeningMovie(Exists&& exists, bool browser,
                               std::string_view preferred = {}) {
  constexpr std::array<std::string_view, 4> stems{
      "PM01L_I5", "PM01S_I5", "PM01L_I3", "PM01S_I3"};
  constexpr std::array<std::string_view, 2> folders{
      "data/movie/", "movie/"};
  constexpr std::array<std::string_view, 4> native{
      ".avi", ".avi.webm", ".webm", ".mp4"};
  constexpr std::array<std::string_view, 4> web{
      ".avi.webm", ".webm", ".mp4", ".avi"};

  auto find = [&](bool onlyPreferred) -> std::string {
    for (auto stem : stems) {
      if (onlyPreferred && stem != preferred)
        continue;
      for (auto folder : folders)
        for (auto ext : (browser ? web : native)) {
          std::string name(folder);
          name += stem;
          name += ext;
          if (exists(name))
            return name;
        }
    }
    return {};
  };
  // Port-only override for controlled comparisons with a given retail AVI.
  if (!preferred.empty()) {
    auto result = find(true);
    if (!result.empty())
      return result;
  }
  return find(false);
}
} // namespace Kotonoha::PmailPort
