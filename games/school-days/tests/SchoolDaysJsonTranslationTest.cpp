#include "SchoolDaysAssets.hpp"
#include "SchoolDaysRuntime.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/localization/Translation.hpp>
#include <Kotonoha/parsers/Ors.h>
#include <Kotonoha/routing/SchoolDaysChoiceTextPlan.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
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
constexpr const char *kIntermediate =
    "[PrintText]=00:00:00\tNarrator\tSynthetic intermediate.\t00:01:00;\n"
    "[SkipFRAME]=00:01:00;\n"
    "[Next]=00:02:00;\n";

void ReachChoiceByPlayer(SchoolDays::Runtime &runtime,
                         std::array<int, 2> *intermediateRoutes = nullptr) {
  Require(runtime.debugNode() == "00/00-00-A00",
          "JSON player-path fixture must start at A00");
  std::size_t sceneIndex = 0;
  for (const char *next : {"00/00-00-A01", "00/00-00-A02",
                           "00/00-00-A03"}) {
    runtime.openPanel(4); // Player next-choice, default Skip OFF.
    Require(runtime.debugNode() == next,
            std::string("JSON player path did not enter ") + next);
    Require(runtime.debugNodeSeconds() && *runtime.debugNodeSeconds() == 0.0,
            std::string("JSON player path skipped the start of ") + next);
    if (sceneIndex < 2 && intermediateRoutes) {
      (*intermediateRoutes)[sceneIndex] = runtime.translationRoute();
      Require((*intermediateRoutes)[sceneIndex] >= 0,
              std::string("Intermediate scene has no translation ID: ") + next);
    }
    ++sceneIndex;
  }
}

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
  Write(scripts / "00-00-A01.ENG.ORS", kIntermediate);
  Write(scripts / "00-00-A02.ENG.ORS", kIntermediate);
  Write(choice, kA03);
  const auto documents = temp.root / "documents";
  fs::create_directories(documents);

  auto assets = std::make_shared<SchoolDays::Assets>(temp.root);
  int a00 = -1;
  int a03 = -1;
  std::array<int, 2> intermediateRoutes{{-1, -1}};
  Kotonoha::Translation::SourceTexts sources;
  // Probe the debug API separately. Its scene jumps can affect the session's
  // read markers, so it cannot own the player-path coverage session.
  {
    const auto probeDocuments = temp.root / "debug-probe-documents";
    fs::create_directories(probeDocuments);
    SchoolDays::Runtime probe(assets, route, probeDocuments);
    Require(probe.debugNode() == "00/00-00-A00", "Fixture failed to open A00");
    a00 = probe.translationRoute();
    Require(a00 >= 0, "A00 lacks a stable KTRF node translation ID");
#if defined(KOTONOHA_DISTRIBUTION_BUILD)
    probe.debugJumpToNode("00/00-00-A03");
    Require(probe.debugNode() == "00/00-00-A00" &&
                probe.translationRoute() == a00,
            "DIST exposed the development scene jump through the translation route");
#else
    probe.debugJumpToNode("00/00-00-A03");
    Require(probe.debugNode() == "00/00-00-A03" &&
                probe.translationRoute() != a00,
            "DEV debug scene jump lost the A03 translation route");
    probe.debugJumpToNode("00/00-00-A00");
    Require(probe.debugNode() == "00/00-00-A00" &&
                probe.translationRoute() == a00,
            "DEV debug return lost the A00 translation route");
#endif
    probe.shutdown();
  }
  {
    const auto probeDocuments = temp.root / "player-probe-documents";
    fs::create_directories(probeDocuments);
    SchoolDays::Runtime player(assets, route, probeDocuments);
    Require(player.debugNode() == "00/00-00-A00" &&
                player.translationRoute() == a00,
            "Fresh player-path probe lost the A00 translation route");
    ReachChoiceByPlayer(player, &intermediateRoutes);
    a03 = player.translationRoute();
    Require(player.debugNode() == "00/00-00-A03" && a03 >= 0 && a03 != a00,
            "Player scene navigation lost distinct KTRF translation IDs");
    Require(intermediateRoutes[0] != a00 && intermediateRoutes[1] != a00 &&
                intermediateRoutes[0] != intermediateRoutes[1] &&
                intermediateRoutes[0] != a03 && intermediateRoutes[1] != a03,
            "Intermediate scenes must have distinct translation route IDs");
    sources = player.translationSources();
    player.shutdown();
  }

  // The exporter must collect ORIGINAL dialogue and choice strings from the
  // authored ORS, not just whichever scene is visible. A canonical speaker
  // name is an ASS style ID, not a translatable user-facing dialogue string.
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
  Require(Contains(sources, intermediateRoutes[0], "Synthetic intermediate.") &&
              Contains(sources, intermediateRoutes[1], "Synthetic intermediate."),
          "Intermediate ORS dialogue was not collected for translation");

  const auto json = temp.root / "translation.json";
  Write(json,
        "{\n  \"nodo\": {\"Narrator\": \"Narrador\", "
        "\"Shared line.\": \"Linha comum.\", "
        "\"Leave now\": \"Sair agora\"},\n  \"" +
            std::to_string(a00) +
            "\": {\"Shared line.\": \"Linha A00.\", "
            "\"A00 alone.\": \"Somente A00.\"},\n  \"" +
            std::to_string(intermediateRoutes[0]) +
            "\": {\"Synthetic intermediate.\": \"Cena intermediaria um.\"},\n  \"" +
            std::to_string(intermediateRoutes[1]) +
            "\": {\"Synthetic intermediate.\": \"Cena intermediaria dois.\"},\n  \"" +
            std::to_string(a03) +
            "\": {\"Stay here\": \"Ficar aqui\"}\n}\n");
  Kotonoha::Translation standalone;
  standalone.load(json.string());
  Require(standalone.text(a00, "Shared line.") == "Linha A00." &&
              standalone.text(a03, "Shared line.") == "Linha comum." &&
              standalone.text(a03, "Stay here") == "Ficar aqui" &&
              standalone.text(a03, "Leave now") == "Sair agora" &&
              standalone.text(intermediateRoutes[0], "Synthetic intermediate.") ==
                  "Cena intermediaria um." &&
              standalone.text(intermediateRoutes[1], "Synthetic intermediate.") ==
                  "Cena intermediaria dois." &&
              standalone.text(a00, "Narrator") == "Narrador" &&
              standalone.text(a03, "not in JSON") == "not in JSON",
          "Route-specific override/global nodo fallback/original fallback drifted");
  Require(standalone.text(a00, "Stay here") == "Stay here",
          "A03-specific translation leaked into A00");
  VerifyChoiceGeometry(standalone.text(a03, "Stay here"));
  VerifyChoiceGeometry(standalone.text(a03, "Leave now"));

  SchoolDays::Runtime runtime(assets, route, documents);
  Require(runtime.debugNode() == "00/00-00-A00" &&
              runtime.translationRoute() == a00,
          "Fresh JSON gameplay did not restore original A00 route");
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
                exported.text(a03, "Leave now") == "Sair agora" &&
                exported.text(intermediateRoutes[0], "Synthetic intermediate.") ==
                    "Cena intermediaria um." &&
                exported.text(intermediateRoutes[1], "Synthetic intermediate.") ==
                    "Cena intermediaria dois.",
            "Engine JSON export lost route or choice text");
    Require(Read(exportedPath).find("A00 alone.") != std::string::npos,
            "Engine JSON export lost a source-only text key");

    // Use the public live-edit API: unlike mutating the dictionary directly,
    // this also notifies active presentation scenes to invalidate text caches.
    runtime.SetState(Kotonoha::GameState::Gameplay);
    ReachChoiceByPlayer(runtime);
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
  Require(Read(scripts / "00-00-A01.ENG.ORS") == kIntermediate &&
              Read(scripts / "00-00-A02.ENG.ORS") == kIntermediate,
          "JSON translation modified an intermediate player-path ORS");
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
