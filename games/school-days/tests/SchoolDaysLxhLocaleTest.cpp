#include "SchoolDaysScenes.hpp"

#include <Kotonoha/parsers/Documents.hpp>
#include <Kotonoha/routing/SchoolDaysTranslationCoverage.hpp>

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using Gate = Kotonoha::SchoolDaysTranslationGate;
using Sources = Gate::DetailedSources;
namespace fs = std::filesystem;

void Require(bool valid, const char *why) {
  if (!valid) throw std::runtime_error(why);
}

struct Scratch {
  fs::path path = fs::temp_directory_path() /
      ("school-days-lxh-locale-" +
       std::to_string(SDL_GetPerformanceCounter()));
  Scratch() { fs::create_directories(path); }
  ~Scratch() {
    std::error_code error;
    fs::remove_all(path, error);
  }
  void Write(const std::string &file, const std::string &contents) const {
    std::ofstream output(path / file, std::ios::binary);
    output << contents;
    Require(bool(output), "Cannot write private synthetic locale fixture");
  }
};

void TestNativeBoundariesAndLayers() {
  // The two original and grafted strings share the SAME commercial NODE.
  // The complete native parser event chain, not a translator's row count,
  // determines which slots belong to LxH.
  const std::string source =
      "[SkipFRAME]=00:00:00;\n"
      "[PrintText]=00:00:01\tSekai\tHQ original.\t00:00:10;\n"
      "[EndRoll]=00:01:00\tCredits\t00:02:00;\n"
      "[PlayMovie]=00:02:01\tLxH/Media/05-A2-F00-LXH\t0\t00:03:00;\n"
      "[PrintText]=00:02:04\tKotonoha\tLxH fuente.\t00:02:12;\n"
      "[PrintText]=00:02:14\tSekai\tLxH fuente.\t00:02:22;\n"
      "[Next]=00:04:00;\n";
  Kotonoha::OrsDocument ors(
      Kotonoha_OrsParserBytes(source.data(), source.size()));
  const auto first = Gate::LxhFirstEvent(ors.get(), "05/05-A2-F00");
  Require(first.has_value(), "Authenticated native LxH movie was not detected");
  Require(!Gate::LxhFirstEvent(ors.get(), "03/05-A2-F00") &&
              !Gate::LxhFirstEvent(ors.get(), "05/05-A2-Z00"),
          "LxH movie leaked into a different scene or retail route");

  std::vector<std::size_t> ordinals;
  Sources slots;
  for (const auto *event = ors.get().data; event; event = event->next) {
    if (event->command != PRINT_TEXT || !event->data.print_text) continue;
    const auto ordinal = Gate::EventOrdinal(ors.get(), event);
    ordinals.push_back(ordinal);
    slots.push_back({23, ordinal, 0, event->data.print_text->text,
                     Gate::SourceSlot::Kind::PrintText, ordinal >= *first});
  }
  Require(slots.size() == 3 && !slots[0].lxh &&
              slots[1].lxh && slots[2].lxh,
          "LxH boundary did not preserve HQ and new slots separately");

  Kotonoha::Translation enBase, enDlc, empty;
  enDlc.mutableSections()["23"]["LxH fuente."] = "Default English.";
  enDlc.mutableSections()["23"][Gate::ContextKey(ordinals[1], 0)] =
      "First English epilogue line.";
  enDlc.mutableSections()["23"][Gate::ContextKey(ordinals[2], 0)] =
      "Second English epilogue line.";
  auto audit = Gate::CheckLayered(slots, enBase, empty, enDlc, empty, true);
  Require(audit.checked == 2 && audit.found == 2 && audit.contextual == 2 &&
              audit.certifiedForNonNativeDisplay(),
          "Native HQ English incorrectly requires 30,968 translated entries");
  // A Python provenance ordinal displaced by one parsed ORS command must not
  // pass merely because the same source also has a valid literal translation.
  enDlc.mutableSections()["23"][Gate::ContextKey(ordinals[1] + 1, 1)] =
      "Misindexed choice never present in this ORS";
  audit = Gate::CheckLayered(slots, enBase, empty, enDlc, empty, true);
  Require(audit.invalidDlcKeys == 1 &&
              !audit.certifiedForNonNativeDisplay(),
          "Orphaned/misindexed DLC context key incorrectly passed native audit");
  enDlc.mutableSections()["23"].erase(Gate::ContextKey(ordinals[1] + 1, 1));
  Require(Gate::TranslateLayered(enBase, enDlc, 23, ordinals[0], 0,
                                 "HQ original.", false) == "HQ original.",
          "DLC dictionary changed the native HQ English script");
  Require(Gate::TranslateLayered(enBase, enDlc, 23, ordinals[1], 0,
                                 "LxH fuente.", true) ==
              "First English epilogue line.",
          "DLC contextual override lost priority over its literal");

  const std::vector<Kotonoha::SubtitleCue> originalCues{
      {0, 1, "HQ original.", "Sekai", true},
      {1, 2, "LxH fuente.", "Kotonoha", true},
      {2, 3, "LxH fuente.", "Sekai", true}};
  const auto localized = SchoolDays::Subtitles::TranslateCues(
      originalCues, ordinals, 23, enBase, &enDlc, first);
  Require(localized[0].text == "HQ original." &&
              localized[1].text == "First English epilogue line." &&
              localized[2].text == "Second English epilogue line." &&
              localized[1].speaker == "Kotonoha" &&
              localized[2].speaker == "Sekai" &&
              localized[1].assText,
          "LxH subtitle localization changed the canonical ASS speakers");

  // A validated native Script.GPK/LxH GPK already owns localized ORS text.
  // It must not require an unrelated JSON sidecar at LOAD-time cue binding.
  const auto native = SchoolDays::Subtitles::TranslateCues(
      originalCues, ordinals, 23, empty, nullptr, first, "pt-BR", true);
  Require(native[0].text == "HQ original." &&
              native[1].text == "LxH fuente." &&
              native[1].speaker == "Kotonoha",
          "Native GPK subtitle text was translated again or lost its speaker");

  // Missing LxH text is an error even when English HQ has no JSON loaded.
  enDlc.mutableSections()["23"].erase(Gate::ContextKey(ordinals[2], 0));
  enDlc.mutableSections()["23"].erase("LxH fuente.");
  audit = Gate::CheckLayered(slots, enBase, empty, enDlc, empty, true);
  Require(audit.missing == 1 && !audit.certifiedForNonNativeDisplay(),
          "Partial English DLC was incorrectly certified");
  bool blocked = false;
  try {
    (void)Gate::TranslateLayered(enBase, enDlc, 23, ordinals[2], 0,
                                 "LxH fuente.", true);
  } catch (const std::runtime_error &) { blocked = true; }
  Require(blocked, "Missing English DLC entry leaked Spanish source text");

  // Explicit en.json is not the same as native/no-JSON English: all base
  // and LxH keys must exist, but identity with the English source is valid.
  enBase.mutableSections()["23"]["HQ original."] = "HQ original.";
  enDlc.mutableSections()["23"][Gate::ContextKey(ordinals[1], 0)] =
      "LxH fuente.";
  enDlc.mutableSections()["23"][Gate::ContextKey(ordinals[2], 0)] =
      "LxH fuente.";
  audit = Gate::CheckLayered(slots, enBase, empty, enDlc, empty, false);
  Require(!audit.certifiedForNonNativeDisplay() && audit.identical == 3,
          "Unreviewed identities in a non-English locale were certified");
  audit = Gate::CheckLayered(slots, enBase, empty, enDlc, empty, false, true);
  Require(audit.checked == 3 && audit.suppliedIdentical == 3 &&
              audit.certifiedForNonNativeDisplay(),
          "Explicit English JSON should accept complete source-identical text");
  enBase.mutableSections()["23"].erase("HQ original.");
  audit = Gate::CheckLayered(slots, enBase, empty, enDlc, empty, false, true);
  Require(audit.missing == 1 && !audit.certifiedForNonNativeDisplay(),
          "Explicit English JSON unexpectedly accepted an absent HQ entry");

  // Complete Spanish V4b remains usable with no separate es-lxh.json.
  Kotonoha::Translation esBase, esReviews;
  esBase.mutableSections()["23"]["HQ original."] = "Original del HQ.";
  esBase.mutableSections()["23"]["LxH fuente."] = "LxH fuente.";
  esReviews.mutableSections()["23"]["LxH fuente."] =
      "spanish_original_lxh";
  audit = Gate::CheckLayered(slots, esBase, esReviews, empty, empty, false);
  Require(audit.checked == 3 && audit.found == 3 &&
              audit.reviewedIdentical == 2 &&
              audit.certifiedForNonNativeDisplay(),
          "Full V4b Spanish JSON cannot retain its reviewed LxH originals");

  // DLC keys win over base contextual, base literal and global nodo.
  esBase.mutableSections()["23"][Gate::ContextKey(ordinals[1], 0)] =
      "Texto español base.";
  Kotonoha::Translation esDlc, esDlcReviews;
  esDlc.mutableSections()["23"]["LxH fuente."] = "Texto DLC.";
  audit = Gate::CheckLayered(slots, esBase, esReviews, esDlc, empty, false);
  Require(audit.overriddenBaseSlots == 2,
          "Replacing existing V4b lines with a Spanish sidecar went undetected");
  Require(Gate::TranslateLayered(esBase, esDlc, 23, ordinals[1], 0,
                                 "LxH fuente.", true) == "Texto DLC.",
          "DLC literal must override a base contextual entry");
  // The DLC sidecar has no generic nodo fallback: only the base JSON's
  // nodo section is the final fallback in the agreed precedence order.
  Kotonoha::Translation invalidGlobalDlc;
  invalidGlobalDlc.mutableSections()["nodo"]["LxH fuente."] = "Wrong DLC global";
  Require(Gate::TranslateLayered(esBase, invalidGlobalDlc, 23, ordinals[1], 0,
                                 "LxH fuente.", true) == "Texto español base.",
          "DLC nodo incorrectly bypassed base contextual override");
  audit = Gate::CheckLayered(slots, esBase, esReviews,
                             invalidGlobalDlc, empty, false);
  Require(audit.invalidDlcKeys == 1 &&
              !audit.certifiedForNonNativeDisplay(),
          "DLC nodo key unexpectedly passed the strict sidecar audit");
  esDlc.mutableSections()["23"]["LxH fuente."] = "LxH fuente.";
  esDlcReviews.mutableSections()["23"]["LxH fuente."] =
      "spanish_original_lxh";
  // Once the sidecar is byte-for-byte equivalent to the V4b result for
  // every occurrence, the compatibility guard permits it.
  esBase.mutableSections()["23"].erase(Gate::ContextKey(ordinals[1], 0));
  audit = Gate::CheckLayered(slots, esBase, esReviews, esDlc,
                             esDlcReviews, false);
  Require(audit.overriddenBaseSlots == 0 &&
              audit.certifiedForNonNativeDisplay(),
          "Legacy Spanish sidecar equivalence was not recognized");
  esDlc.mutableSections()["23"][Gate::ContextKey(ordinals[1], 0)] = "";
  blocked = false;
  try {
    (void)Gate::TranslateLayered(esBase, esDlc, 23, ordinals[1], 0,
                                 "LxH fuente.", true);
  } catch (const std::runtime_error &) { blocked = true; }
  Require(blocked, "Blank DLC contextual value unexpectedly used Spanish base");

  // A third locale follows exactly the same code path as Spanish.
  Kotonoha::Translation frBase, frDlc;
  frBase.mutableSections()["23"]["HQ original."] = "HQ français.";
  frDlc.mutableSections()["23"]["LxH fuente."] = "Épilogue français.";
  audit = Gate::CheckLayered(slots, frBase, empty, frDlc, empty, false);
  Require(audit.checked == 3 && audit.certifiedForNonNativeDisplay(),
          "Third synthetic locale requires special-cased English/Spanish code");
  frBase.mutableSections()["23"].erase("HQ original.");
  Require(!Gate::CheckLayered(slots, frBase, empty, frDlc, empty, false)
               .certifiedForNonNativeDisplay(),
          "Non-native HQ omission silently fell back to original English");
  // Even if a third locale embeds all HQ and grafted LxH occurrences in its
  // base JSON, the startup policy must demand an explicit independent sidecar.
  frBase.mutableSections()["23"]["HQ original."] = "HQ français.";
  frBase.mutableSections()["23"][Gate::ContextKey(ordinals[1], 0)] =
      "Épilogue français A.";
  frBase.mutableSections()["23"][Gate::ContextKey(ordinals[2], 0)] =
      "Épilogue français B.";
  Require(Gate::CheckLayered(slots, frBase, empty, empty, empty, false)
              .certifiedForNonNativeDisplay(),
          "Synthetic full FR base JSON unexpectedly has missing coverage");
  blocked = false;
  try {
    Gate::RequireLxhSidecar("fr", "");
  } catch (const std::runtime_error &) { blocked = true; }
  Require(blocked,
          "Full FR HQ JSON wrongly bypassed the mandatory LxH sidecar");
  blocked = false;
  try {
    Gate::RequireLxhSidecar("en", "");
  } catch (const std::runtime_error &) { blocked = true; }
  Require(blocked, "Native EN LxH wrongly permitted a missing DLC sidecar");
  Gate::RequireLxhSidecar("es", ""); // Spanish V4b legacy exception.
  Gate::RequireLxhSidecar("fr", "fr-lxh.json");
}

void TestPerLocaleSidecars() {
  Scratch scratch;
  scratch.Write("fr.json", "{\"23\":{\"HQ original.\":\"Français\"}}\n");
  scratch.Write("fr-lxh.json", "{\"23\":{\"LxH fuente.\":\"Épilogue\"}}\n");
  Require(Gate::LocaleId("") == "en" &&
              Gate::LocaleId("C:/Locales/es-ES.json") == "es" &&
              Gate::LocaleId("C:/Locales/zh-CN.json") == "zh-cn" &&
              Gate::LocaleId((scratch.path / "fr.json").string()) == "fr" &&
              Gate::LocaleId("translations/pt-br.json") == "pt-br",
          "Locale IDs must remain stable across the HQ/DLC file pair");
  const auto path = Gate::DlcJsonPath("fr", (scratch.path / "fr.json").string());
  Require(fs::equivalent(path, scratch.path / "fr-lxh.json"),
          "Third-language DLC was not found beside its HQ JSON");
  scratch.Write("zh-CN.json", "{\"23\":{\"HQ original.\":\"你好\"}}\n");
  scratch.Write("zh-CN-lxh.json", "{\"23\":{\"LxH fuente.\":\"结局\"}}\n");
  const auto chineseSidecar = Gate::DlcJsonPath(
      "zh-cn", (scratch.path / "zh-CN.json").string());
  Require(fs::equivalent(chineseSidecar, scratch.path / "zh-CN-lxh.json"),
          "Chinese LxH sidecar case did not match the selected base filename");
  Gate::RequireLxhSidecar("zh-cn", chineseSidecar);
  Kotonoha::Translation dictionary;
  dictionary.load(path);
  Require(Gate::TranslateLayered({}, dictionary, 23, 6, 0,
                                 "LxH fuente.", true) == "Épilogue",
          "Locale-specific DLC JSON cannot load with the native loader");
  // Both missing-adjacent cases must reach the installed fallback or return
  // empty; MSVC's is_regular_file(..., error) can set ENOENT for the miss.
  scratch.Write("es.json", "{\"23\":{\"HQ original.\":\"HQ español\"}}\n");
  const auto esOptional = Gate::DlcJsonPath(
      "es", (scratch.path / "es.json").string());
  Require(esOptional.empty() || fs::is_regular_file(esOptional),
          "Missing adjacent ES sidecar did not stay optional");
  Gate::RequireLxhSidecar("es", esOptional);
  const std::string unusedLocale = "qa-no-adjacent-sidecar-9743";
  scratch.Write(unusedLocale + ".json", "{}\n");
  const auto notInstalled = Gate::DlcJsonPath(
      unusedLocale, (scratch.path / (unusedLocale + ".json")).string());
  Require(notInstalled.empty(),
          "Missing adjacent and installed sidecars must return empty");
}
} // namespace

int main() {
  try {
    TestNativeBoundariesAndLayers();
    TestPerLocaleSidecars();
    std::cout << "SchoolDaysLxhLocaleTest PASS: native ORS boundary, HQ/"
                 "LxH priority, missing keys, V4b compatibility, third locale, "
                 "ASS speaker and sidecar resolution\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysLxhLocaleTest FAIL: " << error.what() << '\n';
    return 1;
  }
}
