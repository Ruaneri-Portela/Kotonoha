#include <Kotonoha/core/LauncherLocale.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using Kotonoha::LauncherLocale::Language;
namespace Locale = Kotonoha::LauncherLocale;

void require(bool valid, const char *reason) {
  if (!valid)
    throw std::runtime_error(reason);
}

void testLanguageNames() {
  require(Locale::parse("en") == Language::English, "English code");
  require(Locale::parse("pt_BR") == Language::PortugueseBrazil,
          "Brazilian tag");
  require(Locale::parse("es-ES") == Language::Spanish, "Spanish regional tag");
  require(Locale::parse("RU_ru") == Language::Russian, "Russian regional tag");
  require(Locale::parse("zh-Hans-CN") == Language::Chinese,
          "Simplified Chinese");
  require(Locale::parse("zh-TW") == Language::English,
          "Traditional Chinese must not select Simplified Chinese");
  require(Locale::parse("zh-Hant") == Language::English,
          "Traditional script must not select Simplified Chinese");
  require(Locale::parse("unknown") == Language::English, "Unknown fallback");
  require(Locale::systemLanguage("pt", "BR") == Language::PortugueseBrazil,
          "System locale country");
  require(Locale::systemLanguage("zh", "TW") == Language::English,
          "System locale must not confuse Chinese scripts");
  require(Locale::systemLanguage("ru", "RU") == Language::Russian,
          "Russian system locale");
  for (const auto language :
       {Language::English, Language::PortugueseBrazil, Language::Spanish,
        Language::Russian, Language::Chinese}) {
    require(Locale::parse(Locale::tag(language)) == language,
            "Persisted tag must round-trip");
    require(std::string(Locale::nativeName(language)).size() > 2,
            "Each language must have a native name");
  }
}

void testLabels() {
  constexpr const char *labels[]{"Interface language",
                                 "Play",
                                 "Quit",
                                 "Game",
                                 "Video",
                                 "Translation",
                                 "Advanced",
                                 "Restore defaults",
                                 "Master volume",
                                 "Mute all audio",
                                 "Overlay folder",
                                 "Route file (.ktnroute)",
                                 "Browse...",
                                 "UI font",
                                 "Renderer",
                                 "Log level"};
  for (const auto language : {Language::PortugueseBrazil, Language::Spanish,
                              Language::Russian, Language::Chinese}) {
    for (const char *label : labels) {
      const char *translated = Locale::text(language, label);
      require(translated && *translated && std::string(label) != translated,
              "Missing translated launcher label");
    }
    require(std::string(Locale::text(language, "Unrecognized engine label")) ==
                "Unrecognized engine label",
            "Unknown labels must keep an English fallback");
  }
  require(std::string(Locale::text(Language::English, "Game")) == "Game",
          "English must preserve historical UI strings");
}

void testPersistence() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("kotonoha-launcher-locale-" + std::to_string(nonce));
  require(std::filesystem::create_directory(root),
          "Cannot create a unique synthetic preference directory");
  struct Remove {
    std::filesystem::path path;
    ~Remove() {
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
    }
  } cleanup{root};
  const auto settingsPath = (root / "settings.json").string();
  require(Locale::load(settingsPath, Language::Russian) == Language::Russian,
          "Fresh installation can use a supported system language");
  std::ofstream(settingsPath, std::ios::binary) << "{\"width\":1280}\n";
  require(Locale::load(settingsPath, Language::Russian) == Language::English,
          "Existing game settings retain original English UI by default");
  for (auto language : {Language::PortugueseBrazil, Language::Spanish,
                        Language::Russian, Language::Chinese}) {
    Locale::save(settingsPath, language);
    require(Locale::load(settingsPath, Language::English) == language,
            "Language preference must be persisted independently");
  }
  std::ifstream original(settingsPath, std::ios::binary);
  std::string originalLine;
  std::getline(original, originalLine);
  require(originalLine == "{\"width\":1280}",
          "Changing launcher language must not edit engine settings");
  std::ofstream(root / "launcher-language.txt") << "bogus\n";
  require(Locale::load(settingsPath, Language::Russian) == Language::English,
          "Malformed language preference must not leak into other settings");
  require(Locale::load("", Language::Russian) == Language::English,
          "Empty settings path has no persistent preference");
}
} // namespace

int main() {
  try {
    testLanguageNames();
    testLabels();
    testPersistence();
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << "\n";
    return 1;
  }
  std::cout << "Launcher locale tests passed\n";
  return 0;
}
