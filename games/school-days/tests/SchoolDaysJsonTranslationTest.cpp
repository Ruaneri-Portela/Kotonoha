#include "SchoolDaysAssets.hpp"
#include "SchoolDaysRuntime.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/localization/Translation.hpp>
#include <Kotonoha/parsers/Ors.h>
#include <Kotonoha/routing/SchoolDaysChoiceTextPlan.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {

void Require(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

struct TemporaryDirectory {
  fs::path root = fs::temp_directory_path() /
                  ("school-days-json-test-" +
                   std::to_string(SDL_GetPerformanceCounter()));
  TemporaryDirectory() {
    Require(fs::create_directories(root), "Cannot create isolated JSON fixture");
  }
  ~TemporaryDirectory() {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
};

void Write(const fs::path &path, const std::string &content) {
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(content.data(), static_cast<std::streamsize>(content.size()));
  Require(static_cast<bool>(file), "Cannot write JSON test fixture: " + path.string());
}

std::string Read(const fs::path &path) {
  std::ifstream file(path, std::ios::binary);
  Require(static_cast<bool>(file), "Cannot read JSON test fixture: " + path.string());
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool Contains(const Kotonoha::Translation::SourceTexts &sources,
              int nodeId, const std::string &text) {
  const auto found = sources.find(nodeId);
  return found != sources.end() &&
         std::find(found->second.begin(), found->second.end(), text) !=
             found->second.end();
}

std::string HostTestFont() {
  // The repository deliberately omits commercial/third-party font binaries.
  // Refer to an installed host font without copying it into fixtures or Git.
  if (const char *configured = SDL_getenv("KOTONOHA_TEST_FONT"))
    if (fs::is_regular_file(configured)) return configured;
  for (const char *candidate : {
           "C:/Windows/Fonts/arial.ttf",
           "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
           "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
           "/System/Library/Fonts/Supplemental/Arial.ttf"})
    if (fs::is_regular_file(candidate)) return candidate;
  throw std::runtime_error(
      "No installed test font; set KOTONOHA_TEST_FONT to a local TTF/OTF");
}

// Only synthetic ORS is written; the checked-in route is graph metadata, not
// commercial script/media. Separate paths avoid races with the PortTest fixture.
constexpr const char *kA00 =
    "[PrintText]=00:00:00\tNarrator\tShared line.\t00:01:00;\n"
    "[PrintText]=00:01:00\tNarrator\tA00 alone.\t00:02:00;\n"
    "[SkipFRAME]=00:02:00;\n"
    "[Next]=00:03:00;\n";
constexpr const char *kA03 =
    "[PrintText]=00:00:00\tNarrator\tShared line.\t00:02:00;\n"
    "[SkipFRAME]=00:02:00;\n"
    "[SetSELECT]=00:02:00\tStay here\tLeave now\t00:03:00;\n"
    "[Next]=00:04:00;\n";

void CheckUnmodifiedScript(const fs::path &path) {
  const auto raw = Read(path);
  Require(raw == kA03, "JSON translation modified the original ORS bytes");
  auto document = Kotonoha_OrsParserBytes(raw.data(), raw.size());
  bool foundCaption = false, foundChoice = false;
  for (const auto *event = document.data; event; event = event->next) {
    if (event->command == PRINT_TEXT && event->data.print_text) {
      foundCaption = true;
      Require(std::string(event->data.print_text->text) == "Shared line.",
              "Translated PRINT_TEXT contaminated the parsed raw ORS");
    }
    if (event->command == SetSELECT && event->data.set_select) {
      foundChoice = true;
      Require(event->data.set_select->size == 2 &&
                  std::string(event->data.set_select->options[0]) == "Stay here" &&
                  std::string(event->data.set_select->options[1]) == "Leave now",
              "Translated SET_SELECT contaminated routing choice options");
    }
  }
  Kotonoha_OrsClean(&document);
  Require(foundCaption && foundChoice, "Fixture ORS parser lost dialogue or choice");
}

void VerifyChoiceGeometry(const std::string &translated) {
  using Plan = Kotonoha::SchoolDaysChoiceTextPlan;
  // ASCII fixture so the test probes the retail glyph/line plan without
  // depending on the user's fonts or on Windows code-page conversions.
  const std::u16string text(translated.begin(), translated.end());
  const auto lines = Plan::WrapRetailEnglish(text);
  Require(!lines.empty() && lines.size() <= 5,
          "Translated choice cannot use the standard choice scratch layout");
  Require(Plan::Measure(lines.front().text) > 0,
          "Translated choice measured as an empty line");
  const auto top = Plan::DestinationForLine(2, 0, 0,
                                           static_cast<int>(lines.size()),
                                           lines.front().horizontalOffset);
  const auto bottom = Plan::DestinationForLine(2, 1, 0,
                                              static_cast<int>(lines.size()),
                                              lines.front().horizontalOffset);
  Require(top.y < bottom.y && top.h > 0.0f && bottom.h > 0.0f,
          "Localized options changed the original top/bottom layout");
}

void VerifyJsonIntegration(const char *route) {
  TemporaryDirectory temp;
  const auto scripts = temp.root / "Script" / "English" / "00";
  const auto first = scripts / "00-00-A00.ENG.ORS";
  const auto choice = scripts / "00-00-A03.ENG.ORS";
  Write(first, kA00);
  Write(choice, kA03);
  const auto documents = temp.root / "documents";
  fs::create_directories(documents);

  auto assets = std::make_shared<SchoolDays::Assets>(temp.root);
  SchoolDays::Runtime runtime(assets, route, documents);
  Require(runtime.debugNode() == "00/00-00-A00", "Fixture failed to open A00");
  const int a00 = runtime.translationRoute();
  Require(a00 >= 0, "A00 lacks a stable KTRF node translation ID");
  runtime.debugJumpToNode("00/00-00-A03");
  const int a03 = runtime.translationRoute();
  Require(runtime.debugNode() == "00/00-00-A03" && a03 >= 0 && a03 != a00,
          "Different scenes must preserve distinct translation route IDs");

  // The exporter must collect ORIGINAL dialogue and choice strings from the
  // authored ORS, not just whichever scene is visible. A canonical speaker
  // name is an ASS style ID, not a translatable user-facing dialogue string.
  const auto sources = runtime.translationSources();
  Require(!Contains(sources, a00, "Narrator") &&
              Contains(sources, a00, "Shared line.") &&
              Contains(sources, a00, "A00 alone."),
          "A00 translated a speaker ID or failed its KTRF dialogue export");
  Require(!Contains(sources, a03, "Narrator") &&
              Contains(sources, a03, "Shared line.") &&
              Contains(sources, a03, "Stay here") &&
              Contains(sources, a03, "Leave now"),
          "A03 PRINT_TEXT/SET_SELECT export is incomplete");
  Require(!Contains(sources, a00, "Stay here"),
          "A03 choices were assigned to the wrong KTRF route");

  const auto json = temp.root / "translation.json";
  Write(json,
        "{\n  \"nodo\": {\"Narrator\": \"Narrador\", "
        "\"Shared line.\": \"Linha comum.\", "
        "\"Leave now\": \"Sair agora\"},\n  \"" +
            std::to_string(a00) +
            "\": {\"Shared line.\": \"Linha A00.\", "
            "\"A00 alone.\": \"Somente A00.\"},\n  \"" +
            std::to_string(a03) +
            "\": {\"Stay here\": \"Ficar aqui\"}\n}\n");
  Kotonoha::Translation standalone;
  standalone.load(json.string());
  Require(standalone.text(a00, "Shared line.") == "Linha A00." &&
              standalone.text(a03, "Shared line.") == "Linha comum." &&
              standalone.text(a03, "Stay here") == "Ficar aqui" &&
              standalone.text(a03, "Leave now") == "Sair agora" &&
              standalone.text(a00, "Narrator") == "Narrador" &&
              standalone.text(a03, "not in JSON") == "not in JSON",
          "Route-specific override/global nodo fallback/original fallback drifted");
  Require(standalone.text(a00, "Stay here") == "Stay here",
          "A03-specific translation leaked into A00");
  VerifyChoiceGeometry(standalone.text(a03, "Stay here"));
  VerifyChoiceGeometry(standalone.text(a03, "Leave now"));

  runtime.debugJumpToNode("00/00-00-A00");
  Kotonoha::EngineOptions options;
  options.assets = assets;
  options.persistSettings = false;
  options.rendererOverride = "software";
  options.translationPath = json.string();
  options.settings.fontPath = HostTestFont();
  {
    Kotonoha::Engine engine(runtime, options);
    const auto &translation = engine.GetTranslation();
    Require(runtime.translationRoute() == a00 &&
                translation.text(runtime.translationRoute(), "Shared line.") ==
                    "Linha A00.",
            "Engine did not load node-specific School Days JSON");

    // Exercise Engine's real export entry point and verify its sections can
    // be loaded again, with originals as keys and translations as values.
    const auto exportedPath = temp.root / "exported.json";
    engine.dumpTranslation(exportedPath.string());
    Kotonoha::Translation exported;
    exported.load(exportedPath.string());
    Require(exported.text(a00, "Shared line.") == "Linha A00." &&
                exported.text(a03, "Shared line.") == "Linha comum." &&
                exported.text(a03, "Stay here") == "Ficar aqui" &&
                exported.text(a03, "Leave now") == "Sair agora",
            "Engine JSON export lost route or choice text");
    Require(Read(exportedPath).find("A00 alone.") != std::string::npos,
            "Engine JSON export lost a source-only text key");

    // Use the public live-edit API: unlike mutating the dictionary directly,
    // this also notifies active presentation scenes to invalidate text caches.
    runtime.debugJumpToNode("00/00-00-A03");
    Require(runtime.translationRoute() == a03,
            "Live translation edits lost the active KTRF scene route");
    engine.setTranslation(std::to_string(a03), "Stay here", "Remain for now");
    Require(engine.GetTranslation().text(a03, "Stay here") ==
                "Remain for now", "Live choice dictionary update was not visible");
    VerifyChoiceGeometry(engine.GetTranslation().text(a03, "Stay here"));
    engine.setTranslation("nodo", "Shared line.", "Live global line");
    Require(engine.GetTranslation().text(a03, "Shared line.") ==
                "Live global line" &&
                engine.GetTranslation().text(a00, "Shared line.") ==
                    "Linha A00." &&
                engine.GetTranslation().text(a03, "Untranslated sentence") ==
                    "Untranslated sentence",
            "Live global update broke route priority or untranslated fallback");
  }
  runtime.shutdown();
  CheckUnmodifiedScript(choice);
  Require(Read(first) == kA00, "JSON translation modified the A00 source file");
}

} // namespace

int main(int argc, char **argv) {
  try {
    Require(argc == 2, "Usage: SchoolDaysJsonTranslationTest ktrf-route");
    VerifyJsonIntegration(argv[1]);
    std::cout << "SchoolDays JSON integration PASS: route precedence, "
                 "PRINT_TEXT/SET_SELECT export, fallback, choice geometry, "
                 "original ORS immutability\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysJsonTranslationTest: " << error.what() << '\n';
    return 1;
  }
}
