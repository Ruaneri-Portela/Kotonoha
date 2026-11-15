#include <Kotonoha/routing/SchoolDaysGpkLanguageProfile.hpp>
#include <Kotonoha/routing/SchoolDaysTranslationCoverage.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace Profile = Kotonoha::SchoolDaysGpkLanguageProfile;
using Gate = Kotonoha::SchoolDaysTranslationGate;

namespace {

void Require(bool test, const char *message) {
  if (!test) throw std::runtime_error(message);
}

struct Scratch {
  fs::path folder =
      fs::temp_directory_path() /
      ("school-days-translation-layout-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch().count()));
  fs::path settings = folder / "settings.json";
  fs::path translations = folder / "translations";
  fs::path scripts = translations / "scripts";
  fs::path jsons = translations / "jsons";

  Scratch() { fs::create_directories(translations); }
  ~Scratch() {
    std::error_code ec;
    fs::remove_all(folder, ec);
  }
  void Write(const fs::path &path, const std::string &data = "test") const {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << data;
    if (!out) throw std::runtime_error("Could not write translation test fixture");
  }
};

template <class Function>
void RequireReject(Function &&operation, const char *message) {
  try {
    operation();
  } catch (const std::exception &) {
    return;
  }
  throw std::runtime_error(message);
}

void TestMissingFilesFailClosed() {
  Scratch temp;
  const auto settings = temp.settings.string();
  Require(Profile::ScriptPath(settings, "en").empty(),
          "Native EN tried to select translated Script.GPK");
  Require(Profile::ScriptPath(settings, "pt-BR") ==
              temp.scripts / "Script - Brazilian.GPK",
          "Missing PT-BR must retain canonical missing path");
  Require(Profile::ScriptPath(settings, "es") ==
              temp.scripts / "Script - Spanish.GPK",
          "Missing ES cannot silently use another language");
  Require(Profile::ScriptPath(settings, "zh-CN") ==
              temp.scripts / "Script - chinese-compatible.GPK",
          "Unconverted Chinese pack must never be silently selected");
  Require(Profile::LxhPath(settings, "en") == temp.scripts / "en-lxh.GPK" &&
              Profile::LxhPath(settings, "es") ==
                  temp.scripts / "es-lxh.GPK" &&
              Profile::LxhPath(settings, "pt-BR") ==
                  temp.scripts / "pt-BR-lxh.GPK" &&
              Profile::LxhPath(settings, "zh-CN") ==
                  temp.scripts / "zh-CN-lxh.GPK",
          "Missing LxH GPK must retain exact locale and name");
  Require(Profile::JsonPath(settings, "zh-CN.json") ==
              temp.jsons / "zh-CN.json",
          "Missing JSON must return canonical missing path");
  Require(Profile::DiscoverJsonFiles(settings).empty() &&
              Profile::DiscoverScriptFiles(settings).empty(),
          "Empty translation directory returned phantom assets");
  RequireReject([&] { (void)Profile::ScriptPath(settings, "fr"); },
                "Unexpected native GPK language accepted");
  RequireReject([&] { (void)Profile::LxhPath(settings, "es-ES"); },
                "Unexpected LxH language accepted");
}

void TestOrganizedAndLegacyFallback() {
  Scratch temp;
  const auto settings = temp.settings.string();
  temp.Write(temp.translations / "Script - Brazilian.GPK");
  temp.Write(temp.translations / "es-lxh.GPK");
  temp.Write(temp.translations / "zh-CN.json", "{}");
  temp.Write(temp.translations / "es.json", "{}");
  temp.Write(temp.translations / "es-lxh.json", "{}");
  Require(Profile::ScriptPath(settings, "pt-BR") ==
              temp.translations / "Script - Brazilian.GPK",
          "Legacy flat Script.GPK was not found");
  Require(Profile::LxhPath(settings, "es") ==
              temp.translations / "es-lxh.GPK",
          "Legacy flat LxH.GPK was not found");
  Require(Profile::JsonPath(settings, "zh-CN.json") ==
              temp.translations / "zh-CN.json",
          "Legacy flat JSON was not found");

  // Prefer new layout independently for each file, while retaining an old
  // flat fallback for a different file during a partial directory migration.
  temp.Write(temp.scripts / "Script - Brazilian.GPK");
  temp.Write(temp.scripts / "pt-BR-lxh.GPK");
  temp.Write(temp.jsons / "es.json", "{}");
  temp.Write(temp.jsons / "es-lxh.json", "{}");
  temp.Write(temp.jsons / "zh-CN.json", "{}");
  temp.Write(temp.translations / "Script - chinese.GPK");
  Require(Profile::ScriptPath(settings, "pt-BR") ==
              temp.scripts / "Script - Brazilian.GPK",
          "Canonical scripts/ must outrank the legacy flat GPK");
  Require(Profile::LxhPath(settings, "pt-BR") ==
              temp.scripts / "pt-BR-lxh.GPK",
          "Canonical LxH GPK must outrank flat");
  Require(Profile::LxhPath(settings, "es") ==
              temp.translations / "es-lxh.GPK",
          "Missing canonical file must still use exact flat legacy name");
  Require(Profile::JsonPath(settings, "es.json") == temp.jsons / "es.json",
          "Canonical jsons/ must outrank flat");
  Require(Profile::JsonPath(settings, "zh-CN.json") ==
              temp.jsons / "zh-CN.json",
          "Chinese JSON capitalization was not preserved");
  Require(Profile::ScriptPath(settings, "zh-CN") ==
              temp.scripts / "Script - chinese-compatible.GPK",
          "Incompatible raw Chinese script substituted for compatible variant");

  temp.Write(temp.jsons / "en-lxh.json", "{}");
  temp.Write(temp.jsons / "es-reviewed-identities.json", "{}");
  temp.Write(temp.jsons / "es-lxh-reviewed-identities.json", "{}");
  temp.Write(temp.jsons / "fr.json", "{}");
  const auto base = Profile::DiscoverJsonFiles(settings);
  Require(base.size() == 3 && base[0].filename() == "es.json" &&
              base[1].filename() == "fr.json" &&
              base[2].filename() == "zh-CN.json",
          "JSON discovery must exclude LxH/reviews and avoid legacy duplicates");
  const auto full = Profile::DiscoverJsonFiles(settings, true);
  Require(full.size() == 7, "Auxiliary discovery must include JSON sidecars");
  const auto gpks = Profile::DiscoverScriptFiles(settings);
  Require(gpks.size() == 4, "Script discovery did not merge old and new layouts");
  Require(gpks[0].filename() == "Script - Brazilian.GPK" &&
              gpks[1].filename() == "Script - chinese.GPK" &&
              gpks[2].filename() == "es-lxh.GPK" &&
              gpks[3].filename() == "pt-BR-lxh.GPK",
          "Script discovery did not keep actual names and stable order");
}

void TestMatchingLxhJsonAndReviewManifests() {
  Scratch temp;
  const auto settings = temp.settings.string();
  temp.Write(temp.translations / "es.json", "{}");
  temp.Write(temp.translations / "es-lxh.json", "{}");
  temp.Write(temp.jsons / "es-lxh.json", "{}");
  temp.Write(temp.translations / "es-reviewed-identities.json",
             "{\"7\":{\"Name\":\"punctuation\"}}");
  temp.Write(temp.jsons / "es-reviewed-identities.json",
             "{\"7\":{\"Name\":\"proper_name\"}}");
  const auto lxh = Gate::DlcJsonPath(
      "es", (temp.translations / "es.json").string());
  Require(fs::path(lxh) == temp.jsons / "es-lxh.json",
          "Flat Spanish base must prefer matching canonical jsons/ sidecar");
  const auto review = Gate::LoadReviewedIdentities(
      (temp.translations / "es.json").string());
  Require(review.getSections().at("7").at("Name") == "proper_name",
          "Spanish identity manifest did not prefer canonical jsons/");
  temp.Write(temp.jsons / "zh-CN.json", "{}");
  temp.Write(temp.jsons / "zh-CN-lxh.json", "{}");
  Require(fs::path(Gate::DlcJsonPath(
              "zh-cn", (temp.jsons / "zh-CN.json").string())) ==
              temp.jsons / "zh-CN-lxh.json",
          "Chinese DLC JSON must match selected base filename's casing");
  temp.Write(temp.jsons / "fr.json", "{}");
  Require(Gate::DlcJsonPath(
              "fr", (temp.jsons / "fr.json").string()).empty(),
          "A missing LxH sidecar must not substitute another language");

  // Custom externally chosen JSONs may use only an adjacent matching LxH;
  // they never inherit a possibly different-revision installed dictionary.
  const auto external = temp.folder / "custom";
  temp.Write(external / "es.json", "{}");
  Require(Gate::DlcJsonPath("es", (external / "es.json").string()).empty(),
          "External custom JSON inherited unrelated installed LxH sidecar");
  temp.Write(external / "es-lxh.json", "{}");
  Require(fs::path(Gate::DlcJsonPath(
              "es", (external / "es.json").string())) ==
              external / "es-lxh.json",
          "Explicit custom JSON's adjacent LxH sidecar was not selected");
}

void TestUnsafePathsFailClosed() {
  Scratch temp;
  const auto settings = temp.settings.string();
  for (const auto &unsafe :
       {"../es.json", R"(..\es.json)", "x/es.json",
        R"(C:\Temp\es.json)", "", ".", "es-lxh.GPK"}) {
    RequireReject([&] { (void)Profile::JsonPath(settings, unsafe); },
                  "Unsafe or mismatched JSON filename accepted");
  }
  temp.Write(temp.translations / "pt-BR-lxh.GPK");
  temp.Write(temp.scripts / "pt-br-lxh.GPK"); // wrong case
  RequireReject([&] { (void)Profile::LxhPath(settings, "pt-BR"); },
                "Wrong-case canonical PT-BR should not use legacy flat pack");
  fs::remove(temp.scripts / "pt-br-lxh.GPK");
  fs::create_directories(temp.scripts / "pt-BR-lxh.GPK");
  RequireReject([&] { (void)Profile::LxhPath(settings, "pt-BR"); },
                "Directory shadowing canonical GPK was accepted as a file");
  fs::remove_all(temp.scripts / "pt-BR-lxh.GPK");
  temp.Write(temp.translations / "es.json", "{}");
  temp.Write(temp.jsons / "ES.json", "{}");
  RequireReject([&] { (void)Profile::JsonPath(settings, "es.json"); },
                "Case-mismatched organized JSON silently selected flat fallback");
}

} // namespace

int main() {
  try {
    TestMissingFilesFailClosed();
    TestOrganizedAndLegacyFallback();
    TestMatchingLxhJsonAndReviewManifests();
    TestUnsafePathsFailClosed();
    std::cout << "SchoolDaysTranslationLayoutTest PASS\n";
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysTranslationLayoutTest FAIL: " << error.what()
              << '\n';
    return 1;
  }
  return 0;
}
