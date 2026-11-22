#include "SchoolDaysAssets.hpp"
#include "SchoolDaysLauncher.hpp"
#include "SchoolDaysRuntime.hpp"

#include <Kotonoha/SchoolDaysAssetMount.hpp>
#include <Kotonoha/SchoolDaysGpkArchive.hpp>
#include <Kotonoha/routing/SchoolDaysBackRules.hpp>
#include <Kotonoha/routing/SchoolDaysLxhContent.hpp>
#include <Kotonoha/routing/SchoolDaysLxhPreference.hpp>
#include <Kotonoha/routing/SchoolDaysSaveProvenance.hpp>
#include <Kotonoha/routing/SchoolDaysTranslationCoverage.hpp>

#include <SDL3/SDL.h>
#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr uint8_t kGpkKey[] = {
    0x82, 0xee, 0x1d, 0xb3, 0x57, 0xe9, 0x2c, 0xc2,
    0x2f, 0x54, 0x7b, 0x10, 0x4c, 0x9a, 0x75, 0x49};

void Require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

struct TempDir {
  fs::path path = fs::temp_directory_path() /
                  ("school-days-optional-lxh-" +
                   std::to_string(SDL_GetPerformanceCounter()));
  TempDir() { Require(fs::create_directories(path), "Cannot create temp dir"); }
  ~TempDir() {
    Kotonoha::UninstallMountedSchoolDaysAssetResolver();
    Kotonoha::UnmountSchoolDaysRetailGpks();
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

void WriteFile(const fs::path &p, const std::vector<uint8_t> &bytes) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  Require(static_cast<bool>(out), "Cannot write " + p.string());
}

void WriteText(const fs::path &p, const std::string &text) {
  WriteFile(p, std::vector<uint8_t>(text.begin(), text.end()));
}

void AppendLe16(std::vector<uint8_t> &out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xffu));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xffu));
}

void AppendLe32(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xffu));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xffu));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xffu));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xffu));
}

struct SyntheticEntry {
  std::string name;
  std::vector<uint8_t> data;
};

std::vector<uint8_t> BuildRawGpk(const std::vector<SyntheticEntry> &entries) {
  std::vector<uint8_t> body;
  std::vector<uint8_t> pidx;
  for (const auto &e : entries) {
    const uint32_t offset = static_cast<uint32_t>(body.size());
    body.insert(body.end(), e.data.begin(), e.data.end());
    std::u16string wide(e.name.begin(), e.name.end());
    AppendLe16(pidx, static_cast<uint16_t>(wide.size()));
    for (char16_t ch : wide)
      AppendLe16(pidx, static_cast<uint16_t>(ch));
    AppendLe16(pidx, 0u);  // header size / unused
    AppendLe16(pidx, 1u);  // version
    AppendLe16(pidx, 0u);  // flags
    AppendLe32(pidx, offset);
    AppendLe32(pidx, static_cast<uint32_t>(e.data.size()));
    pidx.insert(pidx.end(), {' ', ' ', ' ', ' '}); // raw magic
    AppendLe32(pidx, static_cast<uint32_t>(e.data.size()));
    pidx.push_back(0u); // inline prefix len
  }
  AppendLe32(pidx, 0u); // PIDX terminator (two zero u16s)
  std::vector<uint8_t> compressed(compressBound(static_cast<uLong>(pidx.size())));
  uLongf compSize = static_cast<uLongf>(compressed.size());
  Require(compress2(compressed.data(), &compSize, pidx.data(),
                    static_cast<uLong>(pidx.size()), Z_BEST_SPEED) == Z_OK,
          "zlib compress2 failed");
  compressed.resize(compSize);
  std::vector<uint8_t> coded;
  AppendLe32(coded, static_cast<uint32_t>(pidx.size()));
  coded.insert(coded.end(), compressed.begin(), compressed.end());
  for (size_t i = 0; i < coded.size(); ++i)
    coded[i] ^= kGpkKey[i % sizeof(kGpkKey)];

  std::vector<uint8_t> out = std::move(body);
  out.insert(out.end(), coded.begin(), coded.end());
  constexpr char kTrailer[] = "STKFile0PIDX";
  out.insert(out.end(), kTrailer, kTrailer + 12);
  AppendLe32(out, static_cast<uint32_t>(coded.size()));
  constexpr char kPack[] = "STKFile0PACKFILE";
  out.insert(out.end(), kPack, kPack + 16);
  return out;
}

std::vector<uint8_t> Bytes(const std::string &s) {
  return {s.begin(), s.end()};
}

std::string RetailOrs(const std::string &line) {
  return "[PrintText]=00:00:00\tMakoto\t" + line + "\t00:01:00;\n"
         "[EndRoll]=00:01:00\tCredits\t00:02:00;\n"
         "[Next]=00:02:00;\n";
}

std::string LxhOrs(const std::string &ending, const std::string &line) {
  return "[PrintText]=00:00:00\tMakoto\tBase " + ending + "\t00:01:00;\n"
         "[EndRoll]=00:01:00\tCredits\t00:02:00;\n"
         "[PlayMovie]=00:02:00\tLxH/Media/" + ending + "-LXH\t0\t00:03:00;\n"
         "[PrintText]=00:02:05\tMakoto\t" + line + "\t00:02:55;\n"
         "[Next]=00:03:00;\n";
}

void Build29RetailPacks(const fs::path &packsDir) {
  for (const auto &spec : Kotonoha::SchoolDaysRetailGpkSpecs()) {
    std::vector<SyntheticEntry> entries;
    if (std::string_view(spec.filename) == "Script.GPK") {
      entries.push_back({"English/00/00-00-A00.ENG.ORS",
                         Bytes("[PrintText]=00:00:00\tMakoto\tHello HQ.\t00:01:00;\n[Next]=00:01:00;\n")});
      for (const auto ending : Kotonoha::SchoolDaysLxhContent::Endings) {
        const std::string code(ending);
        entries.push_back({"English/05/" + code + ".ENG.ORS",
                           Bytes(RetailOrs("Retail ending " + code))});
      }
    } else {
      entries.push_back({"dummy.bin", Bytes("x")});
    }
    WriteFile(packsDir / spec.filename, BuildRawGpk(entries));
  }
}

void WriteValidLxhPack(const fs::path &packsDir) {
  std::vector<SyntheticEntry> entries;
  for (const auto ending : Kotonoha::SchoolDaysLxhContent::Endings) {
    const std::string code(ending);
    entries.push_back({"Script/English/05/" + code + ".ENG.ORS",
                       Bytes(LxhOrs(code, "LxH epilogue " + code))});
    entries.push_back({"LxH/" + code + "-LXH.png", Bytes("PNG")});
    entries.push_back({"LxH/Media/" + code + "-LXH.wmv", Bytes("WMV")});
    entries.push_back({"LxH/Media/" + code + "-LXH.ogg", Bytes("OGG")});
  }
  WriteFile(packsDir / "LxH.GPK", BuildRawGpk(entries));
}

void WritePtBrScriptGpk(const fs::path &path) {
  std::vector<SyntheticEntry> entries;
  entries.push_back({"English/00/00-00-A00.ENG.ORS",
                     Bytes("[PrintText]=00:00:00\tMakoto\tOla HQ.\t00:01:00;\n[Next]=00:01:00;\n")});
  for (const auto ending : Kotonoha::SchoolDaysLxhContent::Endings) {
    const std::string code(ending);
    entries.push_back({"English/05/" + code + ".ENG.ORS",
                       Bytes(RetailOrs("Final retail PT " + code))});
  }
  WriteFile(path, BuildRawGpk(entries));
}

void WritePtBrLxhGpk(const fs::path &path) {
  std::vector<SyntheticEntry> entries;
  for (const auto ending : Kotonoha::SchoolDaysLxhContent::Endings) {
    const std::string code(ending);
    entries.push_back({"Script/English/05/" + code + ".ENG.ORS",
                       Bytes(LxhOrs(code, "Epilogo LxH PT " + code))});
  }
  WriteFile(path, BuildRawGpk(entries));
}

void TestPreferencePersistenceAndResolution(const fs::path &root) {
  using Pref = Kotonoha::SchoolDaysLxhPreference;
  const fs::path docs = root / "prefs";
  fs::create_directories(docs);
  WriteText(docs / "Kotonoha.ini",
            "[KotonohaConfig][Settings]\nContentAspectPreference=2\nAndroidFrameLimit=60\n");

  Require(!Pref::Read(docs).has_value(),
          "Unset Kotonoha.ini must return std::nullopt");

  Pref::Availability absent{false, false, "Packs/LxH.GPK is not installed"};
  Pref::Availability valid{true, true, "Packs/LxH.GPK validated (28/28 entries)"};
  Pref::Availability corrupt{true, false, "Packs/LxH.GPK must contain exactly 28 files"};

  auto r = Pref::Resolve(std::nullopt, absent);
  Require(!r.explicitPreference && !r.userPreference && !r.effectiveEnabled,
          "Fresh install without LxH must default OFF");
  r = Pref::Resolve(std::nullopt, valid);
  Require(!r.explicitPreference && r.userPreference && r.effectiveEnabled,
          "Fresh install with valid LxH must default ON");

  // Explicit user choice ON persists even when files are absent!
  Pref::Write(docs, true);
  auto readBack = Pref::Read(docs);
  Require(readBack.has_value() && *readBack, "EnableLxh=1 did not persist");
  {
    std::ifstream iniIn(docs / "Kotonoha.ini");
    std::string iniText((std::istreambuf_iterator<char>(iniIn)), {});
    Require(iniText.find("ContentAspectPreference=2") != std::string::npos &&
                iniText.find("AndroidFrameLimit=60") != std::string::npos,
            "Writing EnableLxh clobbered existing Kotonoha.ini keys");
  }

  r = Pref::Resolve(readBack, absent);
  Require(r.explicitPreference && r.userPreference && !r.effectiveEnabled &&
              !r.statusReason.empty(),
          "Explicit ON with missing pack must keep preference ON and effective OFF");
  r = Pref::Resolve(readBack, corrupt);
  Require(r.explicitPreference && r.userPreference && !r.effectiveEnabled &&
              r.statusReason.find("28") != std::string::npos,
          "Explicit ON with corrupt pack must disable effective LxH with reason");
  r = Pref::Resolve(readBack, valid, false, "missing pt-BR-lxh.json");
  Require(r.explicitPreference && r.userPreference && !r.effectiveEnabled &&
              r.statusReason.find("pt-BR-lxh.json") != std::string::npos,
          "Missing LxH translation must allow base HQ without silent fallback");

  // Explicit OFF stays OFF even when valid LxH pack exists!
  Pref::Write(docs, false);
  readBack = Pref::Read(docs);
  Require(readBack.has_value() && !*readBack, "EnableLxh=0 did not persist");
  r = Pref::Resolve(readBack, valid);
  Require(r.explicitPreference && !r.userPreference && !r.effectiveEnabled,
          "Explicit OFF must stay OFF even when valid LxH.GPK is present");
}

void TestMountAndBase29Independence(const fs::path &root) {
  const fs::path gameDir = root / "game29";
  const fs::path packs = gameDir / "Packs";
  Build29RetailPacks(packs);

  std::string info;
  Require(SchoolDays::Assets::checkDirectory(gameDir, info),
          "29 original retail GPKs without LxH.GPK must pass checkDirectory: " + info);

  // 1. Optional mount without LxH.GPK succeeds with 29 retail GPKs.
  std::string err;
  Kotonoha::SchoolDaysLxhMountStatus status;
  Require(Kotonoha::MountSchoolDaysRetailGpks(
              gameDir, &err, {}, Kotonoha::SchoolDaysLxhMountMode::Optional, &status),
          "Optional mount without LxH.GPK failed: " + err);
  Require(Kotonoha::SchoolDaysRetailGpksMounted() && !Kotonoha::SchoolDaysLxhMounted(),
          "Retail should be mounted and LxH unmounted");
  Require(status.requested && !status.mounted && !status.notice.empty(),
          "Optional status must record missing LxH notice");

  // All 7 endings resolve from retail Script.GPK (zero LxH grafts).
  for (const auto ending : Kotonoha::SchoolDaysLxhContent::Endings) {
    std::vector<uint8_t> bytes;
    const std::string logical = "Script/English/05/" + std::string(ending) + ".ENG.ORS";
    Require(Kotonoha::ReadMountedSchoolDaysAsset(logical, &bytes, &err),
            "Retail ending script failed to read with LxH OFF: " + err);
    const std::string text(bytes.begin(), bytes.end());
    Require(text.find("Retail ending " + std::string(ending)) != std::string::npos &&
                text.find("LxH/Media/") == std::string::npos,
            "Retail ending contained unexpected LxH graft when LxH is OFF");
    Require(Kotonoha::LookupMountedSchoolDaysAsset(
                "LxH/Media/" + std::string(ending) + "-LXH.wmv") ==
                Kotonoha::SchoolDaysGpkLookupStatus::Missing,
            "LxH media must be Missing when LxH is OFF");
  }
  Kotonoha::UnmountSchoolDaysRetailGpks();

  // 2. Corrupt LxH.GPK: Required fails, Optional cleanly mounts base 29 only!
  WriteText(packs / "LxH.GPK", "NOT_A_VALID_GPK");
  Require(!Kotonoha::MountSchoolDaysRetailGpks(
              gameDir, &err, {}, Kotonoha::SchoolDaysLxhMountMode::Required),
          "Required mount must reject corrupt LxH.GPK");
  Require(Kotonoha::MountSchoolDaysRetailGpks(
              gameDir, &err, {}, Kotonoha::SchoolDaysLxhMountMode::Optional, &status),
          "Optional mount must allow base 29 when LxH.GPK is corrupt: " + err);
  Require(!Kotonoha::SchoolDaysLxhMounted() && !status.mounted,
          "Corrupt LxH.GPK must never mount partially");
  Kotonoha::UnmountSchoolDaysRetailGpks();

  // 3. Valid LxH.GPK: Disabled STILL mounts ONLY base 29 (pure retail endings).
  WriteValidLxhPack(packs);
  Require(Kotonoha::MountSchoolDaysRetailGpks(
              gameDir, &err, {}, Kotonoha::SchoolDaysLxhMountMode::Disabled, &status),
          "Disabled mount failed: " + err);
  Require(!Kotonoha::SchoolDaysLxhMounted(), "Disabled mode mounted LxH.GPK");
  {
    std::vector<uint8_t> bytes;
    Require(Kotonoha::ReadMountedSchoolDaysAsset(
                "Script/English/05/05-A2-F00.ENG.ORS", &bytes, &err),
            err);
    const std::string text(bytes.begin(), bytes.end());
    Require(text.find("Retail ending 05-A2-F00") != std::string::npos,
            "Disabled mode did not serve pure retail script");
  }
  Kotonoha::UnmountSchoolDaysRetailGpks();

  // 4. Valid LxH.GPK + Required: all 7 LxH endings + media mounted strictly!
  Require(Kotonoha::MountSchoolDaysRetailGpks(
              gameDir, &err, {}, Kotonoha::SchoolDaysLxhMountMode::Required, &status),
          "Required mount with valid LxH failed: " + err);
  Require(Kotonoha::SchoolDaysLxhMounted() && status.mounted,
          "Valid LxH.GPK did not mount");
  for (const auto ending : Kotonoha::SchoolDaysLxhContent::Endings) {
    std::vector<uint8_t> bytes;
    const std::string code(ending);
    Require(Kotonoha::ReadMountedSchoolDaysAsset(
                "Script/English/05/" + code + ".ENG.ORS", &bytes, &err),
            err);
    const std::string text(bytes.begin(), bytes.end());
    Require(text.find("LxH epilogue " + code) != std::string::npos &&
                text.find("LxH/Media/" + code + "-LXH") != std::string::npos,
            "LxH ON did not serve grafted ending " + code);
    Require(Kotonoha::LookupMountedSchoolDaysAsset("LxH/" + code + "-LXH.png") ==
                Kotonoha::SchoolDaysGpkLookupStatus::Present,
            "LxH PNG missing when LxH ON");
  }
  Kotonoha::UnmountSchoolDaysRetailGpks();

  // 5. Non-English GPK (pt-BR) WITHOUT pt-BR-lxh.GPK succeeds when LxH is OFF!
  const fs::path ptScript = root / "translations" / "scripts" / "Script - Brazilian.GPK";
  const fs::path ptLxh = root / "translations" / "scripts" / "pt-BR-lxh.GPK";
  WritePtBrScriptGpk(ptScript);
  Require(Kotonoha::ConfigureSchoolDaysTranslatedGpks(
              {"pt-BR", ptScript, {}, /*enableLxh=*/false}, &err),
          "pt-BR base GPK without LxH GPK failed when LxH OFF: " + err);
  Require(Kotonoha::MountSchoolDaysRetailGpks(
              gameDir, &err, {}, Kotonoha::SchoolDaysLxhMountMode::Disabled),
          "Mount pt-BR base GPK with LxH OFF failed: " + err);
  {
    std::vector<uint8_t> bytes;
    Require(Kotonoha::ReadMountedSchoolDaysAsset(
                "Script/English/05/05-A2-F00.ENG.ORS", &bytes, &err),
            err);
    const std::string text(bytes.begin(), bytes.end());
    Require(text.find("Final retail PT 05-A2-F00") != std::string::npos,
            "pt-BR with LxH OFF did not serve translated base retail ending");
  }
  Kotonoha::UnmountSchoolDaysRetailGpks();

  // 6. Non-English GPK (pt-BR) WITH pt-BR-lxh.GPK succeeds when LxH is ON!
  WritePtBrLxhGpk(ptLxh);
  Require(Kotonoha::ConfigureSchoolDaysTranslatedGpks(
              {"pt-BR", ptScript, ptLxh, /*enableLxh=*/true}, &err),
          "pt-BR + LxH GPK failed when LxH ON: " + err);
  Require(Kotonoha::MountSchoolDaysRetailGpks(
              gameDir, &err, {}, Kotonoha::SchoolDaysLxhMountMode::Required),
          "Mount pt-BR + LxH failed: " + err);
  {
    std::vector<uint8_t> bytes;
    Require(Kotonoha::ReadMountedSchoolDaysAsset(
                "Script/English/05/05-A2-F00.ENG.ORS", &bytes, &err),
            err);
    const std::string text(bytes.begin(), bytes.end());
    Require(text.find("Epilogo LxH PT 05-A2-F00") != std::string::npos,
            "pt-BR with LxH ON did not serve translated LxH epilogue");
  }
  Kotonoha::UnmountSchoolDaysRetailGpks();
}

void TestSaveProvenanceEnforcement(const fs::path &root, const char *routePath) {
  const fs::path gameDir = root / "savegame";
  const fs::path packs = gameDir / "Packs";
  const fs::path docs = root / "savedocs";
  fs::create_directories(docs);
  Build29RetailPacks(packs);
  WriteValidLxhPack(packs);

  // Create normal save (slot 0), legacy save without sidecar (slot 1),
  // and attested LxH epilogue save (slot 2) in an isolated documents dir.
  std::vector<uint8_t> normalBytes;
  std::vector<uint8_t> legacyBytes;
  std::vector<uint8_t> lxhBytes;
  fs::path slot2Path;
  {
    auto assetsOn = std::make_shared<SchoolDays::Assets>(
        gameDir, "", false, nullptr, Kotonoha::SchoolDaysLxhMountMode::Required);
    Require(assetsOn->lxhMounted(), "Expected LxH ON for save fixture creation");
    SchoolDays::Runtime runtime(assetsOn, routePath, docs.string());
    runtime.SetState(Kotonoha::GameState::Gameplay);
    runtime.saveGame((docs / "NormalSave").string());
    runtime.saveGame((docs / "LegacySave").string());
    std::error_code ec;
    fs::remove(Kotonoha::SchoolDaysSaveProvenance::SidecarPath(docs / "LegacySave"), ec);

    // Build a valid SLog pointing to 05/05-A2-F00 and publish LxH provenance.
    Kotonoha::SchoolDaysKtrfSession session;
    Kotonoha_KtrfError kerr{};
    Require(session.Open(routePath, assetsOn->orsRoot().string().c_str(), &kerr),
            kerr.message);
    Kotonoha::SchoolDaysKtrfScene startScene;
    Require(session.ResetNewGame(&startScene, &kerr), kerr.message);
    Kotonoha::SchoolDaysKtrfScene targetScene;
    const auto *doc = session.Adapter()->document;
    const auto *nodes = Kotonoha_KtrfFindSection(doc, "NODE");
    uint32_t endingNode = KOTONOHA_KTRF_NULL_INDEX;
    for (uint32_t i = 0; i < nodes->item_count; ++i) {
      Kotonoha::SchoolDaysKtrfScene cand;
      if (session.ResolveSceneNode(i, &cand, &kerr) &&
          cand.sceneKey == "05/05-A2-F00") {
        endingNode = i;
        targetScene = cand;
        break;
      }
    }
    Require(endingNode != KOTONOHA_KTRF_NULL_INDEX, "Cannot find 05/05-A2-F00 in route");
    int routeCoord = 0, sceneCoord = 0;
    Require(Kotonoha::SchoolDaysBackRules::CoordinateForSceneKey(
                targetScene.sceneKey, &routeCoord, &sceneCoord),
            "Cannot map 05/05-A2-F00 to ROUTE/SCENE");
    Require(session.DebugActivateNode(endingNode, &kerr), kerr.message);
    Require(session.State().WriteCoordinate(routeCoord, sceneCoord, &kerr),
            kerr.message);
    auto persist = session.Persistence();
    Require(persist.ReplaceCurrentScriptSnapshot(targetScene.sceneKey, 0x3f800000u,
                                                 session.State(), &kerr),
            kerr.message);
    Require(persist.SerializeSaveLog(&lxhBytes, &kerr), kerr.message);
    slot2Path = docs / "LxhSave";
    WriteFile(slot2Path, lxhBytes);
    std::string perr;
    Kotonoha::SchoolDaysSaveProvenance::Origin origin{true, 150u, 120u, 180u};
    const bool published = Kotonoha::SchoolDaysSaveProvenance::Publish(
        slot2Path, -1, lxhBytes, origin, &perr);
    Require(published, "Failed to publish LxH save provenance: " + perr);

    // With LxH ON, all 3 saves (normal, legacy, LxH) load cleanly!
    runtime.loadGame((docs / "NormalSave").string());
    runtime.loadGame((docs / "LegacySave").string());
    runtime.loadGame(slot2Path.string());
    runtime.shutdown();
  }

  // Now launch with LxH OFF!
  {
    auto assetsOff = std::make_shared<SchoolDays::Assets>(
        gameDir, "", false, nullptr, Kotonoha::SchoolDaysLxhMountMode::Disabled);
    Require(!assetsOff->lxhMounted(), "Expected LxH OFF for load protection test");
    SchoolDays::Runtime runtime(assetsOff, routePath, docs.string());
    runtime.SetState(Kotonoha::GameState::Gameplay);
    // Normal save + LxH OFF -> loads!
    runtime.loadGame((docs / "NormalSave").string());
    // Legacy save (no provenance) + LxH OFF -> loads!
    runtime.loadGame((docs / "LegacySave").string());
    // LxH epilogue save + LxH OFF -> rejected cleanly, file untouched!
    bool rejected = false;
    std::string msg;
    try {
      runtime.loadGame(slot2Path.string());
    } catch (const std::runtime_error &e) {
      rejected = true;
      msg = e.what();
    }
    Require(rejected && msg.find("LxH") != std::string::npos,
            "LxH save must be rejected with clear warning when LxH is OFF");
    Require(fs::is_regular_file(slot2Path) &&
                fs::is_regular_file(
                    Kotonoha::SchoolDaysSaveProvenance::SidecarPath(slot2Path)),
            "Rejected LxH save or its sidecar was deleted/corrupted");
    runtime.shutdown();
  }
}

void TestLauncherCheckboxAndPersistence(const fs::path &root,
                                        const char *routePath) {
  const fs::path gameDir = root / "launcher_game";
  const fs::path packs = gameDir / "Packs";
  const fs::path docs = root / "launcher_docs";
  const fs::path settings = root / "launcher_cfg" / "settings.json";
  fs::create_directories(docs);
  fs::create_directories(settings.parent_path());
  Build29RetailPacks(packs);

  // 1. No LxH.GPK: defaults OFF, EnglishGpk launches base HQ cleanly!
  {
    SchoolDays::Launcher l(settings.string(), gameDir.string(), routePath);
    l.setRetailConfigRoot(docs);
    Require(!l.enableLxhPreference(),
            "Launcher without LxH.GPK must default checkbox OFF");
    l.setLanguage(SchoolDays::Launcher::Language::EnglishGpk);
    Require(l.launch() && !l.effectiveLxhEnabled(),
            "Launcher must allow base EnglishGpk without LxH.GPK: " + l.getStatus());
  }

  // 2. User explicitly checks ON while LxH.GPK is absent -> persists EnableLxh=1,
  //    does NOT erase preference, explains unavailability, allows base HQ!
  {
    SchoolDays::Launcher l(settings.string(), gameDir.string(), routePath);
    l.setRetailConfigRoot(docs);
    l.setEnableLxhPreference(true);
    l.setLanguage(SchoolDays::Launcher::Language::EnglishGpk);
    Require(l.launch() && !l.effectiveLxhEnabled() &&
                !l.lxhStatusNotice().empty(),
            "Launcher with ON + missing LxH.GPK must launch base HQ with notice");
  }
  auto saved = Kotonoha::SchoolDaysLxhPreference::Read(docs);
  Require(saved.has_value() && *saved,
          "Explicit EnableLxh=1 must survive missing LxH.GPK");

  // 3. Now user installs LxH.GPK + en-lxh.GPK -> preference was remembered ON -> effective ON!
  WriteValidLxhPack(packs);
  const fs::path enLxh = settings.parent_path() / "translations" / "scripts" / "en-lxh.GPK";
  WritePtBrLxhGpk(enLxh);
  {
    SchoolDays::Launcher l(settings.string(), gameDir.string(), routePath);
    l.setRetailConfigRoot(docs);
    Require(l.enableLxhPreference(),
            "Reopened launcher must remember explicit EnableLxh=1");
    l.setLanguage(SchoolDays::Launcher::Language::EnglishGpk);
    Require(l.launch() && l.effectiveLxhEnabled(),
            "Launcher with ON + valid LxH.GPK + en-lxh.GPK must activate LxH: " +
                l.getStatus());
  }

  // 4. User toggles OFF -> persists EnableLxh=0 -> effective OFF even with LxH.GPK present!
  {
    SchoolDays::Launcher l(settings.string(), gameDir.string(), routePath);
    l.setRetailConfigRoot(docs);
    l.setEnableLxhPreference(false);
    l.setLanguage(SchoolDays::Launcher::Language::EnglishGpk);
    Require(l.launch() && !l.effectiveLxhEnabled(),
            "Explicit OFF must disable LxH even when files exist");
  }
  saved = Kotonoha::SchoolDaysLxhPreference::Read(docs);
  Require(saved.has_value() && !*saved, "Explicit EnableLxh=0 did not persist");
}

} // namespace

int main(int argc, char **argv) {
  try {
    Require(argc == 2, "Usage: SchoolDaysOptionalLxhTest ktrf-route");
    TempDir temp;
    TestPreferencePersistenceAndResolution(temp.path);
    TestMountAndBase29Independence(temp.path);
    TestSaveProvenanceEnforcement(temp.path, argv[1]);
    TestLauncherCheckboxAndPersistence(temp.path, argv[1]);
    std::cout << "SchoolDaysOptionalLxhTest PASS: preference persistence, "
                 "29-GPK independence, ON/OFF endings, pt-BR base GPK, "
                 "save provenance enforcement, launcher checkbox\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysOptionalLxhTest FAIL: " << error.what() << '\n';
    return 1;
  }
}
