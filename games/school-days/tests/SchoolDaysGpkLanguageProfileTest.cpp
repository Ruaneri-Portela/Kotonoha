#include <Kotonoha/routing/SchoolDaysGpkLanguageProfile.hpp>

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace Profile = Kotonoha::SchoolDaysGpkLanguageProfile;

namespace {
struct Scratch {
  fs::path folder = fs::temp_directory_path() /
      ("school-days-gpk-language-profile-" +
       std::to_string(SDL_GetPerformanceCounter()));
  Scratch() { fs::create_directories(folder); }
  ~Scratch() {
    std::error_code ec;
    fs::remove_all(folder, ec);
  }
  void Write(const std::string &data) const {
    std::ofstream out(folder / "translation-pack.txt", std::ios::binary);
    out << data;
    if (!out) throw std::runtime_error("Cannot write test profile");
  }
};
void Check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
void ExpectReject(const std::string &file) {
  try {
    (void)Profile::Read(file);
  } catch (const std::runtime_error &) {
    return;
  }
  throw std::runtime_error("Invalid translation-pack.txt was accepted");
}
}

int main() {
  try {
    Scratch scratch;
    const auto settings = (scratch.folder / "settings.json").string();
    Check(Profile::Read(settings).empty(), "Missing sidecar must retain legacy profile");
    Check(Profile::ScriptPath(settings, "en").empty(),
          "Native English must use original retail Packs/Script.GPK only");
    Check(Profile::ScriptPath(settings, "pt-BR").filename() ==
              "Script - Brazilian.GPK", "Brazilian GPK name mismatch");
    Check(Profile::ScriptPath(settings, "es").filename() ==
              "Script - Spanish.GPK", "Spanish GPK path mismatch");
    Check(Profile::ScriptPath(settings, "zh-CN").filename() ==
              "Script - chinese-compatible.GPK", "Incompatible raw Chinese GPK selected");
    Check(Profile::Supported("ru") &&
          Profile::ScriptPath(settings, "ru").filename() == "Script - Russian.GPK",
          "Russian GPK path mismatch");
    Check(Profile::LxhPath(settings, "en").filename() == "en-lxh.GPK" &&
              Profile::LxhPath(settings, "pt-BR").filename() == "pt-BR-lxh.GPK" &&
              Profile::LxhPath(settings, "es").filename() == "es-lxh.GPK" &&
              Profile::LxhPath(settings, "ru").filename() == "ru-lxh.GPK" &&
              Profile::LxhPath(settings, "zh-CN").filename() == "zh-CN-lxh.GPK",
          "Per-locale LxH GPK sidecar name mismatch");
    scratch.Write("SchoolDaysTranslationPack.v1\r\npt-BR\r\n");
    Check(Profile::Read(settings) == "pt-BR", "CRLF profile did not parse");
    scratch.Write("SchoolDaysTranslationPack.v1\nen\n");
    Check(Profile::Read(settings) == "en", "Original English profile did not parse");
    scratch.Write("SchoolDaysTranslationPack.v1\nes\n");
    Check(Profile::Read(settings) == "es", "Spanish native profile did not parse");
    scratch.Write("SchoolDaysTranslationPack.v1\nzh-CN\n");
    Check(Profile::Read(settings) == "zh-CN", "Chinese native profile did not parse");
    scratch.Write("SchoolDaysTranslationPack.v1\nru\n");
    Check(Profile::Read(settings) == "ru", "Russian native profile did not parse");
    scratch.Write("SchoolDaysTranslationPack.v1\nunknown\n");
    ExpectReject(settings);
    scratch.Write("SchoolDaysTranslationPack.v1\nzh-CN\nforeign-path\n");
    ExpectReject(settings);
    scratch.Write("SchoolDaysTranslationPack.v1\n");
    ExpectReject(settings);
    std::cout << "SchoolDaysGpkLanguageProfileTest PASS\n";
  } catch (const std::exception &e) {
    std::cerr << "SchoolDaysGpkLanguageProfileTest FAIL: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
