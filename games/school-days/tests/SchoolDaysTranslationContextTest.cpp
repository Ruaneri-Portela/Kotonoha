#include "SchoolDaysScenes.hpp"

#include <Kotonoha/parsers/Documents.hpp>
#include <Kotonoha/routing/SchoolDaysTranslationCoverage.hpp>

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Gate = Kotonoha::SchoolDaysTranslationGate;
namespace fs = std::filesystem;

void Require(bool condition, const char *why) {
  if (!condition) throw std::runtime_error(why);
}

void Write(const fs::path &file, const std::string &value) {
  fs::create_directories(file.parent_path());
  std::ofstream output(file, std::ios::binary | std::ios::trunc);
  output << value;
  Require(bool(output), "Cannot write isolated translation test fixture");
}

struct Scratch {
  fs::path root = fs::temp_directory_path() /
      ("school-days-translation-context-" +
       std::to_string(SDL_GetPerformanceCounter()));
  Scratch() { fs::create_directories(root); }
  ~Scratch() {
    std::error_code error;
    fs::remove_all(root, error);
  }
};

void VerifyContextAndColors() {
  // All five native ORS commands, including SkipFRAME/Next, count in
  // @ors:<eventOrdinal>:<slot>. SetSELECT's two visible options are 0 and 1.
  const std::string source =
      "[SkipFRAME]=00:00:00;\n"
      "[PrintText]=00:00:01\tSekai\tSame source.\t00:01:00;\n"
      "[Next]=00:01:00;\n"
      "[PrintText]=00:01:01\tSekai\tSame source.\t00:02:00;\n"
      "[SetSELECT]=00:02:00\tStay here\tLeave now\t00:03:00;\n"
      "[PrintText]=00:03:01\tMakoto\tMakoto\t00:04:00;\n";
  Kotonoha::OrsDocument document(
      Kotonoha_OrsParserBytes(source.data(), source.size()));
  std::vector<const Kotonoha_orsEvent *> events;
  for (auto *event = document.get().data; event; event = event->next)
    events.push_back(event);
  Require(events.size() == 6, "Native ORS parse changed event count");
  Require(events[1]->command == PRINT_TEXT &&
              events[3]->command == PRINT_TEXT &&
              events[4]->command == SetSELECT &&
              events[4]->data.set_select->size == 2,
          "Native ORS parser lost subtitle or choice events");
  for (std::size_t i = 0; i < events.size(); ++i)
    Require(Gate::EventOrdinal(document.get(), events[i]) == i,
            "EventOrdinal excluded non-text parsed ORS commands");
  Kotonoha_orsEvent unrelated{};
  bool rejectedOutside = false;
  try { (void)Gate::EventOrdinal(document.get(), &unrelated); }
  catch (const std::runtime_error &) { rejectedOutside = true; }
  Require(rejectedOutside, "Unknown ORS event pointer was silently accepted");
  Require(Gate::ContextKey(4, 1) == "@ors:4:1",
          "Context key formatting drifted from JSON exporter");

  Kotonoha::Translation dictionary;
  auto &node = dictionary.mutableSections()["5"];
  node["Same source."] = "Generic line.";
  node["@ors:1:0"] = "Primera línea.";
  node["@ors:3:0"] = "Segunda línea.";
  node["Stay here"] = "Quédate aquí";
  node["@ors:4:1"] = "Vete ahora";
  node["Makoto"] = "Makoto";
  node["Sekai"] = "Someone else";  // Must not alter canonical ASS speaker.
  dictionary.mutableSections()["nodo"]["A shared line."] = "Una línea común.";

  Gate::DetailedSources sources{
      {5, 1, 0, "Same source."}, {5, 3, 0, "Same source."},
      {5, 4, 0, "Stay here"}, {5, 4, 1, "Leave now"},
      {5, 5, 0, "Makoto"}, {8, 2, 0, "A shared line."}};
  Kotonoha::Translation reviewed;
  auto coverage = Gate::CheckDetailed(sources, dictionary, reviewed);
  Require(coverage.checked == 6 && coverage.found == 6 &&
              coverage.missing == 0 && coverage.blank == 0 &&
              coverage.identical == 1 && coverage.contextual == 3 &&
              coverage.resolvedCollisions == 1 &&
              coverage.issues.size() == 1 &&
              coverage.issues.front().eventOrdinal == 5 &&
              coverage.issues.front().slot == 0 &&
              !coverage.certifiedForNonNativeDisplay(),
          "Unreviewed identity or contextual variant was incorrectly certified");
  reviewed.mutableSections()["nodo"]["Makoto"] = "proper_name";
  Require(!Gate::CheckDetailed(sources, dictionary, reviewed)
               .certifiedForNonNativeDisplay(),
          "Global nodo review incorrectly certified another NODE");
  reviewed.mutableSections()["5"]["Makoto"] = "unreviewed";
  Require(!Gate::CheckDetailed(sources, dictionary, reviewed)
               .certifiedForNonNativeDisplay(),
          "Unreviewed identity marker was accepted");
  reviewed.mutableSections()["5"]["Makoto"] = "looks_english";
  Require(!Gate::CheckDetailed(sources, dictionary, reviewed)
               .certifiedForNonNativeDisplay(),
          "Unknown identity reason was accepted");
  reviewed.mutableSections()["5"]["Makoto"] = "proper_name";
  coverage = Gate::CheckDetailed(sources, dictionary, reviewed);
  Require(coverage.certifiedForNonNativeDisplay() &&
              coverage.reviewedIdentical == 1 && coverage.identical == 0,
          "Reviewed identity failed otherwise-complete contextual corpus");

  const auto &c = dictionary;
  Require(Gate::Translate(c, 5, 1, 0, "Same source.") == "Primera línea." &&
              Gate::Translate(c, 5, 3, 0, "Same source.") == "Segunda línea." &&
              Gate::Translate(c, 5, 4, 0, "Stay here") == "Quédate aquí" &&
              Gate::Translate(c, 5, 4, 1, "Leave now") == "Vete ahora" &&
              Gate::Translate(c, 8, 2, 0, "A shared line.") ==
                  "Una línea común.",
          "Context precedence, SetSELECT slot or NODE/nodo fallback broken");

  // This is the same routine Subtitles::refreshLocalizedCues uses to produce
  // ASS cues, so canonical Sekai/Makoto style identifiers are preserved even
  // when the JSON also contains a misleading speaker-name translation.
  const std::vector<Kotonoha::SubtitleCue> originals{
      {0, 1, "Same source.", "Sekai", true},
      {1, 2, "Same source.", "Sekai", true},
      {2, 3, "Makoto", "Makoto", true}};
  const auto translated = SchoolDays::Subtitles::TranslateCues(
      originals, {1, 3, 5}, 5, dictionary);
  Require(translated.size() == 3 &&
              translated[0].text == "Primera línea." &&
              translated[1].text == "Segunda línea." &&
              translated[0].speaker == "Sekai" &&
              translated[1].speaker == "Sekai" &&
              translated[2].speaker == "Makoto" &&
              translated[0].assText && translated[2].assText,
          "Subtitle localization lost context or canonical ASS speaker colors");

  // BackLog, PlayData and the choice atlas call the same contextual routine;
  // their speaker attribution is supplied separately and remains canonical.
  const std::string backlogText = Gate::Translate(
      dictionary, 5, Gate::EventOrdinal(document.get(), events[3]), 0,
      events[3]->data.print_text->text);
  Require(backlogText == translated[1].text &&
              std::string(events[3]->data.print_text->character) == "Sekai",
          "BackLog contextual dialogue or speaker attribution drifted");

  node["@ors:1:0"] = "";
  coverage = Gate::CheckDetailed(sources, dictionary, reviewed);
  Require(coverage.blank == 1 && !coverage.certifiedForNonNativeDisplay(),
          "Blank contextual override silently fell back to generic literal");
  Require(coverage.issues.size() == 1 &&
              coverage.issues[0].eventOrdinal == 1 &&
              coverage.issues[0].slot == 0 &&
              coverage.issues[0].kind ==
                  Kotonoha::SchoolDaysTranslationIssue::Kind::Blank,
          "Blank contextual issue lost native ORS ordinal/slot diagnostics");
  bool blockedBlank = false;
  try { (void)Gate::Translate(dictionary, 5, 1, 0, "Same source."); }
  catch (const std::runtime_error &) { blockedBlank = true; }
  Require(blockedBlank, "Blank contextual override reached English fallback");
  node.erase("@ors:1:0");
  Require(Gate::Translate(dictionary, 5, 1, 0, "Same source.") ==
              "Generic line.",
          "Missing contextual override did not use NODE literal");
  node.erase("Stay here");
  bool blockedMissing = false;
  try { (void)Gate::Translate(dictionary, 5, 4, 0, "Stay here"); }
  catch (const std::runtime_error &) { blockedMissing = true; }
  Require(blockedMissing, "Missing choice leaked original English");

  Kotonoha::Translation originalEnglish;
  Require(Gate::Translate(originalEnglish, 5, 1, 0, "Same source.") ==
              "Same source.",
          "Native English no-JSON mode unexpectedly requires a translation");
}

void VerifyReviewedManifest() {
  Scratch scratch;
  const auto locale = scratch.root / "es.json";
  const auto reviewed = scratch.root / "es-reviewed-identities.json";
  Write(locale, "{\"nodo\":{\"Hello\":\"Hola\"}}\n");
  Require(Gate::LoadReviewedIdentities(locale.string()).getSections().empty(),
          "Absent review manifest was not treated as uncertified");
  Write(reviewed,
        "{\"5\":{\"Makoto\":\"proper_name\","
        "\"@ors:12:0\":\"spanish_original_lxh\"}}\n");
  const auto loaded = Gate::LoadReviewedIdentities(locale.string());
  Require(loaded.getSections().at("5").at("Makoto") == "proper_name" &&
              loaded.getSections().at("5").at("@ors:12:0") ==
                  "spanish_original_lxh",
          "Review manifest was not loaded from the locale directory");
  Kotonoha::Translation dictionary;
  dictionary.mutableSections()["5"]["Spanish original"] =
      "Spanish original";
  const auto reviewStatus = Gate::CheckDetailed(
      {{5, 12, 0, "Spanish original"}}, dictionary, loaded);
  Require(reviewStatus.certifiedForNonNativeDisplay() &&
              reviewStatus.reviewedIdentical == 1,
          "LxH native Spanish identity review was incorrectly rejected");
  Write(reviewed, "{invalid JSON");
  bool rejectedInvalid = false;
  try { (void)Gate::LoadReviewedIdentities(locale.string()); }
  catch (const std::exception &) { rejectedInvalid = true; }
  Require(rejectedInvalid, "Malformed reviewed-identities JSON was accepted");
}
} // namespace

int main() {
  try {
    VerifyContextAndColors();
    VerifyReviewedManifest();
    std::cout << "SchoolDaysTranslationContextTest PASS: ORS ordinals, "
                 "contextual collisions, choices, reviewed identities, "
                 "BackLog and canonical ASS color tags\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysTranslationContextTest FAIL: " << error.what()
              << '\n';
    return 1;
  }
}
