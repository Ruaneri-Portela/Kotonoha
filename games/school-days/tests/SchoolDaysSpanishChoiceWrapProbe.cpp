// Optional read-only local QA. Not a CTest fixture: the 29 retail packs,
// permanently integrated Packs/LxH.GPK, and community JSON are user-owned.
#include "SchoolDaysAssets.hpp"

#include <Kotonoha/localization/Translation.hpp>
#include <Kotonoha/routing/SchoolDaysChoiceTextPlan.hpp>
#include <Kotonoha/routing/SchoolDaysTranslationCoverage.hpp>

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
std::u16string AsUtf16(const std::string &utf8) {
  if (utf8.empty()) return {};
  char *converted = SDL_iconv_string(
      "UTF-16LE", "UTF-8", utf8.c_str(), utf8.size() + 1u);
  if (!converted)
    throw std::runtime_error("Invalid UTF-8 in Spanish choice");
  const std::u16string result(reinterpret_cast<const char16_t *>(converted));
  SDL_free(converted);
  return result;
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 4 && argc != 5 && argc != 6)
      throw std::runtime_error(
          "Usage: SchoolDaysSpanishChoiceWrapProbe <retail-data-dir> "
          "<ktrf-route> <spanish-json> [optional-display-overlay] "
          "[private-native-slot-dump.tsv]; use - for no display overlay");
    using Gate = Kotonoha::SchoolDaysTranslationGate;
    const auto overlay = argc >= 5 && std::string(argv[4]) != "-"
                             ? std::string(argv[4]) : std::string{};
    // Integrated endings are mounted unconditionally from Packs/LxH.GPK.
    // The optional overlay only affects ordinary remaster display resources.
    SchoolDays::Assets assets(argv[1], overlay);
    Kotonoha::Translation dictionary;
    dictionary.load(argv[3]);
    const auto sources = Gate::CollectDetailed(assets, argv[2]);
    if (argc == 6) {
      // A private diagnostic dump of the ACTUAL C++ ORS parser's sorted
      // event ordinals. No commercial text is written to the worktree or
      // displayed; the caller supplies an isolated scratch destination.
      const std::filesystem::path destination(argv[5]);
      if (std::filesystem::exists(destination))
        throw std::runtime_error("Refusing to overwrite native slot dump");
      std::ofstream dump(destination, std::ios::binary | std::ios::trunc);
      if (!dump) throw std::runtime_error("Cannot create native slot dump");
      dump << "node\teventOrdinal\tslot\tkind\tutf8hex\n";
      for (const auto &source : sources) {
        dump << source.node << '\t' << source.eventOrdinal << '\t'
             << source.slot << '\t'
             << (source.kind == Gate::SourceSlot::Kind::Choice ? 'C' : 'T')
             << '\t';
        for (const unsigned char ch : source.original)
          dump << std::uppercase << std::hex << std::setw(2)
               << std::setfill('0') << static_cast<unsigned>(ch);
        dump << std::dec << '\n';
      }
      if (!dump) throw std::runtime_error("Native slot dump truncated");
      std::cout << "NATIVE_SLOT_DUMP=" << destination.string() << '\n';
    }
    const auto reviews = Gate::LoadReviewedIdentities(argv[3]);
    const auto coverage = Gate::CheckDetailed(sources, dictionary, reviews);
    std::cout << "School Days native KTRF/ORS locale gate: "
              << coverage.summary() << '\n';
    std::size_t choices = 0, missing = 0, overFiveLines = 0;
    std::size_t widerThanScratch = 0, maxLines = 0;
    for (const auto &source : sources) {
      if (source.kind != Gate::SourceSlot::Kind::Choice) continue;
      ++choices;
      std::string translated;
      try {
        translated = Gate::Translate(dictionary, source.node,
                                     source.eventOrdinal, source.slot,
                                     source.original);
      } catch (const std::runtime_error &) {
        ++missing;
        continue;
      }
      const auto lines = Kotonoha::SchoolDaysChoiceTextPlan::WrapRetailEnglish(
          AsUtf16(translated));
      maxLines = std::max(maxLines, lines.size());
      if (lines.size() > 5) ++overFiveLines;
      bool overflow = false;
      for (const auto &line : lines)
        if (Kotonoha::SchoolDaysChoiceTextPlan::Measure(line.text) > 1024)
          overflow = true;
      if (overflow) ++widerThanScratch;
    }
    std::cout << "Spanish choice wrap scan: choices=" << choices
              << " untranslated=" << missing
              << " over_5_lines=" << overFiveLines
              << " glyph_width_over_1024=" << widerThanScratch
              << " max_lines=" << maxLines << '\n';
    return coverage.certifiedForNonNativeDisplay() &&
                   choices > 0 && missing == 0 && overFiveLines == 0 &&
                   widerThanScratch == 0
               ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << "Spanish choice wrap probe: " << error.what() << '\n';
    return 2;
  }
}
