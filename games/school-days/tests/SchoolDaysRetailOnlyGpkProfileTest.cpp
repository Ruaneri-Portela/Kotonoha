#include <Kotonoha/SchoolDaysAssetMount.hpp>
#include <Kotonoha/routing/SchoolDaysDisplayNarrativeMarkup.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;

void require(bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}

void assertProfile(const std::string &locale, const fs::path &script) {
  std::string error;
  require(Kotonoha::ConfigureSchoolDaysTranslatedGpks({locale, script, {}}, &error),
          locale + " unexpectedly requires LxH.GPK: " + error);
  require(Kotonoha::ClearSchoolDaysTranslatedGpks(&error),
          locale + " profile could not be cleared: " + error);
}
} // namespace

int main() {
#if !defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
  std::cerr << "This test belongs only to retail-only builds.\n";
  return 1;
#else
  try {
    const fs::path root = fs::current_path() / "retail-only-gpk-profile-fixture";
    fs::create_directories(root);
    const auto brazilian = root / "Script - Brazilian.GPK";
    const auto spanish = root / "Script - Spanish.GPK";
    const auto chinese = root / "Script - chinese-compatible.GPK";
    const auto russian = root / "Script - Russian.GPK";
    {
      std::ofstream file(brazilian, std::ios::binary | std::ios::trunc);
      file << 'x';
    }
    {
      std::ofstream file(spanish, std::ios::binary | std::ios::trunc);
      file << 'x';
    }
    {
      std::ofstream file(chinese, std::ios::binary | std::ios::trunc);
      file << 'x';
    }
    {
      std::ofstream file(russian, std::ios::binary | std::ios::trunc);
      file << 'x';
    }
    // The selection gate checks identity and file existence; actual GPK
    // structure is verified later during mount against Script.GPK retail.
    assertProfile("en", {});
    assertProfile("pt-BR", brazilian);
    assertProfile("es", spanish);
    assertProfile("zh-CN", chinese);
    assertProfile("ru", russian);

    using Markup = Kotonoha::SchoolDaysDisplayNarrativeMarkup;
    require(Markup::Project(u8"Выбор?^") == u8"Выбор?",
            "Russian choice display marker was not removed");
    require(Markup::Project(u8"你好^") == u8"你好",
            "Chinese choice display marker was not removed");

    std::string error;
    require(!Kotonoha::ConfigureSchoolDaysTranslatedGpks(
                {"pt-BR", brazilian, root / "pt-BR-lxh.GPK"}, &error),
            "Retail-only mode incorrectly accepts an LxH translation");
    require(!Kotonoha::ConfigureSchoolDaysTranslatedGpks(
                {"es", brazilian, {}}, &error),
            "Spanish mode incorrectly accepts the Brazilian GPK name");
    require(!Kotonoha::ConfigureSchoolDaysTranslatedGpks(
                {"zh-CN", russian, {}}, &error),
            "Chinese mode incorrectly accepts a Russian archive");
    require(!Kotonoha::ConfigureSchoolDaysTranslatedGpks(
                {"ru", chinese, {}}, &error),
            "Russian mode incorrectly accepts a Chinese archive");
    fs::remove(brazilian);
    fs::remove(spanish);
    fs::remove(chinese);
    fs::remove(russian);
    fs::remove(root);
    std::cout << "Retail-only EN/PT-BR/ES/ZH-CN/RU selection requires no LxH.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Retail-only GPK profile test: " << error.what() << '\n';
    return 1;
  }
#endif
}
