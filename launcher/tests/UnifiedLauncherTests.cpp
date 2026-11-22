// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <KotonohaLauncher/LauncherBridge.h>
#include <KotonohaLauncher/UnifiedLauncher.hpp>

#include <Kotonoha/Filesystem/StoragePaths.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
namespace fs = std::filesystem;
using namespace KotonohaLauncher;
using Kotonoha::Filesystem::EngineKind;

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

class SyntheticLibrary {
public:
  fs::path root;
  SyntheticLibrary()
      : root(fs::temp_directory_path() /
             ("kotonoha-unified-lib-" +
              std::to_string(std::chrono::steady_clock::now()
                                 .time_since_epoch()
                                 .count()))) {
    fs::create_directories(root);
  }
  ~SyntheticLibrary() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }

  void write(const fs::path &rel, std::string_view content = "fixture") const {
    fs::create_directories((root / rel).parent_path());
    std::ofstream out(root / rel, std::ios::binary);
    if (!out)
      throw std::runtime_error("Cannot create fixture: " + rel.string());
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
  }
};

} // namespace

int main() {
  try {
    // 1. The hub links 13 engine families (16 identities), never the Example demo.
    const auto &reg = EngineLibraryRegistry::instance();
    require(reg.all().size() == 16, "Exactly 16 game identities must be "
                                    "registered (13 shared engine families)");
    require(!reg.find(EngineKind::Unknown),
            "The Example demo must not be registered in the hub");
    require(reg.find(EngineKind::Artemis) &&
                reg.find(EngineKind::Artemis)->libraryTarget ==
                    "artemis_port",
            "Artemis maps to artemis_port library");
    require(reg.find(EngineKind::RealLive) &&
                reg.find(EngineKind::RealLive)->libraryTarget ==
                    "reallive_port",
            "RealLive maps to reallive_port library");
    require(reg.find(EngineKind::ONScripter) &&
                reg.find(EngineKind::ONScripter)->libraryTarget ==
                    "ponscripter_port",
            "ONScripter/PONScripter maps to ponscripter_port library");
    require(reg.find(EngineKind::SnowRadish) &&
                reg.find(EngineKind::SnowRadish)->libraryTarget ==
                    "snow_radish_vacation",
            "SnowRadish maps to snow_radish_vacation library");
    require(reg.find(EngineKind::SummerRadish) &&
                reg.find(EngineKind::SummerRadish)->libraryTarget ==
                    "summer_radish_vacation",
            "SummerRadish maps to summer_radish_vacation library");
    require(reg.find(EngineKind::ZeroNoTsukaima) &&
                reg.find(EngineKind::ZeroNoTsukaima)->libraryTarget ==
                    "zero_no_tsukaima",
            "ZeroNoTsukaima maps to zero_no_tsukaima library");

    require(reg.findBySlug("meo") &&
                reg.findBySlug("meo")->targetBinary == "MEO",
            "Miss Each Other must select the MEO port");
    require(reg.findBySlug("katawa-shoujo") &&
                reg.findBySlug("katawa-shoujo")->targetBinary == "KatawaShoujo",
            "Katawa Shoujo must select its own port");
    require(reg.findBySlug("renpy") &&
                reg.findBySlug("renpy")->libraryTarget ==
                    "doki_doki_literature_club",
            "Generic Ren'Py games use the shared VM without DDLC branding");

    // The registry is a link-time contract. No external executable is needed.
#if defined(KOTONOHA_LAUNCH_AMAGAMI)
    require(reg.canInstantiateInProcess(EngineKind::Amagami),
            "Linked AMAGAMI must expose a real in-process factory");
#else
    require(!reg.canInstantiateInProcess(EngineKind::Amagami),
            "Unlinked AMAGAMI must not advertise an in-process runtime");
#endif
#if defined(KOTONOHA_LAUNCH_ZERO)
    require(reg.canInstantiateInProcess(EngineKind::ZeroNoTsukaima),
            "Linked ZERO must expose a real in-process factory");
#else
    require(!reg.canInstantiateInProcess(EngineKind::ZeroNoTsukaima),
            "Unlinked ZERO must not advertise an in-process runtime");
#endif
#if defined(KOTONOHA_LAUNCH_SNOW)
    require(reg.canInstantiateInProcess(EngineKind::SnowRadish),
            "Linked SNOW must expose a real in-process factory");
#else
    require(!reg.canInstantiateInProcess(EngineKind::SnowRadish),
            "Unlinked SNOW must not advertise an in-process runtime");
#endif
#if defined(KOTONOHA_LAUNCH_SUMMER)
    require(reg.canInstantiateInProcess(EngineKind::SummerRadish),
            "Linked SUMMER must expose a real in-process factory");
#else
    require(!reg.canInstantiateInProcess(EngineKind::SummerRadish),
            "Unlinked SUMMER must not advertise an in-process runtime");
#endif
#if defined(KOTONOHA_LAUNCH_DDLC)
    require(reg.canInstantiateInProcess(EngineKind::RenPy),
            "Linked DDLC must expose a real in-process factory");
#else
    require(!reg.canInstantiateInProcess(EngineKind::RenPy),
            "Unlinked DDLC must not advertise an in-process runtime");
#endif
#if defined(KOTONOHA_LAUNCH_SCHOOL_DAYS)
    require(reg.canInstantiateInProcess(EngineKind::SchoolDays),
            "Linked SCHOOL_DAYS must expose a real in-process factory");
#else
    require(!reg.canInstantiateInProcess(EngineKind::SchoolDays),
            "Unlinked SCHOOL_DAYS must not advertise an in-process runtime");
#endif
#if defined(KOTONOHA_LAUNCH_REALLIVE)
    require(reg.canInstantiateInProcess(EngineKind::RealLive),
            "Linked REALLIVE must expose a real in-process factory");
#else
    require(!reg.canInstantiateInProcess(EngineKind::RealLive),
            "Unlinked REALLIVE must not advertise an in-process runtime");
#endif
#if defined(KOTONOHA_LAUNCH_KIRIKIRI)
    require(reg.canInstantiateInProcess(EngineKind::Kirikiri),
            "Linked KIRIKIRI must expose a real in-process factory");
#else
    require(!reg.canInstantiateInProcess(EngineKind::Kirikiri),
            "Unlinked KIRIKIRI must not advertise an in-process runtime");
#endif
#if defined(KOTONOHA_LAUNCH_LUCA_SYSTEM)
    require(reg.canInstantiateInProcess(EngineKind::LucaSystem),
            "Linked LUCA_SYSTEM must expose a real in-process factory");
#else
    require(!reg.canInstantiateInProcess(EngineKind::LucaSystem),
            "Unlinked LUCA_SYSTEM must not advertise an in-process runtime");
#endif
#if defined(KOTONOHA_LAUNCH_PMAIL)
    require(reg.canInstantiateInProcess(EngineKind::PureMail),
            "Linked PMAIL must expose a real in-process factory");
#else
    require(!reg.canInstantiateInProcess(EngineKind::PureMail),
            "Unlinked PMAIL must not advertise an in-process runtime");
#endif
#if defined(KOTONOHA_LAUNCH_N2SYSTEM)
    require(reg.canInstantiateInProcess(EngineKind::N2System),
            "Linked N2SYSTEM must expose a real in-process factory");
#else
    require(!reg.canInstantiateInProcess(EngineKind::N2System),
            "Unlinked N2SYSTEM must not advertise an in-process runtime");
#endif
#if defined(KOTONOHA_LAUNCH_PONSCRIPTER)
    require(reg.canInstantiateInProcess(EngineKind::ONScripter),
            "Linked PONSCRIPTER must expose a real in-process factory");
#else
    require(!reg.canInstantiateInProcess(EngineKind::ONScripter),
            "Unlinked PONSCRIPTER must not advertise an in-process runtime");
#endif

    // 2. Build a synthetic multi-game library with same-engine titles and
    //    companion patches to verify detection + per-game save splitting.
    SyntheticLibrary lib;
    // Two RealLive games
    lib.write("AIR_PT-BR/Seen.txt", "KPAC");
    lib.write("AIR_PT-BR/Gameexe.ini", "#CAPTION=\"AIR\"");
    lib.write("AIR_PT-BR/rlBabel.dll", "MZ");
    lib.write("Clannad_PT-BR/Seen.txt", "KPAC");
    lib.write("Clannad_PT-BR/Gameexe.ini", "#CAPTION=\"CLANNAD\"");
    lib.write("Clannad_PT-BR/rlBabel.dll", "MZ");

    // Three CMP / SummerRadish / MEO games
    lib.write("SummerRadishVacation/DATA/SCRIPT.CMP", "PACK");
    lib.write("SummerRadishVacation/DATA/GRAPHIC.CMP", "PACK");
    lib.write("SummerRadishVacation2/DATA/SCRIPT.CMP", "PACK");
    lib.write("SummerRadishVacation2/DATA/GRAPHIC.CMP", "PACK");
    lib.write("MEO/DATA/SCRIPT.CMP", "PACK");
    lib.write("MEO/DATA/GRAPHIC.CMP", "PACK");
    lib.write("MEO/gEngine.gesl", "MZ");

    // Katawa shares the Ren'Py VM, but has its own package and executable.
    lib.write("KatawaShoujo/game/script-a1-monday.rpyc", "RPYC");

    // Generic Ren'Py games need not use DDLC's scripts/images archive names.
    lib.write("AnotherRenpy/game/archive.rpa",
              "RPA-3.0 0000000000000019 12345678\n");

    // Snow Radish Vacation
    lib.write("SnowRadishVacation/DATA/SCRIPT.PAK", "PACK");
    lib.write("SnowRadishVacation/DATA/GRAPHIC.PAK", "PACK");

    // PONScripter (Saya no Uta PT-BR)
    lib.write("SayaNoUta_PT-BR/0.utf", ";mode800\n*define\ngame\n*start\n");
    lib.write("SayaNoUta_PT-BR/arc.nsa", "NSA");

    // DDLC + DDLC_PT-BR_Patch (VNX)
    lib.write("DDLC/game/scripts.rpa", "RPA-3.0 0000000000000019 12345678\n");
    lib.write("DDLC/game/images.rpa", "RPA-3.0 0000000000000019 12345678\n");
    lib.write("DDLC_PT-BR_Patch/vnx/game/vnx.rpx", "RPE-1.0");
    lib.write("DDLC_PT-BR_Patch/game/vnxsurvival.rpyc", "RPYC");

    const auto catalog = scanGameLibrary(lib.root.string());
    require(catalog.size() >= 8,
            "Expected at least 8 discovered games in synthetic library, got " +
                std::to_string(catalog.size()));

    // The library may also contain non-game folders. Cache their negative
    // probes without masking a game installed into one of them later.
    SyntheticLibrary changing;
    changing.write("NewTitle/unrelated/notes.txt", "not a game");
    require(scanGameLibrary(changing.root.string()).empty(),
            "Unrelated folder must not be a detected game");
    changing.write("NewTitle/data.xp3", "XP3 placeholder");
    std::error_code timestampError;
    fs::last_write_time(changing.root / "NewTitle",
                        fs::file_time_type::clock::now() +
                            std::chrono::seconds(5),
                        timestampError);
    require(!timestampError, "Cannot set scan-cache fixture timestamp");
    const auto changedCatalog = scanGameLibrary(changing.root.string());
    require(changedCatalog.size() == 1 &&
                changedCatalog[0].engine.kind == EngineKind::Kirikiri,
            "Scan-cache must invalidate after adding a known game archive");

    // 3. Verify that every game gets a unique isolated save directory when
    //    splitSavesByGame is true, even when sharing the same engine &
    //    packageId!
    const fs::path saveRoot = lib.root / "save-root";
    std::set<std::string> uniqueSaveDirs;
    bool foundDdlcOverlayPairing = false;
    bool foundMeo = false;
    bool foundKatawa = false;
    bool foundGenericRenpy = false;
    bool foundPonscripter = false;

    for (const auto &entry : catalog) {
      const std::string saveDir =
          resolveGameSaveDirectory(entry, true, saveRoot.string());
      require(!saveDir.empty(), "Save directory must not be empty");
      if (entry.title == "DDLC") {
        require(!entry.overlayPath.empty(),
                "DDLC must auto-pair with DDLC_PT-BR_Patch");
        foundDdlcOverlayPairing = true;
      }
      if (entry.title == "MEO") {
        require(entry.engine.kind == EngineKind::SummerRadish &&
                    entry.engine.slug == "meo" &&
                    entry.libraryTarget == "summer_radish_vacation" &&
                    entry.targetBinary == "MEO" &&
                    entry.packageId == "me.hirameki.kotonoha.meo",
                "MEO must use the MEO identity and shared CMP library");
        foundMeo = true;
      }
      if (entry.title == "KatawaShoujo") {
        require(
            entry.engine.kind == EngineKind::RenPy &&
                entry.engine.slug == "katawa-shoujo" &&
                entry.libraryTarget == "doki_doki_literature_club" &&
                entry.targetBinary == "KatawaShoujo" &&
                entry.packageId == "me.hirameki.kotonoha.katawashoujo" &&
                entry.overlayPath.empty(),
            "Katawa must use its own game identity without DDLC patch overlay");
        foundKatawa = true;
      }
      if (entry.title == "AnotherRenpy") {
        require(entry.engine.kind == EngineKind::RenPy &&
                    entry.engine.slug == "renpy" &&
                    entry.overlayPath.empty(),
                "Generic Ren'Py must avoid DDLC identity and VNX overlay");
        foundGenericRenpy = true;
      }
      if (entry.title == "SayaNoUta_PT-BR") {
        require(entry.engine.kind == EngineKind::ONScripter &&
                    entry.engine.implementedInKotonoha &&
                    entry.libraryTarget == "ponscripter_port",
                "SayaNoUta_PT-BR must map to implemented ponscripter_port");
        foundPonscripter = true;
      }
      require(uniqueSaveDirs.insert(saveDir).second,
              "Collision in per-game isolated save directory: " + saveDir);
    }

    require(foundDdlcOverlayPairing, "DDLC overlay pairing verified");
    require(foundMeo, "MEO detection verified");
    require(foundKatawa, "Katawa Shoujo detection verified");
    require(foundGenericRenpy, "Generic Ren'Py detection verified");
    require(foundPonscripter, "PONScripter detection verified");

    // 4. Verify applyPerGameSaveIsolation updates KOTONOHA_USER_DATA_DIR and
    //    settingsPath to point inside the per-game profile.
    Kotonoha::EngineOptions opts;
    opts.persistSettings = true;
    applyPerGameSaveIsolation(catalog.front(), opts, true, saveRoot.string());
    require(opts.settingsPath.find(catalog.front().saveProfileId) !=
                std::string::npos,
            "Settings path must be isolated inside the game's profile folder");

    // 5. Verify LauncherBridge C ABI (used by iOS SwiftUI & Android Material 3)
    const std::string rootStr = lib.root.string();
    const char *roots[1] = {rootStr.c_str()};
    char *jsonCatalog =
        KotonohaLauncherScanRootsJson(roots, 1, 1, saveRoot.string().c_str());
    require(jsonCatalog != nullptr,
            "KotonohaLauncherScanRootsJson returned null");
    const std::string jsonStr(jsonCatalog);
    KotonohaLauncherFreeString(jsonCatalog);
    require(jsonStr.find("\"DDLC\"") != std::string::npos &&
                jsonStr.find("\"SayaNoUta_PT-BR\"") != std::string::npos &&
                jsonStr.find("\"saveDirectory\"") != std::string::npos,
            "JSON catalog must include discovered titles and saveDirectory");

    char routeInfo[256] = {};
    require(KotonohaLauncherValidateRoute("", routeInfo, sizeof(routeInfo)) ==
                1,
            "Empty route path should be accepted (uses default bundled route)");

    std::cout << formatDiscoveredGamesReport(catalog, true);
    std::cout << "UnifiedLauncher & Per-Game Save Isolation tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
