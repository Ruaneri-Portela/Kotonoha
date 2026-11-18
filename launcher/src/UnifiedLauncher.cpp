// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <KotonohaLauncher/UnifiedLauncher.hpp>

#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/debug/ImGuiTheme.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <system_error>

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
  const std::string pkg =
      entry.packageId.empty() ? "me.hirameki.kotonoha.launcher" : entry.packageId;
  return (userRoot / "Kotonoha" / pkg / "save").lexically_normal().string();
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

  if (splitSavesByGame) {
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
  registerLibrary({EngineKind::LucaSystem, "luca-system", "LucaSystem",
                   "luca_system_formats", "LucaSystem", "--luca-system",
                   "me.hirameki.kotonoha.lucasystem", {}});
  registerLibrary({EngineKind::RealLive, "reallive", "RealLive / rlBabel",
                   "reallive_port", "RealLive", "--reallive",
                   "me.hirameki.kotonoha.reallive", {}});
  registerLibrary({EngineKind::Kirikiri, "kirikiri", "KiriKiri2 / KiriKiriZ",
                   "kirikiri_formats", "Kirikiri", "--kirikiri",
                   "me.hirameki.kotonoha.kirikiri", {}});
  registerLibrary({EngineKind::RenPy, "doki-doki-literature-club", "Ren'Py",
                   "doki_doki_literature_club", "DokiDoki", "--ddlc",
                   "me.hirameki.kotonoha.dokidokiliteratureclub", {}});
  registerLibrary({EngineKind::N2System, "n2system", "N2System / Nitroplus",
                   "n2system_formats", "N2SystemGame", "--n2system",
                   "me.hirameki.kotonoha.n2system", {}});
  registerLibrary({EngineKind::ONScripter, "ponscripter",
                   "PONScripter / ONScripter", "ponscripter_port",
                   "PonscripterGame", "--ponscripter",
                   "me.hirameki.kotonoha.ponscripter", {}});
  registerLibrary({EngineKind::Amagami, "amagami", "Amagami (PS2 SCF)",
                   "amagami_port", "Amagami", "--amagami",
                   "me.hirameki.kotonoha.amagami", {}});
  registerLibrary({EngineKind::SchoolDays, "school-days",
                   "Overflow GPK / FILMEngine (School Days HQ)", "school_days",
                   "SchoolDays", "--school-days",
                   "me.hirameki.kotonoha.schooldays", {}});
  registerLibrary({EngineKind::PureMail, "pmail", "Pure Mail (OverFlow ADV)",
                   "PmailPort", "Pmail", "--pmail",
                   "me.hirameki.kotonoha.pmail", {}});
  registerLibrary({EngineKind::SnowRadish, "snow-radish-vacation",
                   "Snow Radish ADV (of_adv3)", "snow_radish_vacation",
                   "SnowRadish", "--snow",
                   "me.hirameki.kotonoha.snowradishvacation", {}});
  registerLibrary({EngineKind::SummerRadish, "summer-radish-vacation",
                   "Summer Radish / Overflow CMP", "summer_radish_vacation",
                   "SummerRadish", "--summer",
                   "me.hirameki.kotonoha.summerradishvacation", {}});
  registerLibrary({EngineKind::ZeroNoTsukaima, "zero-no-tsukaima",
                   "Zero no Tsukaima (PS2 Squirrel/KAG)", "zero_no_tsukaima",
                   "ZeroNoTsukaima", "--zero",
                   "me.hirameki.kotonoha.zeronotsukaima", {}});
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
  const auto *desc = find(entry.engine.kind);
  if (desc && desc->factory)
    return desc->factory(entry, options);
  return {};
}

DetectedEngine detectKotonohaEngine(const Filesystem &source,
                                    std::string_view hintPath) {
  // 1. Check Overflow gEngine / MEO (Personal/MISSEACHOTHER, MISS EACH OTHER.msi,
  //    MEO.exe, gEngine.gesl, or FILMEngine.gem).
  if (hasFileCaseInsensitive(source, "MISS EACH OTHER.msi") ||
      hasFileCaseInsensitive(source, "gEngine.gesl") ||
      hasFileCaseInsensitive(source, "MEO.exe") ||
      findRequiredFilesPrefix(
          source, {"SCRIPT.CMP"},
          {"Personal/MISSEACHOTHER/", "Personal/MISSEACHOTHER/DATA/",
           "Personal/MISSEACHOTHER/PACK/"})) {
    return {EngineKind::SummerRadish,
            "summer-radish-vacation",
            "Overflow gEngine / FILMEngine (MEO CMP)",
            "Personal/MISSEACHOTHER/",
            "MISS EACH OTHER (CMP / FILMEngine)",
            true};
  }

  // 2. Check Summer Radish Vacation 1 & 2 (SCRIPT.CMP + GRAPHIC.CMP in
  //    DISC1/PACK/, PACK/, DATA/, or root; or INSTALL.cmp + Info.DISC).
  if (const auto cmpPrefix = findRequiredFilesPrefix(
          source, {"SCRIPT.CMP", "GRAPHIC.CMP"},
          {"", "DATA/", "PACK/", "DISC1/PACK/", "DISC1/DATA/"})) {
    return {EngineKind::SummerRadish,
            "summer-radish-vacation",
            "Summer Radish CMP",
            *cmpPrefix,
            *cmpPrefix + "SCRIPT.CMP",
            true};
  }
  if (hasFileCaseInsensitive(source, "INSTALL.cmp") &&
      hasFileCaseInsensitive(source, "Info.DISC")) {
    return {EngineKind::SummerRadish,
            "summer-radish-vacation",
            "Summer Radish CMP",
            "DISC1/",
            "INSTALL.cmp + Info.DISC",
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
      return {EngineKind::SnowRadish,
              "snow-radish-vacation",
              "Snow Radish ADV (of_adv3)",
              *snowPrefix,
              *snowPrefix + "SCRIPT.PAK",
              true};
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
      findRequiredFilesPrefix(source, {"vnx.rpx"}, {"vnx/game/", "game/", ""})) {
    return {EngineKind::RenPy,
            "doki-doki-literature-club",
            "Ren'Py (VNX PT-BR Patch)",
            "vnx/game/",
            "vnx/game/vnx.rpx",
            true};
  }

  (void)hintPath;
  return detected;
}

DetectedEngine detectKotonohaEngineFromPath(const std::string &path) {
  if (path.empty())
    return {};
  try {
    auto mounted = Filesystem::mount(path);
    if (!mounted)
      return {};
    return detectKotonohaEngine(*mounted, path);
  } catch (...) {
    return {};
  }
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

  if (const auto *desc = EngineLibraryRegistry::instance().find(detected.kind)) {
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
      return results;
    }
  }

  if (!fs::is_directory(rootPath, ec))
    return results;

  std::string vnxPatchPath;
  std::vector<fs::path> candidates;
  for (const auto &dirEntry : fs::directory_iterator(rootPath, ec)) {
    const auto &p = dirEntry.path();
    const std::string name = p.filename().string();
    if (name.empty() || name[0] == '.')
      continue;
    if (dirEntry.is_directory(ec) ||
        (dirEntry.is_regular_file(ec) && isDiscImageExtension(p))) {
      candidates.push_back(p);
    }
  }

  std::sort(candidates.begin(), candidates.end());

  for (const auto &candidate : candidates) {
    try {
      auto mounted = Filesystem::mount(candidate.string());
      if (!mounted)
        continue;
      if (isVnxOverlayPatch(*mounted)) {
        vnxPatchPath = candidate.string();
      }
      const auto detected =
          detectKotonohaEngine(*mounted, candidate.string());
      if (detected.kind != EngineKind::Unknown) {
        results.push_back(
            buildDiscoveredGameEntry(candidate.string(), detected));
      }
    } catch (...) {
      continue;
    }
  }

  // Pair companion VNX PT-BR patch with base DDLC automatically.
  if (!vnxPatchPath.empty()) {
    for (auto &item : results) {
      if (item.engine.kind == EngineKind::RenPy && item.path != vnxPatchPath &&
          item.overlayPath.empty()) {
        item.overlayPath = vnxPatchPath;
      }
    }
  }

  return results;
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
    : Launcher("KotonohaLauncher — Unified Multi-Engine Hub",
               settingsPath, std::move(defaults)) {
  if (!settingsPath.empty()) {
    const fs::path parent = fs::path(settingsPath).parent_path();
    rememberFilePath = (parent / "library.txt").string();
    rememberSaveSplitPath = (parent / "save-isolation.txt").string();
    rememberSaveRootPath = (parent / "save-root.txt").string();
  }
  const std::string savedSplit = readFirstLineFromFile(rememberSaveSplitPath);
  if (savedSplit == "0" || savedSplit == "false")
    isolateSavesPerGame = false;
  customSaveRootPath = readFirstLineFromFile(rememberSaveRootPath);

  std::string startPath = std::move(initialLibraryPath);
  if (startPath.empty())
    startPath = readFirstLineFromFile(rememberFilePath);
  setLibraryPath(std::move(startPath));
}

void UnifiedLauncher::setLibraryPath(std::string path) {
  currentLibraryPath = std::move(path);
  rescan();
}

void UnifiedLauncher::rescan() {
  catalog = scanGameLibrary(currentLibraryPath);
  if (selected < 0 || selected >= static_cast<int>(catalog.size()))
    selected = 0;
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
  ImGui::Checkbox(
      "Split Save & Config Folder by Game Title (Prevent Same-Engine Conflicts)",
      &isolateSavesPerGame);
  ImGui::InputTextWithHint(
      "##customSaveRoot",
      "Optional custom save root directory (leave empty for platform default)...",
      &customSaveRootPath);

  ImGui::Spacing();
  ImGui::InputTextWithHint("##filter",
                           "Filter by title, engine family, or CMake library...",
                           &filterText);
  ImGui::Spacing();

  if (ImGui::BeginTable("UnifiedCatalog", 5,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_ScrollY,
                        ImVec2(0.0f, 220.0f))) {
    ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Engine Family",
                            ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Engine Library",
                            ImGuiTableColumnFlags_WidthFixed, 145.0f);
    ImGui::TableSetupColumn("Save Profile",
                            ImGuiTableColumnFlags_WidthFixed, 130.0f);
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
      ImGui::TextUnformatted(
          isolateSavesPerGame ? item.saveProfileId.c_str() : "(shared engine)");

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
    const auto current = selectedEntry();
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
}

} // namespace KotonohaLauncher
