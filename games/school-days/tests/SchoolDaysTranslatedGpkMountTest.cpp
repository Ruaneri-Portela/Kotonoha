#include <Kotonoha/SchoolDaysAssetMount.hpp>
#include <Kotonoha/SchoolDaysGpkArchive.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/routing/SchoolDaysLxhContent.hpp>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace fs = std::filesystem;
using namespace Kotonoha;

constexpr std::array<uint8_t, 16> kPidxXorKey = {{
    0x82, 0xEE, 0x1D, 0xB3, 0x57, 0xE9, 0x2C, 0xC2,
    0x2F, 0x54, 0x7B, 0x10, 0x4C, 0x9A, 0x75, 0x49,
}};

void Check(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

void U16(std::vector<uint8_t> *out, uint16_t n) {
  out->push_back(static_cast<uint8_t>(n));
  out->push_back(static_cast<uint8_t>(n >> 8u));
}
void U32(std::vector<uint8_t> *out, uint32_t n) {
  for (int i = 0; i < 4; ++i)
    out->push_back(static_cast<uint8_t>(n >> (8u * i)));
}

std::vector<uint8_t> Compress(const std::vector<uint8_t> &input) {
  uLongf cap = compressBound(static_cast<uLong>(input.size()));
  std::vector<uint8_t> output(static_cast<size_t>(cap));
  Check(compress2(output.data(), &cap, input.data(),
                  static_cast<uLong>(input.size()), Z_BEST_COMPRESSION) == Z_OK,
        "Synthetic PIDX compression failed");
  output.resize(static_cast<size_t>(cap));
  return output;
}

struct Entry {
  std::string name;
  std::string data;
};

std::vector<uint8_t> MakeGpk(const std::vector<Entry> &entries) {
  Check(!entries.empty(), "Synthetic GPK cannot be empty");
  constexpr uint32_t kOffset = 5120u;
  std::vector<uint8_t> bytes(kOffset, 0u);
  std::vector<uint8_t> index;
  uint32_t offset = kOffset;
  for (const auto &entry : entries) {
    Check(!entry.data.empty() && !entry.name.empty(), "Invalid synthetic GPK entry");
    Check(entry.name.size() < 65535u, "Synthetic entry filename too long");
    U16(&index, static_cast<uint16_t>(entry.name.size()));
    for (unsigned char ch : entry.name) {
      Check(ch <= 0x7fu, "Synthetic fixture expects ASCII names");
      U16(&index, ch);
    }
    U16(&index, 0u);
    U16(&index, 0u);
    U16(&index, 0u);
    U32(&index, offset);
    U32(&index, static_cast<uint32_t>(entry.data.size()));
    index.insert(index.end(), {' ', ' ', ' ', ' '});
    U32(&index, static_cast<uint32_t>(entry.data.size()));
    const auto prefix = std::min<size_t>(entry.data.size(), 5u);
    index.push_back(static_cast<uint8_t>(prefix));
    index.insert(index.end(), entry.data.begin(),
                 entry.data.begin() + static_cast<std::ptrdiff_t>(prefix));
    bytes.insert(bytes.end(), entry.data.begin() + static_cast<std::ptrdiff_t>(prefix),
                 entry.data.end());
    offset += static_cast<uint32_t>(entry.data.size() - prefix);
  }
  U16(&index, 0u);
  U16(&index, 0u);
  auto packedIndex = Compress(index);
  std::vector<uint8_t> encoded;
  U32(&encoded, static_cast<uint32_t>(index.size()));
  encoded.insert(encoded.end(), packedIndex.begin(), packedIndex.end());
  for (size_t i = 0; i < encoded.size(); ++i)
    encoded[i] ^= kPidxXorKey[i % kPidxXorKey.size()];
  bytes.insert(bytes.end(), encoded.begin(), encoded.end());
  bytes.insert(bytes.end(), {'S', 'T', 'K', 'F', 'i', 'l', 'e', '0',
                             'P', 'I', 'D', 'X'});
  U32(&bytes, static_cast<uint32_t>(encoded.size()));
  bytes.insert(bytes.end(), {'S', 'T', 'K', 'F', 'i', 'l', 'e', '0',
                             'P', 'A', 'C', 'K', 'F', 'I', 'L', 'E'});
  return bytes;
}

void Write(const fs::path &path, const std::vector<uint8_t> &bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  Check(static_cast<bool>(out), "Cannot create synthetic " + path.string());
  out.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  Check(static_cast<bool>(out), "Could not write synthetic " + path.string());
}

std::string HqOrs(std::string_view line, bool alterTiming = false) {
  return "[SkipFRAME]=00:00:00;\n"
         "[PrintText]=00:00:01\tSekai\t" + std::string(line) +
         (alterTiming ? "\t00:00:11;\n" : "\t00:00:10;\n") +
         "[Next]=00:00:12;\n";
}

std::string LxhOrs(std::string_view line) {
  return "[SkipFRAME]=00:00:00;\n"
         "[PrintText]=00:00:01\tSekai\t" + std::string(line) +
         "\t00:00:10;\n"
         "[EndRoll]=00:01:00\tCredits\t00:02:00;\n"
         "[PlayMovie]=00:02:01\tLxH/Media/05-A2-F00-LXH\t0\t00:03:00;\n"
         "[Next]=00:04:00;\n";
}

std::vector<Entry> ScriptEntries(std::string_view text, bool alterTiming = false) {
  return {
      {"English/01/01-AA-A00.ENG.ORS", HqOrs(text, alterTiming)},
      {"English/05/05-A2-F00.ENG.ORS", HqOrs(text)},
      {"English/README.BIN", "SYNTHETIC_NON_SCRIPT"},
  };
}

std::vector<Entry> LxhEntries(std::string_view text, bool withMedia) {
  std::vector<Entry> result;
  for (const auto &logical : SchoolDaysLxhContent::PackEntries()) {
    if (logical.size() > 4u && logical.substr(logical.size() - 4u) == ".ORS")
      result.push_back({logical, LxhOrs(text)});
    else if (withMedia)
      result.push_back({logical, "SYNTHETIC_ORIGINAL_MEDIA::" + logical});
  }
  return result;
}

void CreateRetail(const fs::path &root) {
  const fs::path packs = root / "Packs";
  for (const auto &spec : SchoolDaysRetailGpkSpecs()) {
    const std::string name(spec.filename);
    Write(packs / name, MakeGpk(name == "Script.GPK"
        ? ScriptEntries("Retail English HQ.")
        : std::vector<Entry>{{"DUMMY.BIN", "SYNTHETIC"}}));
  }
  Write(packs / "LxH.GPK", MakeGpk(LxhEntries("Original integrated LxH.", true)));
}

std::string ReadText(std::string_view logical) {
  std::vector<uint8_t> bytes;
  std::string error;
  Check(ReadMountedSchoolDaysAsset(logical, &bytes, &error),
        "Cannot read synthetic " + std::string(logical) + ": " + error);
  return std::string(bytes.begin(), bytes.end());
}

void CheckRejected(const SchoolDaysTranslatedGpkSelection &selection,
                   const fs::path &root, std::string_view reason) {
  std::string error;
  Check(!ConfigureSchoolDaysTranslatedGpks(selection, &error),
        "Invalid locale configuration was accepted: " + std::string(reason));
  Check(!MountSchoolDaysRetailGpks(root, &error),
        "Rejected locale silently mounted English retail assets: " +
        std::string(reason));
  Check(!SchoolDaysRetailGpksMounted(),
        "Rejected locale left mounted archives active");
  Check(ClearSchoolDaysTranslatedGpks(&error),
        "Cannot clear rejected locale configuration: " + error);
}

void VerifyMountedLocale(const fs::path &root, std::string_view hqText,
                         std::string_view lxhText) {
  std::string error;
  Check(MountSchoolDaysRetailGpks(root, &error),
        "Valid synthetic pack set did not mount: " + error);
  InstallMountedSchoolDaysAssetResolver();
  const std::string hq = "Script/English/01/01-AA-A00.ENG.ORS";
  const std::string lxh = "Script/English/05/05-A2-F00.ENG.ORS";
  Check(ReadText(hq).find(hqText) != std::string::npos,
        "Base HQ script came from wrong GPK");
  Check(ReadText(lxh).find(lxhText) != std::string::npos,
        "LxH epilogue ORS came from wrong GPK");
  const auto image = ReadText("LxH/05-A2-F00-LXH.png");
  Check(image.find("SYNTHETIC_ORIGINAL_MEDIA") != std::string::npos,
        "LxH image came from translated GPK instead of original LxH.GPK");

  const auto lookup = ResolveSchoolDaysAssetPath(root.string().c_str(), hq);
  Check(lookup.present && IsSchoolDaysGpkUri(lookup.physical),
        "Translated HQ script did not resolve to a GPK URI");
  if (hqText != "Retail English HQ.") {
    Check(lookup.physical.find("__sd_localized_script__") != std::string::npos,
          "Translated HQ URI can be shadowed by loose overlay");
    std::vector<uint8_t> bytes;
    Check(ReadSchoolDaysAssetRef(lookup.physical, &bytes, &error),
          "Marked URI cannot read translated GPK: " + error);
    Check(std::string(bytes.begin(), bytes.end()).find(hqText) != std::string::npos,
          "Marked URI resolved retail content");
  }
  UninstallMountedSchoolDaysAssetResolver();
  UnmountSchoolDaysRetailGpks();
}

} // namespace

int main(int argc, char **argv) {
  // Opt-in, read-only audit of user-owned installed packs. Never a CTest
  // default and never writes a configuration, real save or commercial GPK.
  if (argc == 6 && std::string_view(argv[1]) == "--audit-real") {
    try {
      const std::string locale = argv[2];
      const fs::path root = argv[3];
      const fs::path script = std::string_view(argv[4]) == "-"
                                  ? fs::path{} : fs::path(argv[4]);
      const fs::path lxh = argv[5];
      std::string error;
      Check(ConfigureSchoolDaysTranslatedGpks({locale, script, lxh}, &error),
            "Cannot select real translated GPK: " + error);
      Check(MountSchoolDaysRetailGpks(root, &error),
            "Real translated GPK failed native structural validation: " + error);
      const auto scene = ReadText("Script/English/00/00-00-A00.ENG.ORS");
      const auto ending = ReadText("Script/English/05/05-A2-F00.ENG.ORS");
      Check(scene.find("[PrintText]") != std::string::npos &&
                ending.find("LxH/Media/05-A2-F00-LXH") != std::string::npos,
            "Real translated ORS did not contain expected script and ending");
      std::cout << "REAL_TRANSLATED_GPK_AUDIT_PASS locale=" << locale
                << " sceneBytes=" << scene.size()
                << " endingBytes=" << ending.size() << '\n';
      UnmountSchoolDaysRetailGpks();
      return 0;
    } catch (const std::exception &e) {
      UnmountSchoolDaysRetailGpks();
      std::cerr << "REAL_TRANSLATED_GPK_AUDIT_FAIL " << e.what() << '\n';
      return 1;
    }
  }
  if (argc != 1) {
    std::cerr << "Usage: SchoolDaysTranslatedGpkMountTest "
                 "[--audit-real LOCALE RETAIL_ROOT SCRIPT_OR_DASH LXH_GPK]\n";
    return 2;
  }
  const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path root = fs::temp_directory_path() /
      ("SchoolDaysTranslatedGpkMountTest-" + std::to_string(unique));
  try {
    fs::create_directories(root);
    CreateRetail(root);
    const auto translations = root / "translations";
    const auto ptScript = translations / "Script - Brazilian.GPK";
    const auto ptLxh = translations / "pt-BR-lxh.GPK";
    const auto esScript = translations / "Script - Spanish.GPK";
    const auto esLxh = translations / "es-lxh.GPK";
    const auto enLxh = translations / "en-lxh.GPK";
    const auto enScript = translations / "Script - english.GPK";
    Write(ptScript, MakeGpk(ScriptEntries("Texto PT-BR.")));
    Write(ptLxh, MakeGpk(LxhEntries("Epilogo PT-BR.", false)));
    Write(esScript, MakeGpk(ScriptEntries("Texto español sintético.")));
    Write(esLxh, MakeGpk(LxhEntries("Epílogo español sintético.", false)));
    Write(enLxh, MakeGpk(LxhEntries("English epilogue.", false)));
    Write(enScript, MakeGpk(ScriptEntries("Unauthorized English override.")));

    CheckRejected({"en", enScript, enLxh}, root, "English cannot choose Script.GPK");
    CheckRejected({"zh-CN", ptScript, ptLxh}, root, "Chinese requires conversion");
    CheckRejected({"pt-BR", ptScript, {}}, root, "PT-BR missing LxH");
    CheckRejected({"pt-BR", enScript, ptLxh}, root, "PT-BR wrong base filename");
    CheckRejected({"pt-BR", root / "missing.GPK", ptLxh}, root, "PT-BR missing GPK");
    CheckRejected({"es", ptScript, esLxh}, root, "Spanish cannot use Brazilian base");
    CheckRejected({"es", esScript, ptLxh}, root, "Spanish requires its own LxH");
    CheckRejected({"es", esScript, {}}, root, "Spanish incomplete locale pair");

    std::string error;
    Check(ConfigureSchoolDaysTranslatedGpks({"en", {}, enLxh}, &error),
          "English LxH-only configuration rejected: " + error);
    VerifyMountedLocale(root, "Retail English HQ.", "English epilogue.");

    Check(ConfigureSchoolDaysTranslatedGpks({"pt-BR", ptScript, ptLxh}, &error),
          "Valid PT-BR GPK selection rejected: " + error);
    VerifyMountedLocale(root, "Texto PT-BR.", "Epilogo PT-BR.");

    Check(ConfigureSchoolDaysTranslatedGpks({"es", esScript, esLxh}, &error),
          "Valid synthetic Spanish GPK selection rejected: " + error);
    VerifyMountedLocale(root, "Texto español sintético.", "Epílogo español sintético.");

    Write(ptScript, MakeGpk(ScriptEntries("Texto PT-BR.", true)));
    Check(ConfigureSchoolDaysTranslatedGpks({"pt-BR", ptScript, ptLxh}, &error),
          "Cannot configure structural mismatch fixture: " + error);
    Check(!MountSchoolDaysRetailGpks(root, &error) &&
              error.find("structure mismatch") != std::string::npos,
          "Translated ORS timing mismatch was not rejected before mount");
    Check(!SchoolDaysRetailGpksMounted(),
          "Failed ORS compatibility check left a published mount");
    Check(ClearSchoolDaysTranslatedGpks(&error), "Could not clear failed selection");

    Check(MountSchoolDaysRetailGpks(root, &error),
          "Default retail English mount failed after clearing translation: " + error);
    Check(ReadText("Script/English/01/01-AA-A00.ENG.ORS").find(
              "Retail English HQ.") != std::string::npos,
          "Default English source is not original Packs/Script.GPK");
    UnmountSchoolDaysRetailGpks();
    std::error_code ec;
    fs::remove_all(root, ec);
    std::cout << "SchoolDaysTranslatedGpkMountTest: PASS\n";
    return 0;
  } catch (const std::exception &error) {
    UninstallMountedSchoolDaysAssetResolver();
    UnmountSchoolDaysRetailGpks();
    std::error_code ec;
    fs::remove_all(root, ec);
    std::cerr << "SchoolDaysTranslatedGpkMountTest: " << error.what() << '\n';
    return 1;
  }
}
