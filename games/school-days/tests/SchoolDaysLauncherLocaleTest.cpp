#include <Kotonoha/routing/SchoolDaysLauncherLocale.hpp>

// These assertions must run in Release/DIST too. Several exercise persistence
// side effects, so NDEBUG would otherwise skip the operations entirely.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
namespace Loc = Kotonoha::SchoolDaysLauncherLocale;

std::string formatSignature(std::string_view value) {
  std::string result;
  for (std::size_t i = 0; i + 1 < value.size(); ++i) {
    if (value[i] != '%')
      continue;
    if (value[i + 1] == '%') {
      ++i;
      continue;
    }
    // The launcher uses printf-style ImGui::Text* formats for dynamic
    // status/path labels. Missing or mismatched specifiers are unsafe.
    if (std::string_view("diuoxXfFeEgGaAcsp").find(value[i + 1]) !=
        std::string_view::npos) {
      result.push_back(value[i + 1]);
      ++i;
    }
  }
  return result;
}

int main() {
  // 1. 100% coverage across all 5 locales and every key.
  std::string missing;
  if (!Loc::ValidateCoverage(&missing)) {
    std::cerr << "Missing launcher translation: " << missing << "\n";
    return 1;
  }

  // 1b. Every dynamic UI text must preserve the English format parameters.
  for (std::size_t k = 0; k < Loc::kKeyCount; ++k) {
    const auto key = static_cast<Loc::Key>(k);
    const std::string expected = formatSignature(Loc::Text(Loc::Language::En, key));
    for (const auto lang : Loc::AllLanguages()) {
      const std::string actual = formatSignature(Loc::Text(lang, key));
      if (expected != actual) {
        std::cerr << "Mismatched format specifiers: " << Loc::Code(lang)
                  << " key=" << k << " expected=" << expected
                  << " actual=" << actual << "\n";
        return 1;
      }
    }
  }

  // 2. Exact 5 codes and native names.
  assert(std::string(Loc::Code(Loc::Language::En)) == "en");
  assert(std::string(Loc::Code(Loc::Language::PtBr)) == "pt-BR");
  assert(std::string(Loc::Code(Loc::Language::Es)) == "es");
  assert(std::string(Loc::Code(Loc::Language::Ru)) == "ru");
  assert(std::string(Loc::Code(Loc::Language::ZhCn)) == "zh-CN");

  assert(std::string(Loc::NativeName(Loc::Language::En)) == "English");
  assert(std::string(Loc::NativeName(Loc::Language::PtBr)) == "Português (Brasil)");
  assert(std::string(Loc::NativeName(Loc::Language::Es)) == "Español");
  assert(std::string(Loc::NativeName(Loc::Language::Ru)) == "Русский");
  assert(std::string(Loc::NativeName(Loc::Language::ZhCn)) == "简体中文");

  // 3. Parsing & default resolution (existing install vs fresh install).
  assert(Loc::ParseCode("pt-BR") == Loc::Language::PtBr);
  assert(Loc::ParseCode("pt_BR") == Loc::Language::PtBr);
  assert(Loc::ParseCode("es-MX") == Loc::Language::Es);
  assert(Loc::ParseCode("ru-RU") == Loc::Language::Ru);
  assert(Loc::ParseCode("zh-Hans-CN") == Loc::Language::ZhCn);
  assert(!Loc::ParseCode("zh-TW").has_value());
  assert(!Loc::ParseCode("ja-JP").has_value());

  // Existing install without launcher-language.txt preserves previous EN UI.
  assert(Loc::ResolveDefault(/*existingInstallation=*/true, "pt-BR") ==
         Loc::Language::En);
  // Fresh install adopts supported system locale, or falls back to EN.
  assert(Loc::ResolveDefault(/*existingInstallation=*/false, "pt-BR") ==
         Loc::Language::PtBr);
  assert(Loc::ResolveDefault(/*existingInstallation=*/false, "ru-RU") ==
         Loc::Language::Ru);
  assert(Loc::ResolveDefault(/*existingInstallation=*/false, "zh-CN") ==
         Loc::Language::ZhCn);
  assert(Loc::ResolveDefault(/*existingInstallation=*/false, "fr-FR") ==
         Loc::Language::En);

  // 4. Atomic persistence independent of game language files.
  const fs::path tmpDir =
      fs::temp_directory_path() / "kotonoha-launcher-locale-test";
  std::error_code ec;
  fs::remove_all(tmpDir, ec);
  fs::create_directories(tmpDir);
  const std::string settingsPath = (tmpDir / "settings.json").string();

  // Seed unrelated game-language & LxH sidecars and confirm they stay untouched.
  {
    std::ofstream(tmpDir / "language.txt") << "brazilian-gpk\n";
    std::ofstream(tmpDir / "translation-pack.txt")
        << "SchoolDaysTranslationPack.v1\npt-BR\n";
    std::ofstream(tmpDir / "Kotonoha.ini") << "EnableLxh=1\n";
  }

  assert(!Loc::ReadPreference(settingsPath).has_value());
  assert(Loc::PreferencePath(settingsPath).filename() ==
         Loc::kPreferenceFilename);
  for (Loc::Language lang : Loc::AllLanguages()) {
    assert(Loc::WritePreference(settingsPath, lang));
    const auto loaded = Loc::ReadPreference(settingsPath);
    assert(loaded.has_value() && *loaded == lang);
  }

  // Verify game language & LxH files were never altered.
  {
    std::ifstream langIn(tmpDir / "language.txt");
    std::string line;
    std::getline(langIn, line);
    assert(line == "brazilian-gpk");

    std::ifstream packIn(tmpDir / "translation-pack.txt");
    std::getline(packIn, line);
    assert(line == "SchoolDaysTranslationPack.v1");
    std::getline(packIn, line);
    assert(line == "pt-BR");

    std::ifstream iniIn(tmpDir / "Kotonoha.ini");
    std::getline(iniIn, line);
    assert(line == "EnableLxh=1");
  }

  fs::remove_all(tmpDir, ec);
  std::cout << "PASS SchoolDaysLauncherLocaleTest (" << Loc::kLanguageCount
            << " locales x " << Loc::kKeyCount << " keys)\n";
  return 0;
}
