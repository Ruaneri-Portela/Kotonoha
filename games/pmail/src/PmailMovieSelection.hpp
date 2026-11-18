#pragma once
#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace Kotonoha::PmailPort {

inline std::string upperAscii(std::string_view text) {
  std::string out(text);
  for (char &c : out)
    if (c >= 'a' && c <= 'z')
      c = char(c - 'a' + 'A');
  return out;
}

// Original pmail.exe table at 0x441358 contains all four PM01 variants.
// SYS 53 builds the same names from a script-supplied stem; the L_/S_ and
// I5/I3 suffixes are fixed by the retail movie mode. Exact CPU/configuration-
// dependent preference is still under investigation. Native builds prefer
// retail AVI; browsers prefer converted sidecars.
template <class Exists>
std::string selectMovie(std::string_view stem, Exists &&exists, bool browser,
                        std::string_view preferred = {}) {
  constexpr std::array<std::string_view, 4> suffixes{
      "L_I5", "S_I5", "L_I3", "S_I3"};
  constexpr std::array<std::string_view, 2> folders{"data/movie/", "movie/"};
  constexpr std::array<std::string_view, 4> native{
      ".avi", ".avi.webm", ".webm", ".mp4"};
  constexpr std::array<std::string_view, 4> web{
      ".avi.webm", ".webm", ".mp4", ".avi"};
  const std::string base = upperAscii(stem);

  auto find = [&](bool onlyPreferred) -> std::string {
    for (auto suffix : suffixes) {
      std::string full(base);
      full += suffix;
      if (onlyPreferred && full != preferred)
        continue;
      for (auto folder : folders)
        for (auto ext : (browser ? web : native)) {
          std::string name(folder);
          name += full;
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

template <class Exists>
std::string selectOpeningMovie(Exists &&exists, bool browser,
                               std::string_view preferred = {}) {
  return selectMovie("PM01", std::forward<Exists>(exists), browser, preferred);
}
} // namespace Kotonoha::PmailPort
