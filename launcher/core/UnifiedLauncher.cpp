// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <KotonohaLauncher/UnifiedLauncher.hpp>
#include <KotonohaLauncher/LauncherBridge.h>

#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/debug/ImGuiTheme.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <system_error>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
extern "C" const char *KotonohaIOSDocumentsGamesPath();
#endif
#endif

namespace KotonohaLauncher {

namespace {
namespace fs = std::filesystem;
using Kotonoha::Filesystem::DetectedEngine;
using Kotonoha::Filesystem::EngineKind;
using Kotonoha::Filesystem::Filesystem;
using Kotonoha::Filesystem::findRequiredFilesPrefix;

std::string toLower(std::string_view input) {
  std::string out(input);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return out;
}

bool containsCaseInsensitive(std::string_view haystack,
                             std::string_view needle) {
  if (needle.empty())
    return true;
  const std::string h = toLower(haystack);
  const std::string n = toLower(needle);
  return h.find(n) != std::string::npos;
}

bool hasFileCaseInsensitive(const Filesystem &source,
                            std::string_view relativePath) {
  return findRequiredFilesPrefix(source, {std::string(relativePath)}, {""})
      .has_value();
}

std::string readFirstLineFromFile(const std::string &filePath) {
  if (filePath.empty())
    return {};
  std::string line;
  Kotonoha::Filesystem::InputFile input(filePath);
  if (input.is_open())
    std::getline(input, line);
  return line;
}

bool isDiscImageExtension(const fs::path &p) {
  const std::string ext = toLower(p.extension().string());
  return ext == ".iso" || ext == ".bin" || ext == ".img";
}

bool isVnxOverlayPatch(const Filesystem &source) {
  if (findRequiredFilesPrefix(source, {"vnx.rpx"}, {"vnx/game/", "game/", ""}))
    return true;
  if (findRequiredFilesPrefix(source, {"vnxsurvival.rpyc"},
                              {"game/", "vnx/game/", ""}) &&
      !findRequiredFilesPrefix(source, {"audio.rpa", "images.rpa"},
                               {"game/", ""})) {
    return true;
  }
  return false;
}

fs::path defaultBaseUserDataDir() {
  static const fs::path cachedBase = []() -> fs::path {
    const auto locs = Kotonoha::applicationStorageLocations();
    if (!locs.userData.empty())
      return locs.userData;
    std::error_code ec;
    return fs::temp_directory_path(ec) / "KotonohaUserData";
  }();
  return cachedBase;
}

struct CachedProbe {
  std::uint64_t fingerprint = 0;
  DetectedEngine engine;
  bool vnxPatch = false;
};

std::uint64_t sourceFingerprint(const std::string &path) {
  // Native discovery must not stat every file in a game's asset tree merely
  // to reuse a cached engine identity. Inspect the root and known metadata
  // directories; refresh periodically for changes deeper in the tree.
  const auto hour = std::chrono::duration_cast<std::chrono::hours>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
  if (path.find("://") != std::string::npos)
    return std::hash<std::string>{}(path) ^ std::uint64_t(hour);

  std::error_code error;
  const fs::path root(path);
  if (!fs::exists(root, error))
    return 0;
  std::uint64_t hash = 1469598103934665603ull;
  auto mix = [&hash](std::uint64_t value) {
    hash = (hash ^ value) * 1099511628211ull;
  };
  mix(static_cast<std::uint64_t>(hour));
  auto sample = [&](const fs::path &file) {
    mix(std::hash<std::string>{}(file.lexically_relative(root).string()));
    error.clear();
    const auto modified = fs::last_write_time(file, error);
    if (!error)
      mix(std::uint64_t(modified.time_since_epoch().count()));
    error.clear();
    if (fs::is_regular_file(file, error)) {
      error.clear();
      const auto size = fs::file_size(file, error);
      if (!error)
        mix(size);
    }
  };
  sample(root);
  if (fs::is_directory(root, error)) {
    for (const char *name :
         {"files", "game", "data", "archive", "Packs", "USRDIR", "kineticdata",
          "Data", "script", "scripts", "scenario", "game-assets"}) {
      sample(root / name);
    }
    for (const char *name :
         {"Seen.txt", "Gameexe.ini", "PARAM.PAK", "SCRIPT.PAK", "SYSSE.PAK",
          "data.xp3", "startup.tjs", "0.txt", "nscript.dat"}) {
      sample(root / name);
    }
  }
  return hash;
}

std::map<std::string, CachedProbe> &scanCache() {
  static std::map<std::string, CachedProbe> cache;
  static const bool loaded = [&] {
    try {
      const fs::path path =
          fs::path(defaultGameParamsFilePath()).parent_path() /
          "scan-cache-v1.txt";
      Kotonoha::Filesystem::InputFile input(path);
      if (!input.is_open())
        return true;
      std::string key;
      CachedProbe item;
      int kind = 0, implemented = 0, patch = 0;
      while (input >> std::quoted(key) >> item.fingerprint >> kind >>
             implemented >> patch >> std::quoted(item.engine.slug) >>
             std::quoted(item.engine.displayName) >>
             std::quoted(item.engine.dataPrefix) >>
             std::quoted(item.engine.evidence)) {
        item.engine.kind = EngineKind(kind);
        item.engine.implementedInKotonoha = implemented != 0;
        item.vnxPatch = patch != 0;
        cache[key] = item;
      }
    } catch (const std::exception &) {
      cache.clear();
    }
    return true;
  }();
  (void)loaded;
  return cache;
}

bool &scanCacheDirty() {
  static bool dirty = false;
  return dirty;
}

std::mutex &scanCacheMutex() {
  static std::mutex mutex;
  return mutex;
}

void persistScanCache(const std::map<std::string, CachedProbe> &cache) {
  try {
    std::ostringstream data;
    for (const auto &[path, item] : cache)
      data << std::quoted(path) << ' ' << item.fingerprint << ' '
           << int(item.engine.kind) << ' '
           << int(item.engine.implementedInKotonoha) << ' '
           << int(item.vnxPatch) << ' ' << std::quoted(item.engine.slug) << ' '
           << std::quoted(item.engine.displayName) << ' '
           << std::quoted(item.engine.dataPrefix) << ' '
           << std::quoted(item.engine.evidence) << '\n';
    const fs::path path = fs::path(defaultGameParamsFilePath()).parent_path() /
                          "scan-cache-v1.txt";
    Kotonoha::writeTextFileAtomically(path.string(), data.str());
  } catch (const std::exception &) {
    // An unwritable cache must not prevent game discovery.
  }
}

CachedProbe probeCached(const std::string &path) {
  std::lock_guard<std::mutex> lock(scanCacheMutex());
  const auto fingerprint = sourceFingerprint(path);
  auto &cache = scanCache();
  if (const auto found = cache.find(path);
      found != cache.end() && found->second.fingerprint == fingerprint)
    return found->second;
  CachedProbe result;
  result.fingerprint = fingerprint;
  try {
    auto mounted = Filesystem::mountForDetection(path);
    if (mounted) {
      result.vnxPatch = isVnxOverlayPatch(*mounted);
      result.engine = detectKotonohaEngine(*mounted, path);
    }
  } catch (const std::exception &) {
  }
  if (result.engine.kind != EngineKind::Unknown || result.vnxPatch) {
    cache[path] = result;
    scanCacheDirty() = true;
  } else {
    // Cache negative probes too; a changed root/hour still invalidates them.
    cache[path] = result;
    scanCacheDirty() = true;
  }
  return result;
}

// A library root may be a single game, a folder of games, or a folder of
// collections (e.g. /Games/Pack/[date] Title/). Descend only into folders that
// are not games themselves: a detected game's `data/`, `game/` and `DISC1/`
// subtrees must never be reported as separate titles. The depth and visit
// caps keep a mistyped root (e.g. an entire disk) from stalling the hub.
constexpr int kLibraryScanMaxDepth = 3;
constexpr std::size_t kLibraryScanMaxCandidates = 512;

struct LibraryScanState {
  std::vector<DiscoveredGameEntry> results;
  std::string vnxPatchPath;
  std::size_t visited = 0;
};

void collectGamesInLibraryFolder(const fs::path &folder, int depth,
                                 LibraryScanState &state) {
  std::error_code ec;
  std::vector<fs::path> candidates;
  for (const auto &dirEntry : fs::directory_iterator(folder, ec)) {
    const auto &p = dirEntry.path();
    const std::string name = p.filename().string();
    if (name.empty() || name[0] == '.')
      continue;
    if (dirEntry.is_directory(ec) ||
        (dirEntry.is_regular_file(ec) && isDiscImageExtension(p)))
      candidates.push_back(p);
  }
  std::sort(candidates.begin(), candidates.end());
  for (const auto &candidate : candidates) {
    if (state.visited++ >= kLibraryScanMaxCandidates)
      return;
    const auto probe = probeCached(candidate.string());
    if (probe.vnxPatch) {
      if (state.vnxPatchPath.empty())
        state.vnxPatchPath = candidate.string();
      continue;
    }
    if (probe.engine.kind != EngineKind::Unknown) {
      state.results.push_back(
          buildDiscoveredGameEntry(candidate.string(), probe.engine));
      // Internal folders of an installed game are not independent titles.
      continue;
    }
    if (depth < kLibraryScanMaxDepth && fs::is_directory(candidate, ec))
      collectGamesInLibraryFolder(candidate, depth + 1, state);
  }
}

void flushScanCache() {
  std::lock_guard<std::mutex> lock(scanCacheMutex());
  if (scanCacheDirty()) {
    persistScanCache(scanCache());
    scanCacheDirty() = false;
  }
}

} // namespace

std::string sanitizeGameProfileId(std::string_view titleOrPath) {
  fs::path p(titleOrPath);
  std::string stem = p.filename().string();
  if (stem.empty())
    stem = p.parent_path().filename().string();
  if (isDiscImageExtension(p))
    stem = p.stem().string();

  std::string clean;
  clean.reserve(stem.size());
  for (unsigned char ch : stem) {
    if (std::isalnum(ch)) {
      clean.push_back(static_cast<char>(std::tolower(ch)));
    } else if (ch == '-' || ch == '_' || ch == '.' || ch == ' ') {
      if (!clean.empty() && clean.back() != '-' && clean.back() != '_')
        clean.push_back(ch == ' ' ? '_' : static_cast<char>(ch));
    }
  }
  while (!clean.empty() &&
         (clean.back() == '-' || clean.back() == '_' || clean.back() == '.')) {
    clean.pop_back();
  }
  if (clean.empty() || clean == "." || clean == "..")
    return "game";
  return clean;
}

std::string resolveIsolatedUserDataRoot(const DiscoveredGameEntry &entry,
                                        bool splitSavesByGame,
                                        std::string_view customSaveRoot) {
  fs::path baseRoot = customSaveRoot.empty() ? defaultBaseUserDataDir()
                                             : fs::path(customSaveRoot);
  if (!splitSavesByGame)
    return baseRoot.lexically_normal().string();

  const std::string profile = entry.saveProfileId.empty()
                                  ? sanitizeGameProfileId(entry.title)
                                  : entry.saveProfileId;
  return (baseRoot / "Kotonoha-Profiles" / profile).lexically_normal().string();
}

std::string resolveGameSaveDirectory(const DiscoveredGameEntry &entry,
                                     bool splitSavesByGame,
                                     std::string_view customSaveRoot) {
  const fs::path userRoot =
      resolveIsolatedUserDataRoot(entry, splitSavesByGame, customSaveRoot);
  return (userRoot / "save").lexically_normal().string();
}

void applyPerGameSaveIsolation(const DiscoveredGameEntry &entry,
                               Kotonoha::EngineOptions &options,
                               bool splitSavesByGame,
                               std::string_view customSaveRoot) {
  const std::string isolatedRoot =
      resolveIsolatedUserDataRoot(entry, splitSavesByGame, customSaveRoot);
  if (isolatedRoot.empty())
    return;

  std::error_code ec;
  const fs::path saveDir =
      resolveGameSaveDirectory(entry, splitSavesByGame, customSaveRoot);
  fs::create_directories(saveDir, ec);

  if (auto *env = SDL_GetEnvironment()) {
    SDL_SetEnvironmentVariable(env, "KOTONOHA_USER_DATA_DIR",
                               isolatedRoot.c_str(), true);
  }
#if !defined(_WIN32)
  setenv("KOTONOHA_USER_DATA_DIR", isolatedRoot.c_str(), 1);
#endif

  if (splitSavesByGame && options.persistSettings) {
    const fs::path docDir = saveDir.parent_path();
    fs::create_directories(docDir, ec);
    options.settingsPath = (docDir / "settings.json").string();
  }
}

EngineLibraryRegistry &EngineLibraryRegistry::instance() {
  static EngineLibraryRegistry registry;
  return registry;
}

EngineLibraryRegistry::EngineLibraryRegistry() {
  // Modular CMake engine libraries for every Kotonoha port:
  registerLibrary({EngineKind::LucaSystem,
                   "luca-system",
                   "LucaSystem",
                   "luca_system_formats",
                   "LucaSystem",
                   "--luca-system",
                   "me.hirameki.kotonoha.lucasystem",
                   {}});
  registerLibrary({EngineKind::RealLive,
                   "reallive",
                   "RealLive / rlBabel",
                   "reallive_port",
                   "RealLive",
                   "--reallive",
                   "me.hirameki.kotonoha.reallive",
                   {}});
  registerLibrary({EngineKind::Kirikiri,
                   "kirikiri",
                   "KiriKiri2 / KiriKiriZ",
                   "kirikiri_formats",
                   "Kirikiri",
                   "--kirikiri",
                   "me.hirameki.kotonoha.kirikiri",
                   {}});
  registerLibrary({EngineKind::RenPy,
                   "doki-doki-literature-club",
                   "Ren'Py",
                   "doki_doki_literature_club",
                   "DokiDoki",
                   "--ddlc",
                   "me.hirameki.kotonoha.dokidokiliteratureclub",
                   {}});
  registerLibrary({EngineKind::RenPy,
                   "renpy",
                   "Ren'Py (compiled games)",
                   "doki_doki_literature_club",
                   "DokiDoki",
                   "--ddlc",
                   "me.hirameki.kotonoha.renpy",
                   {}});
  registerLibrary({EngineKind::RenPy,
                   "katawa-shoujo",
                   "Katawa Shoujo (Ren'Py)",
                   "doki_doki_literature_club",
                   "KatawaShoujo",
                   "--katawa",
                   "me.hirameki.kotonoha.katawashoujo",
                   {}});
  registerLibrary({EngineKind::SummerRadish,
                   "meo",
                   "Miss Each Other",
                   "summer_radish_vacation",
                   "MEO",
                   "--meo",
                   "me.hirameki.kotonoha.meo",
                   {}});
  registerLibrary({EngineKind::N2System,
                   "n2system",
                   "N2System / Nitroplus",
                   "n2system_formats",
                   "N2SystemGame",
                   "--n2system",
                   "me.hirameki.kotonoha.n2system",
                   {}});
  registerLibrary({EngineKind::ONScripter,
                   "ponscripter",
                   "PONScripter / ONScripter",
                   "ponscripter_port",
                   "PonscripterGame",
                   "--ponscripter",
                   "me.hirameki.kotonoha.ponscripter",
                   {}});
  registerLibrary({EngineKind::Artemis,
                   "artemis",
                   "Artemis Engine",
                   "artemis_port",
                   "ArtemisGame",
                   "--artemis",
                   "me.hirameki.kotonoha.artemis",
                   {}});
  registerLibrary({EngineKind::Amagami,
                   "amagami",
                   "Amagami (PS2 SCF)",
                   "amagami_port",
                   "Amagami",
                   "--amagami",
                   "me.hirameki.kotonoha.amagami",
                   {}});
  registerLibrary({EngineKind::SchoolDays,
                   "school-days",
                   "Overflow GPK / FILMEngine (School Days HQ)",
                   "school_days",
                   "SchoolDays",
                   "--school-days",
                   "me.hirameki.kotonoha.schooldays",
                   {}});
  registerLibrary({EngineKind::PureMail,
                   "pmail",
                   "Pure Mail (OverFlow ADV)",
                   "PmailPort",
                   "Pmail",
                   "--pmail",
                   "me.hirameki.kotonoha.pmail",
                   {}});
  registerLibrary({EngineKind::SnowRadish,
                   "snow-radish-vacation",
                   "Snow Radish ADV (of_adv3)",
                   "snow_radish_vacation",
                   "SnowRadish",
                   "--snow",
                   "me.hirameki.kotonoha.snowradishvacation",
                   {}});
  registerLibrary({EngineKind::SummerRadish,
                   "summer-radish-vacation",
                   "Summer Radish / Overflow CMP",
                   "summer_radish_vacation",
                   "SummerRadish",
                   "--summer",
                   "me.hirameki.kotonoha.summerradishvacation",
                   {}});
  registerLibrary({EngineKind::ZeroNoTsukaima,
                   "zero-no-tsukaima",
                   "Zero no Tsukaima (PS2 Squirrel/KAG)",
                   "zero_no_tsukaima",
                   "ZeroNoTsukaima",
                   "--zero",
                   "me.hirameki.kotonoha.zeronotsukaima",
                   {}});
  registerLinkedEngineFactories(*this);
}

void EngineLibraryRegistry::registerLibrary(
    EngineLibraryDescriptor descriptor) {
  for (auto &existing : descriptors_) {
    if (existing.kind == descriptor.kind && existing.slug == descriptor.slug) {
      existing = std::move(descriptor);
      return;
    }
  }
  descriptors_.push_back(std::move(descriptor));
}

const EngineLibraryDescriptor *
EngineLibraryRegistry::find(EngineKind kind) const {
  for (const auto &desc : descriptors_) {
    if (desc.kind == kind)
      return &desc;
  }
  return nullptr;
}

const EngineLibraryDescriptor *
EngineLibraryRegistry::findBySlug(std::string_view slug) const {
  for (const auto &desc : descriptors_) {
    if (desc.slug == slug)
      return &desc;
  }
  return nullptr;
}

bool EngineLibraryRegistry::canInstantiateInProcess(EngineKind kind) const {
  const auto *desc = find(kind);
  return desc && static_cast<bool>(desc->factory);
}

MountedEngineInstance
EngineLibraryRegistry::instantiate(const DiscoveredGameEntry &entry,
                                   Kotonoha::EngineOptions &options) const {
  const auto *desc = findBySlug(entry.engine.slug);
  if (!desc || desc->kind != entry.engine.kind)
    desc = find(entry.engine.kind);
  if (desc && desc->factory)
    return desc->factory(entry, options);
  return {};
}

DetectedEngine detectKotonohaEngine(const Filesystem &source,
                                    std::string_view hintPath) {
  // 1. Check Overflow gEngine / MEO (Personal/MISSEACHOTHER, MISS EACH
  // OTHER.msi,
  //    MEO.exe, gEngine.gesl, or FILMEngine.gem).
  if (hasFileCaseInsensitive(source, "MISS EACH OTHER.msi") ||
      hasFileCaseInsensitive(source, "gEngine.gesl") ||
      hasFileCaseInsensitive(source, "MEO.exe") ||
      findRequiredFilesPrefix(
          source, {"SCRIPT.CMP"},
          {"Personal/MISSEACHOTHER/", "Personal/MISSEACHOTHER/DATA/",
           "Personal/MISSEACHOTHER/PACK/", "Personal/MISSEACHOTHER/PACKS/"})) {
    return {EngineKind::SummerRadish,
            "meo",
            "Overflow gEngine / FILMEngine (MEO CMP)",
            "Personal/MISSEACHOTHER/",
            "MISS EACH OTHER (CMP / FILMEngine)",
            true};
  }

  // Katawa Shoujo is Ren'Py-based but must retain its own application
  // identity, save directory and standalone executable.
  if (const auto prefix = findRequiredFilesPrefix(
          source, {"script-a1-monday.rpyc"}, {"game/", ""})) {
    return {EngineKind::RenPy,
            "katawa-shoujo",
            "Katawa Shoujo (Ren'Py)",
            *prefix,
            *prefix + "script-a1-monday.rpyc",
            true};
  }

  // Preserve DDLC's game identity and VNX overlay pairing. Other Ren'Py
  // packages use the neutral renpy identity returned by detectEngine().
  if (const auto prefix = findRequiredFilesPrefix(
          source, {"scripts.rpa", "images.rpa"}, {"game/", ""})) {
    return {EngineKind::RenPy,
            "doki-doki-literature-club",
            "Doki Doki Literature Club! (Ren'Py)",
            *prefix,
            *prefix + "scripts.rpa",
            true};
  }

  // 2. Check Summer Radish Vacation 1 & 2, Lost M, and Imouto de Ikou!
  //    (SCRIPT.CMP + GRAPHIC.CMP / GRAPHIC000.CMP / BGDATA.CMP in
  //    DISC1/PACK/, PACK/, DATA/, or root; or INSTALL.cmp + Info.DISC).
  if (findRequiredFilesPrefix(source, {"Lost-M.conf"}, {"", "DISC1/"}) ||
      findRequiredFilesPrefix(source, {"SCRIPT.CMP", "GRAPHIC000.CMP"},
                              {"", "PACK/", "DISC1/PACK/"})) {
    const auto cmpPrefix =
        findRequiredFilesPrefix(source, {"SCRIPT.CMP"},
                                {"", "PACK/", "DISC1/PACK/", "DATA/"})
            .value_or("DISC1/PACK/");
    return {EngineKind::SummerRadish,
            "summer-radish-vacation",
            "Overflow CMP (Lost M)",
            cmpPrefix,
            cmpPrefix + "SCRIPT.CMP + GRAPHIC000.CMP",
            true};
  }
  if (findRequiredFilesPrefix(source, {"SCRIPT.CMP", "BGDATA.CMP"},
                              {"", "PACK/", "DISC1/PACK/", "DATA/"})) {
    const auto cmpPrefix =
        findRequiredFilesPrefix(source, {"SCRIPT.CMP", "BGDATA.CMP"},
                                {"", "PACK/", "DISC1/PACK/", "DATA/"})
            .value_or("DISC1/PACK/");
    return {EngineKind::SummerRadish,
            "summer-radish-vacation",
            "Overflow CMP (Imouto de Ikou!)",
            cmpPrefix,
            cmpPrefix + "SCRIPT.CMP + BGDATA.CMP",
            true};
  }
  if (const auto cmpPrefix = findRequiredFilesPrefix(
          source, {"SCRIPT.CMP", "GRAPHIC.CMP"},
          {"", "DATA/", "PACK/", "DISC1/PACK/", "DISC1/DATA/"})) {
    return {EngineKind::SummerRadish,  "summer-radish-vacation",
            "Summer Radish CMP",       *cmpPrefix,
            *cmpPrefix + "SCRIPT.CMP", true};
  }
  if (hasFileCaseInsensitive(source, "INSTALL.cmp") &&
      hasFileCaseInsensitive(source, "Info.DISC")) {
    return {EngineKind::SummerRadish,  "summer-radish-vacation",
            "Summer Radish CMP",       "DISC1/",
            "INSTALL.cmp + Info.DISC", true};
  }

  // 2b. Check Large PonPon (MAINSYS.INI + PACK/script.dat + PACK/GRAPHIC.DAT)
  if (hasFileCaseInsensitive(source, "MAINSYS.INI") &&
      findRequiredFilesPrefix(source, {"SCRIPT.DAT", "GRAPHIC.DAT"},
                              {"", "PACK/", "pack/"})) {
    return {EngineKind::PureMail,
            "pmail",
            "Overflow GC Engine (Large PonPon)",
            "PACK/",
            "MAINSYS.INI + PACK/SCRIPT.DAT",
            true};
  }

  // 3. Check Snow Radish Vacation before generic PureMail rule:
  //    SnowRadish uses either SNOWRADISH.EXE / snowrad.id with
  //    data/pack/SCRIPT.DAT + GRAPHIC.DAT, or DATA/SCRIPT.PAK + GRAPHIC.PAK.
  if (hasFileCaseInsensitive(source, "SNOWRADISH.EXE") ||
      hasFileCaseInsensitive(source, "snowrad.id")) {
    const auto datPrefix =
        findRequiredFilesPrefix(source, {"SCRIPT.DAT", "GRAPHIC.DAT"},
                                {"", "pack/", "data/pack/", "DATA/"})
            .value_or("data/pack/");
    return {EngineKind::SnowRadish,
            "snow-radish-vacation",
            "Snow Radish ADV (of_adv3)",
            datPrefix,
            "SNOWRADISH.EXE + " + datPrefix + "SCRIPT.DAT",
            true};
  }
  if (!findRequiredFilesPrefix(source, {"PARAM.PAK", "SYSSE.PAK"},
                               {"", "files/"})) {
    if (const auto snowPrefix = findRequiredFilesPrefix(
            source, {"SCRIPT.PAK", "GRAPHIC.PAK"}, {"DATA/", ""})) {
      return {EngineKind::SnowRadish,      "snow-radish-vacation",
              "Snow Radish ADV (of_adv3)", *snowPrefix,
              *snowPrefix + "SCRIPT.PAK",  true};
    }
  }

  // 3. Check PS2 discs (Amagami SLPS_259.18 & Zero no Tsukaima SLPS_257.09 /
  //    SLPS_258.30 / SLPS_258.98) before generic rules.
  if (hasFileCaseInsensitive(source, "SLPS_259.18") ||
      (hasFileCaseInsensitive(source, "AMAGAMI.IMG") &&
       hasFileCaseInsensitive(source, "SCENARIO.ARC"))) {
    return {EngineKind::Amagami,
            "amagami",
            "Amagami (PS2 SCF / SLPS_259.18)",
            "",
            "SLPS_259.18 + SCENARIO.ARC",
            true};
  }
  if (hasFileCaseInsensitive(source, "SLPS_257.09")) {
    return {EngineKind::ZeroNoTsukaima,
            "zero-no-tsukaima",
            "Zero no Tsukaima: Concerto (PS2 SLPS_257.09)",
            "",
            "SLPS_257.09 + SCENE.BIN",
            true};
  }
  if (hasFileCaseInsensitive(source, "SLPS_258.30")) {
    return {EngineKind::ZeroNoTsukaima,
            "zero-no-tsukaima",
            "Zero no Tsukaima: Nocturne (PS2 SLPS_258.30)",
            "",
            "SLPS_258.30 + SYSTEM.BIN",
            true};
  }
  if (hasFileCaseInsensitive(source, "SLPS_258.98")) {
    return {EngineKind::ZeroNoTsukaima,
            "zero-no-tsukaima",
            "Zero no Tsukaima: Symphony (PS2 SLPS_258.98)",
            "",
            "SLPS_258.98 + SYSTEM.BIN",
            true};
  }
  if (hasFileCaseInsensitive(source, "SCENE.BIN") &&
      hasFileCaseInsensitive(source, "SCENE_ID.BIN")) {
    return {EngineKind::ZeroNoTsukaima,
            "zero-no-tsukaima",
            "Zero no Tsukaima (PS2 Squirrel/KAG)",
            "",
            "SCENE.BIN + SCENE_ID.BIN",
            true};
  }

  // 4. Delegate to core Kotonoha::Filesystem::detectEngine for all supported
  //    visual novel engine signatures.
  auto detected = Kotonoha::Filesystem::detectEngine(source);

  // 5. Refine ONScripter / PONScripter (implemented via ponscripter_port!).
  if (detected.kind == EngineKind::ONScripter) {
    detected.slug = "ponscripter";
    detected.displayName = "PONScripter / ONScripter";
    detected.implementedInKotonoha = true;
    if (hasFileCaseInsensitive(source, "0.utf") &&
        hasFileCaseInsensitive(source, "arc.nsa")) {
      detected.evidence = "0.utf + arc.nsa";
    }
    return detected;
  }

  // 6. Refine RealLive when rlBabel.dll is present (AIR_PT-BR, Clannad_PT-BR).
  if (detected.kind == EngineKind::RealLive) {
    if (hasFileCaseInsensitive(source, "rlBabel.dll")) {
      detected.displayName = "RealLive + rlBabel (Western/PT-BR)";
      detected.evidence += " + rlBabel.dll";
    }
    return detected;
  }

  // 7. Refine Ren'Py when VNX PT-BR patch structure is detected.
  if (detected.kind == EngineKind::Unknown &&
      findRequiredFilesPrefix(source, {"vnx.rpx"},
                              {"vnx/game/", "game/", ""})) {
    return {EngineKind::RenPy,          "doki-doki-literature-club",
            "Ren'Py (VNX PT-BR Patch)", "vnx/game/",
            "vnx/game/vnx.rpx",         true};
  }

  (void)hintPath;
  return detected;
}

DetectedEngine detectKotonohaEngineFromPath(const std::string &path) {
  if (path.empty())
    return {};
  return probeCached(path).engine;
}

DiscoveredGameEntry buildDiscoveredGameEntry(const std::string &path,
                                             const DetectedEngine &detected) {
  DiscoveredGameEntry entry;
  const fs::path fsPath(path);
  entry.title = fsPath.filename().string();
  if (entry.title.empty())
    entry.title = fsPath.parent_path().filename().string();
  entry.saveProfileId = sanitizeGameProfileId(entry.title);
  entry.path = path;
  entry.engine = detected;

  const auto &registry = EngineLibraryRegistry::instance();
  const auto *desc = registry.findBySlug(detected.slug);
  if (!desc || desc->kind != detected.kind)
    desc = registry.find(detected.kind);
  if (desc) {
    entry.libraryTarget = desc->libraryTarget;
    entry.targetBinary = desc->targetBinary;
    entry.cliOption = desc->cliOption;
    entry.packageId = desc->packageId;
  } else {
    entry.libraryTarget = detected.slug.empty() ? "unknown" : detected.slug;
    entry.packageId = "org.kotonoha." + entry.libraryTarget;
  }
  return entry;
}

std::vector<DiscoveredGameEntry>
scanGameLibrary(const std::string &libraryOrGamePath) {
  std::vector<DiscoveredGameEntry> results;
  if (libraryOrGamePath.empty())
    return results;

  std::error_code ec;
  const fs::path rootPath(libraryOrGamePath);
  if (!fs::exists(rootPath, ec))
    return results;

  // First check if `libraryOrGamePath` itself is a single game folder or disc.
  {
    const auto direct = detectKotonohaEngineFromPath(rootPath.string());
    if (direct.kind != EngineKind::Unknown) {
      results.push_back(buildDiscoveredGameEntry(rootPath.string(), direct));
      flushScanCache();
      return results;
    }
  }

  if (!fs::is_directory(rootPath, ec)) {
    flushScanCache();
    return results;
  }

  LibraryScanState state;
  collectGamesInLibraryFolder(rootPath, 0, state);
  results = std::move(state.results);
  const std::string &vnxPatchPath = state.vnxPatchPath;

  // Pair companion VNX PT-BR patch with base DDLC automatically.
  if (!vnxPatchPath.empty()) {
    for (auto &item : results) {
      if (item.engine.kind == EngineKind::RenPy &&
          item.engine.slug == "doki-doki-literature-club" &&
          item.path != vnxPatchPath && item.overlayPath.empty()) {
        item.overlayPath = vnxPatchPath;
      }
    }
  }

  // Persist once per directory scan, not after every candidate. On libraries
  // with many titles, repeated atomic full-cache rewrites dominated IO.
  flushScanCache();
  return results;
}

std::vector<DiscoveredGameEntry>
scanGameLibraryRoots(const std::vector<std::string> &roots) {
  std::vector<DiscoveredGameEntry> merged;
  std::vector<std::string> seenKeys;
  std::string discoveredVnxPatch;

  auto canonicalKey = [](const std::string &raw) -> std::string {
    if (raw.find("://") != std::string::npos)
      return raw;
    std::error_code ec;
    const auto canon = fs::weakly_canonical(fs::path(raw), ec);
    return ec ? fs::path(raw).lexically_normal().string() : canon.string();
  };

  for (const auto &root : roots) {
    if (root.empty())
      continue;
    auto found = scanGameLibrary(root);
    for (auto &entry : found) {
      if (!entry.overlayPath.empty() && discoveredVnxPatch.empty())
        discoveredVnxPatch = entry.overlayPath;
      const std::string key = canonicalKey(entry.path);
      if (std::find(seenKeys.begin(), seenKeys.end(), key) != seenKeys.end())
        continue;
      seenKeys.push_back(key);
      merged.push_back(std::move(entry));
    }
  }

  // Cross-root VNX patch pairing (e.g. DDLC in Documents/games, patch in
  // Documents/ or bookmarked folder).
  if (!discoveredVnxPatch.empty()) {
    for (auto &item : merged) {
      if (item.engine.kind == EngineKind::RenPy &&
          item.engine.slug == "doki-doki-literature-club" &&
          item.path != discoveredVnxPatch && item.overlayPath.empty()) {
        item.overlayPath = discoveredVnxPatch;
      }
    }
  }

  // Merge persisted per-game launch parameters.
  const auto savedParams = loadGameParamsMap();
  for (auto &item : merged) {
    auto it = savedParams.find(item.saveProfileId);
    if (it != savedParams.end())
      item.params = it->second;
  }

  std::sort(merged.begin(), merged.end(),
            [](const DiscoveredGameEntry &a, const DiscoveredGameEntry &b) {
              return toLower(a.title) < toLower(b.title);
            });
  return merged;
}

std::string defaultGameParamsFilePath() {
  try {
    const std::string settings = Kotonoha::EngineSettings::defaultPath();
    if (!settings.empty())
      return (fs::path(settings).parent_path() / "game-params.txt").string();
  } catch (const std::exception &) {
  }
  return (defaultBaseUserDataDir() / "Kotonoha" /
          "me.hirameki.kotonoha" / "game-params.txt")
      .string();
}

std::map<std::string, GameLaunchParameters>
loadGameParamsMap(const std::string &filePath) {
  std::map<std::string, GameLaunchParameters> result;
  const std::string path =
      filePath.empty() ? defaultGameParamsFilePath() : filePath;
  if (path.empty())
    return result;
  Kotonoha::Filesystem::InputFile input(path);
  if (!input.is_open())
    return result;
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream ss(line);
    std::string profileId;
    GameLaunchParameters p;
    if (ss >> std::quoted(profileId) >> std::quoted(p.assetOverlayPath) >>
        std::quoted(p.translationPath) >> std::quoted(p.subtitleStylePath) >>
        std::quoted(p.customDocumentsDir) >> std::quoted(p.ktrfPath) >>
        std::quoted(p.scriptsDir) >> std::quoted(p.startScenario)) {
      // Optional trailing graphics overrides (backward-compatible with older
      // 8-field game-params.txt files).
      if (ss >> std::quoted(p.rendererBackend)) {
        if (ss >> std::quoted(p.upscalerBackend)) {
          if (ss >> std::quoted(p.upscalerPreset)) {
            ss >> std::quoted(p.upscalerFactor);
          }
        }
      }
      if (!profileId.empty())
        result[profileId] = std::move(p);
    }
  }
  return result;
}

bool saveGameParamsMap(
    const std::map<std::string, GameLaunchParameters> &params,
    const std::string &filePath) {
  const std::string path =
      filePath.empty() ? defaultGameParamsFilePath() : filePath;
  if (path.empty())
    return false;
  try {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ostringstream out;
    for (const auto &[profileId, p] : params) {
      if (profileId.empty())
        continue;
      out << std::quoted(profileId) << ' ' << std::quoted(p.assetOverlayPath)
          << ' ' << std::quoted(p.translationPath) << ' '
          << std::quoted(p.subtitleStylePath) << ' '
          << std::quoted(p.customDocumentsDir) << ' ' << std::quoted(p.ktrfPath)
          << ' ' << std::quoted(p.scriptsDir) << ' '
          << std::quoted(p.startScenario) << ' '
          << std::quoted(p.rendererBackend) << ' '
          << std::quoted(p.upscalerBackend) << ' '
          << std::quoted(p.upscalerPreset) << ' '
          << std::quoted(p.upscalerFactor) << '\n';
    }
    Kotonoha::writeTextFileAtomically(path, out.str());
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

GameLaunchParameters loadGameParamsForProfile(const std::string &saveProfileId,
                                              const std::string &filePath) {
  if (saveProfileId.empty())
    return {};
  const auto all = loadGameParamsMap(filePath);
  const auto it = all.find(saveProfileId);
  return it != all.end() ? it->second : GameLaunchParameters{};
}

bool saveGameParamsForProfile(const std::string &saveProfileId,
                              const GameLaunchParameters &params,
                              const std::string &filePath) {
  if (saveProfileId.empty())
    return false;
  auto all = loadGameParamsMap(filePath);
  all[saveProfileId] = params;
  return saveGameParamsMap(all, filePath);
}

std::string
formatDiscoveredGamesReport(const std::vector<DiscoveredGameEntry> &entries,
                            bool splitSavesByGame) {
  std::ostringstream out;
  out << "Kotonoha Unified Engine Discovery (" << entries.size()
      << " game(s) detected, per-game save isolation: "
      << (splitSavesByGame ? "ON" : "OFF") << "):\n";
  out << std::left << std::setw(24) << "TITLE" << std::setw(34)
      << "ENGINE FAMILY" << std::setw(22) << "CMAKE LIBRARY" << std::setw(22)
      << "SAVE PROFILE ID"
      << "EVIDENCE\n";
  out << std::string(124, '-') << "\n";
  for (const auto &e : entries) {
    out << std::left << std::setw(24) << e.title.substr(0, 23) << std::setw(34)
        << e.engine.displayName.substr(0, 33) << std::setw(22)
        << e.libraryTarget.substr(0, 21) << std::setw(22)
        << e.saveProfileId.substr(0, 21) << e.engine.evidence;
    if (!e.overlayPath.empty()) {
      out << " [overlay: " << fs::path(e.overlayPath).filename().string()
          << "]";
    }
    out << "\n";
  }
  return out.str();
}

UnifiedLauncher::UnifiedLauncher(std::string settingsPath,
                                 std::string initialLibraryPath,
                                 Kotonoha::EngineSettings defaults)
    : Launcher("Kotonoha", settingsPath,
               std::move(defaults)) {
  if (!settingsPath.empty()) {
    const fs::path parent = fs::path(settingsPath).parent_path();
    rememberFilePath = (parent / "library.txt").string();
    rememberSaveSplitPath = (parent / "save-isolation.txt").string();
    rememberSaveRootPath = (parent / "save-root.txt").string();
    rememberGameParamsPath = (parent / "game-params.txt").string();
  }
  const std::string savedSplit = readFirstLineFromFile(rememberSaveSplitPath);
  if (savedSplit == "0" || savedSplit == "false")
    isolateSavesPerGame = false;
  customSaveRootPath = readFirstLineFromFile(rememberSaveRootPath);
  loadSavedGameParams();

#if defined(__APPLE__) && TARGET_OS_IPHONE
  // On iOS, the hub scans one stable Files-visible sandbox directory.
  // Saved desktop library paths must never override Documents/games.
  (void)initialLibraryPath;
  setLibraryPath(KotonohaIOSDocumentsGamesPath());
#else
  std::string startPath = std::move(initialLibraryPath);
  if (startPath.empty())
    startPath = readFirstLineFromFile(rememberFilePath);
  if (startPath.empty()) {
#if defined(__ANDROID__)
    try {
      const auto docs = Kotonoha::applicationStorageLocations().userData;
      if (!docs.empty())
        startPath = (docs / "games").string();
    } catch (const std::exception &) {
    }
#endif
    if (startPath.empty()) {
      // Default game library: the multi-game pack folder users keep on the
      // server volume, with the external disk as a fallback. Users can still
      // pick any other folder with "Library / Folder..." below.
      for (const char *candidate : {"/Volumes/Server/data2/Games",
                                    "/Volumes/Nero 2TB/KotonohaAssets"}) {
        std::error_code ec;
        if (fs::is_directory(candidate, ec)) {
          startPath = candidate;
          break;
        }
      }
    }
  }
  setLibraryPath(std::move(startPath));
#endif
}

void UnifiedLauncher::setLibraryPath(std::string path) {
#if defined(__APPLE__) && TARGET_OS_IPHONE
  (void)path;
  currentLibraryPath = KotonohaIOSDocumentsGamesPath();
#else
  currentLibraryPath = std::move(path);
#endif
  rescan();
}

void UnifiedLauncher::rescan() {
  for (const auto &entry : catalog) {
    if (!entry.saveProfileId.empty())
      savedGameParams[entry.saveProfileId] = entry.params;
  }
  catalog = scanGameLibrary(currentLibraryPath);
  for (auto &entry : catalog) {
    auto it = savedGameParams.find(entry.saveProfileId);
    if (it != savedGameParams.end())
      entry.params = it->second;
  }
  if (selected < 0 || selected >= static_cast<int>(catalog.size()))
    selected = 0;
}

void UnifiedLauncher::loadSavedGameParams() {
  if (rememberGameParamsPath.empty())
    return;
  savedGameParams = loadGameParamsMap(rememberGameParamsPath);
}

void UnifiedLauncher::saveGameParams() const {
  if (rememberGameParamsPath.empty())
    return;
  std::map<std::string, GameLaunchParameters> merged = savedGameParams;
  for (const auto &entry : catalog) {
    if (!entry.saveProfileId.empty())
      merged[entry.saveProfileId] = entry.params;
  }
  saveGameParamsMap(merged, rememberGameParamsPath);
}

void UnifiedLauncher::setSelectedIndex(int index) {
  if (index >= 0 && index < static_cast<int>(catalog.size()))
    selected = index;
}

bool UnifiedLauncher::hasValidSelection() const {
  return selected >= 0 && selected < static_cast<int>(catalog.size());
}

DiscoveredGameEntry UnifiedLauncher::selectedEntry() const {
  if (hasValidSelection())
    return catalog[static_cast<std::size_t>(selected)];
  return {};
}

std::string UnifiedLauncher::selectedSaveDirectory() const {
  if (!hasValidSelection())
    return {};
  return resolveGameSaveDirectory(selectedEntry(), isolateSavesPerGame,
                                  customSaveRootPath);
}

void UnifiedLauncher::drawGameOptions() {
  ImGui::TextUnformatted("Game Library Folder or Individual Game (Folder / ISO "
                         "/ PS2 BIN):");

#if defined(__APPLE__) && TARGET_OS_IPHONE
  // Files.app exposes Documents/games. The iOS hub must not browse outside
  // its sandbox or persist an arbitrary desktop path.
  ImGui::TextWrapped("%s", currentLibraryPath.c_str());
#else
  if (ImGui::InputText("##libraryPath", &currentLibraryPath,
                       ImGuiInputTextFlags_EnterReturnsTrue)) {
    rescan();
  }
  ImGui::SameLine();
  if (ImGui::Button("Library / Folder...")) {
    browseFolder(currentLibraryPath,
                 [this](const std::string &chosen) { setLibraryPath(chosen); });
  }
  ImGui::SameLine();
  if (ImGui::Button("Disc Image...")) {
    browseFile({{"Disc image (.iso, .bin)", "iso;bin;img"}, {"All files", "*"}},
               currentLibraryPath,
               [this](const std::string &chosen) { setLibraryPath(chosen); });
  }
#endif
  ImGui::SameLine();
  if (ImGui::Button("Rescan")) {
    rescan();
  }

  ImGui::PushStyleColor(ImGuiCol_Text, !catalog.empty()
                                           ? Kotonoha::ImGuiTheme::success()
                                           : Kotonoha::ImGuiTheme::warning());
  if (!catalog.empty()) {
    ImGui::Text("Detected %zu game(s) across Kotonoha engine libraries.",
                catalog.size());
  } else if (currentLibraryPath.empty()) {
    ImGui::TextUnformatted(
        "Select a multi-game library folder or an individual game folder/disc "
        "image above.");
  } else {
    ImGui::TextUnformatted(
        "No recognized visual novel archives found at the specified path.");
  }
  ImGui::PopStyleColor();

  ImGui::Spacing();
  ImGui::Checkbox("Split Save & Config Folder by Game Title (Prevent "
                  "Same-Engine Conflicts)",
                  &isolateSavesPerGame);
  ImGui::InputTextWithHint("##customSaveRoot",
                           "Optional custom save root directory (leave empty "
                           "for platform default)...",
                           &customSaveRootPath);

  ImGui::Spacing();
  ImGui::InputTextWithHint(
      "##filter", "Filter by title, engine family, or CMake library...",
      &filterText);
  ImGui::Spacing();

  if (ImGui::BeginTable("UnifiedCatalog", 5,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                        ImVec2(0.0f, 220.0f))) {
    ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Engine Family",
                            ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Engine Library", ImGuiTableColumnFlags_WidthFixed,
                            145.0f);
    ImGui::TableSetupColumn("Save Profile", ImGuiTableColumnFlags_WidthFixed,
                            130.0f);
    ImGui::TableSetupColumn("Signature / Patch",
                            ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    for (int i = 0; i < static_cast<int>(catalog.size()); ++i) {
      const auto &item = catalog[static_cast<std::size_t>(i)];
      if (!filterText.empty() &&
          !containsCaseInsensitive(item.title, filterText) &&
          !containsCaseInsensitive(item.engine.displayName, filterText) &&
          !containsCaseInsensitive(item.libraryTarget, filterText) &&
          !containsCaseInsensitive(item.saveProfileId, filterText)) {
        continue;
      }

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      const bool isSelected = (selected == i);
      ImGui::PushID(i);
      if (ImGui::Selectable(item.title.c_str(), isSelected,
                            ImGuiSelectableFlags_SpanAllColumns)) {
        selected = i;
      }
      ImGui::PopID();

      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(item.engine.displayName.c_str());

      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(item.libraryTarget.c_str());

      ImGui::TableSetColumnIndex(3);
      ImGui::TextUnformatted(isolateSavesPerGame ? item.saveProfileId.c_str()
                                                 : "(shared engine)");

      ImGui::TableSetColumnIndex(4);
      if (!item.overlayPath.empty()) {
        ImGui::Text("%s (+ %s)", item.engine.evidence.c_str(),
                    fs::path(item.overlayPath).filename().string().c_str());
      } else {
        ImGui::TextUnformatted(item.engine.evidence.c_str());
      }
    }
    ImGui::EndTable();
  }

  if (hasValidSelection()) {
    auto &current = catalog[static_cast<std::size_t>(selected)];
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Selected: %s  |  Engine: %s  |  Library: %s",
                current.title.c_str(), current.engine.displayName.c_str(),
                current.libraryTarget.c_str());
    ImGui::TextWrapped("Isolated Save Directory: %s",
                       selectedSaveDirectory().c_str());
    ImGui::TextWrapped("Source Path: %s", current.path.c_str());
    if (!current.overlayPath.empty()) {
      ImGui::TextWrapped("Auto-Paired Overlay Patch: %s",
                         current.overlayPath.c_str());
    }

    ImGui::Spacing();
    const bool expandByDefault = current.engine.kind == EngineKind::SchoolDays;
    if (ImGui::CollapsingHeader(
            "Optional Game Launch Parameters (Routes, Patches, Translations)",
            expandByDefault ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
      if (current.engine.kind == EngineKind::SchoolDays) {
        ImGui::InputTextWithHint(
            "##ktrfPath",
            "Route file (.ktnroute / .ktrf) — leave empty for bundled default",
            &current.params.ktrfPath);
        // ValidateRoute paridade com SwiftUI/Android, via a mesma C ABI.
        char routeInfo[512] = {};
        const int routeOk = KotonohaLauncherValidateRoute(
            current.params.ktrfPath.c_str(), routeInfo,
            static_cast<int>(sizeof(routeInfo)));
        if (routeInfo[0] != '\0') {
          ImGui::PushStyleColor(ImGuiCol_Text, routeOk != 0
                                                  ? Kotonoha::ImGuiTheme::success()
                                                  : Kotonoha::ImGuiTheme::warning());
          ImGui::TextWrapped("%s", routeInfo);
          ImGui::PopStyleColor();
        }
        char *defaultRoute = KotonohaLauncherDefaultSchoolDaysRoute();
        if (defaultRoute) {
          ImGui::TextDisabled("Default route: %s", defaultRoute);
          KotonohaLauncherFreeString(defaultRoute);
        }
#if !(defined(__APPLE__) && TARGET_OS_IPHONE)
        ImGui::SameLine();
        if (ImGui::Button("Route (.ktnroute)...")) {
          const int idx = selected;
          browseFile(
              {{"School Days route (.ktnroute, .ktrf)", "ktnroute;ktrf"},
               {"All files", "*"}},
              current.params.ktrfPath.empty() ? current.path
                                              : current.params.ktrfPath,
              [this, idx](const std::string &chosen) {
                if (idx >= 0 && idx < static_cast<int>(catalog.size())) {
                  catalog[static_cast<std::size_t>(idx)].params.ktrfPath =
                      chosen;
                }
              });
        }
#endif
        ImGui::InputTextWithHint("##scriptsDir",
                                 "Translated ORS scripts directory (--scripts)",
                                 &current.params.scriptsDir);
#if !(defined(__APPLE__) && TARGET_OS_IPHONE)
        ImGui::SameLine();
        if (ImGui::Button("Scripts Folder...")) {
          const int idx = selected;
          browseFolder(
              current.params.scriptsDir.empty() ? current.path
                                                : current.params.scriptsDir,
              [this, idx](const std::string &chosen) {
                if (idx >= 0 && idx < static_cast<int>(catalog.size())) {
                  catalog[static_cast<std::size_t>(idx)].params.scriptsDir =
                      chosen;
                }
              });
        }
#endif
      }

      ImGui::InputTextWithHint(
          "##assetOverlayPath",
          "Asset overlay directory or patch (--asset-overlay)",
          &current.params.assetOverlayPath);
#if !(defined(__APPLE__) && TARGET_OS_IPHONE)
      ImGui::SameLine();
      if (ImGui::Button("Overlay Folder...")) {
        const int idx = selected;
        browseFolder(
            current.params.assetOverlayPath.empty()
                ? current.path
                : current.params.assetOverlayPath,
            [this, idx](const std::string &chosen) {
              if (idx >= 0 && idx < static_cast<int>(catalog.size())) {
                catalog[static_cast<std::size_t>(idx)].params.assetOverlayPath =
                    chosen;
              }
            });
      }
#endif

      ImGui::InputTextWithHint(
          "##translationPath",
          "Translation catalog file (.ktr) (--translation)",
          &current.params.translationPath);
#if !(defined(__APPLE__) && TARGET_OS_IPHONE)
      ImGui::SameLine();
      if (ImGui::Button("Translation (.ktr)...")) {
        const int idx = selected;
        browseFile(
            {{"Kotonoha Translation (.ktr)", "ktr"}, {"All files", "*"}},
            current.params.translationPath.empty()
                ? current.path
                : current.params.translationPath,
            [this, idx](const std::string &chosen) {
              if (idx >= 0 && idx < static_cast<int>(catalog.size())) {
                catalog[static_cast<std::size_t>(idx)].params.translationPath =
                    chosen;
              }
            });
      }
#endif

      ImGui::InputTextWithHint(
          "##subtitleStylePath",
          "ASS subtitle stylesheet (.ass) (--subtitle-style)",
          &current.params.subtitleStylePath);
      ImGui::InputTextWithHint("##customDocumentsDir",
                               "Custom save/documents directory (--documents)",
                               &current.params.customDocumentsDir);
      ImGui::InputTextWithHint(
          "##startScenario",
          "Initial scenario / node override (--start / --node)",
          &current.params.startScenario);
    }
  }
}

bool UnifiedLauncher::validate(std::string &error) {
  if (!Launcher::validate(error))
    return false;
  if (!hasValidSelection()) {
    error = "Select a game or library folder containing at least one supported "
            "visual novel.";
    return false;
  }
  const auto current = selectedEntry();
  const auto &registry = EngineLibraryRegistry::instance();
  if (!current.engine.implementedInKotonoha ||
      !registry.canInstantiateInProcess(current.engine.kind)) {
    error = "The selected game engine is detectable but its runtime library "
            "is not linked into this KotonohaLauncher. Reconfigure with "
            "-DKOTONOHA_BUILD_<PORT>=ON or -DKOTONOHA_BUILD_ALL=ON.";
    return false;
  }
  return true;
}

void UnifiedLauncher::save() {
  if (!rememberFilePath.empty() && !currentLibraryPath.empty()) {
    Kotonoha::writeTextFileAtomically(rememberFilePath,
                                      currentLibraryPath + "\n");
  }
  if (!rememberSaveSplitPath.empty()) {
    Kotonoha::writeTextFileAtomically(rememberSaveSplitPath,
                                      isolateSavesPerGame ? "1\n" : "0\n");
  }
  if (!rememberSaveRootPath.empty()) {
    Kotonoha::writeTextFileAtomically(rememberSaveRootPath,
                                      customSaveRootPath + "\n");
  }
  saveGameParams();
}

} // namespace KotonohaLauncher
