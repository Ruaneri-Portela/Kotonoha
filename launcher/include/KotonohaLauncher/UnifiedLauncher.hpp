// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <Kotonoha/Filesystem/EngineDetector.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/core/Config.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Launcher.hpp>
#include <Kotonoha/core/Runtime.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace KotonohaLauncher {

struct GameLaunchParameters {
  // Common optional parameters across all ports
  std::string assetOverlayPath;    // --asset-overlay <path>
  std::string translationPath;     // --translation <file.ktr>
  std::string subtitleStylePath;   // --subtitle-style <file.ass>
  std::string customDocumentsDir;  // --documents <dir>

  // School Days specific
  std::string ktrfPath;            // --ktrf <file.ktnroute>
  std::string scriptsDir;          // --scripts <dir>

  // RealLive / Amagami / Ponscripter / N2System / SchoolDays start override
  std::string startScenario;       // --start / --node <id>

  // Graphics & Upscaling overrides (empty = inherit launcher global / Auto)
  std::string rendererBackend;     // --renderer <name>
  std::string upscalerBackend;     // --upscaler <auto|none|linear|mpv|metalfx>
  std::string upscalerPreset;      // --upscaler-preset <index|name>
  std::string upscalerFactor;      // --upscaler-factor <1.0..4.0>
};

struct DiscoveredGameEntry {
  std::string title;
  std::string saveProfileId; // Sanitized per-title ID (e.g. "clannad_pt-br")
  std::string path;
  std::string overlayPath;
  Kotonoha::Filesystem::DetectedEngine engine;
  std::string libraryTarget; // CMake engine library target, e.g. "reallive_port"
  std::string targetBinary;  // Modular port executable, e.g. "RealLive"
  std::string cliOption;     // Port CLI option, e.g. "--reallive"
  std::string packageId;     // Bundle/package ID, e.g. "me.hirameki.kotonoha.reallive"
  GameLaunchParameters params;
};

// Swappable in-process engine instance produced by an engine library factory.
struct MountedEngineInstance {
  std::unique_ptr<Kotonoha::Runtime> runtime;
  std::shared_ptr<Kotonoha::AssetProvider> assets;
  // Release game-owned textures/audio before the Engine renderer is destroyed.
  std::function<void(Kotonoha::Runtime &)> shutdown;
};

using EngineLibraryFactory = std::function<MountedEngineInstance(
    const DiscoveredGameEntry &entry, Kotonoha::EngineOptions &options)>;

struct EngineLibraryDescriptor {
  Kotonoha::Filesystem::EngineKind kind =
      Kotonoha::Filesystem::EngineKind::Unknown;
  std::string slug;
  std::string displayName;
  std::string libraryTarget;
  std::string targetBinary;
  std::string cliOption;
  std::string packageId;
  EngineLibraryFactory factory;
};

// Registry of linked in-process game runtimes. Unlinked ports remain detectable
// but cannot be launched until their CMake game option is enabled.
class EngineLibraryRegistry {
public:
  static EngineLibraryRegistry &instance();

  void registerLibrary(EngineLibraryDescriptor descriptor);
  const EngineLibraryDescriptor *
  find(Kotonoha::Filesystem::EngineKind kind) const;
  const EngineLibraryDescriptor *findBySlug(std::string_view slug) const;
  const std::vector<EngineLibraryDescriptor> &all() const {
    return descriptors_;
  }

  bool canInstantiateInProcess(Kotonoha::Filesystem::EngineKind kind) const;
  MountedEngineInstance
  instantiate(const DiscoveredGameEntry &entry,
              Kotonoha::EngineOptions &options) const;

private:
  EngineLibraryRegistry();
  std::vector<EngineLibraryDescriptor> descriptors_;
};

// Linked game libraries supply their in-process factories at build time.
// No dynamic executable search, fork, exec, or shell dispatch.
void registerLinkedEngineFactories(EngineLibraryRegistry &registry);

// Computes a safe, deterministic per-game profile identifier from a game's
// folder or disc filename (e.g. "Clannad_PT-BR" -> "clannad_pt-br",
// "AIR-SE.ISO" -> "air-se", "ZeroNoTsukaima0.bin" -> "zeronotsukaima0") so
// multiple games sharing one engine (RealLive, SummerRadish, ZeroNoTsukaima,
// Kirikiri, etc.) never collide in save or config directories.
std::string sanitizeGameProfileId(std::string_view titleOrPath);

// Resolves the per-game isolated user-data root and concrete save directory
// for `entry`. When `splitSavesByGame` is true, returns a per-title root
// `<baseUserData>/Kotonoha/Profiles/<saveProfileId>` so `StoragePaths` places
// saves under `<root>/Kotonoha/<packageId>/save`.
std::string resolveIsolatedUserDataRoot(const DiscoveredGameEntry &entry,
                                        bool splitSavesByGame = true,
                                        std::string_view customSaveRoot = {});

std::string resolveGameSaveDirectory(const DiscoveredGameEntry &entry,
                                     bool splitSavesByGame = true,
                                     std::string_view customSaveRoot = {});

// Applies the per-game isolated save/config environment (`KOTONOHA_USER_DATA_DIR`)
// to the current process and updates `options.settingsPath` so neither saves
// nor settings collide across games.
void applyPerGameSaveIsolation(const DiscoveredGameEntry &entry,
                               Kotonoha::EngineOptions &options,
                               bool splitSavesByGame = true,
                               std::string_view customSaveRoot = {});

// Enhanced Kotonoha multi-engine detector that covers all retail and localized
// KotonohaAssets titles (folders, .iso, and PS2 Mode 1/2 .bin) as well as all
// supported visual novel engine families.
Kotonoha::Filesystem::DetectedEngine
detectKotonohaEngine(const Kotonoha::Filesystem::Filesystem &source,
                     std::string_view hintPath = {});

Kotonoha::Filesystem::DetectedEngine
detectKotonohaEngineFromPath(const std::string &path);

DiscoveredGameEntry
buildDiscoveredGameEntry(const std::string &path,
                         const Kotonoha::Filesystem::DetectedEngine &detected);

// Callback invoked as soon as one game is found. The entry is fully resolved
// (engine, save profile and persisted parameters) before the callback runs.
using GameFoundCallback = std::function<void(const DiscoveredGameEntry &)>;

// Scans a single game directory/disc image or a multi-game library folder.
// maxDepth defaults to 0: only the root and its direct children are visited.
// Candidates are probed in parallel; results are sorted by title.
std::vector<DiscoveredGameEntry>
scanGameLibrary(const std::string &libraryOrGamePath, int maxDepth = 0,
                GameFoundCallback callback = {});

// Scans multiple roots (Documents/games, Documents, bookmarked folders/ISOs,
// Android SAF URIs), deduplicating by canonical path and pairing overlays.
std::vector<DiscoveredGameEntry>
scanGameLibraryRoots(const std::vector<std::string> &roots, int maxDepth = 0,
                     GameFoundCallback callback = {});

// Persistent per-game launch parameter store shared by Desktop ImGui, iOS
// SwiftUI, and Android Material 3 launchers.
std::string defaultGameParamsFilePath();
std::map<std::string, GameLaunchParameters>
loadGameParamsMap(const std::string &filePath = {});
bool saveGameParamsMap(
    const std::map<std::string, GameLaunchParameters> &params,
    const std::string &filePath = {});
GameLaunchParameters
loadGameParamsForProfile(const std::string &saveProfileId,
                         const std::string &filePath = {});
bool saveGameParamsForProfile(const std::string &saveProfileId,
                              const GameLaunchParameters &params,
                              const std::string &filePath = {});

// Formats a human-readable and machine-parseable summary table of discovered
// games, their CMake engine libraries, and their isolated per-game save IDs.
std::string formatDiscoveredGamesReport(
    const std::vector<DiscoveredGameEntry> &entries,
    bool splitSavesByGame = true);

// Unified Kotonoha multi-engine launcher hub (lives in `launcher/`,
// outside `games/`).
class UnifiedLauncher final : public Kotonoha::Launcher {
public:
  UnifiedLauncher(std::string settingsPath, std::string initialLibraryPath = {},
                  Kotonoha::EngineSettings defaults = {});

  const std::string &libraryPath() const { return currentLibraryPath; }
  void setLibraryPath(std::string path);

  bool splitSavesByGame() const { return isolateSavesPerGame; }
  void setSplitSavesByGame(bool enabled) { isolateSavesPerGame = enabled; }

  const std::string &customSaveRoot() const { return customSaveRootPath; }
  void setCustomSaveRoot(std::string path) {
    customSaveRootPath = std::move(path);
  }

  const std::vector<DiscoveredGameEntry> &entries() const { return catalog; }
  int selectedIndex() const { return selected; }
  void setSelectedIndex(int index);

  bool hasValidSelection() const;
  DiscoveredGameEntry selectedEntry() const;
  std::string selectedSaveDirectory() const;

protected:
  void drawGameOptions() override;
  bool validate(std::string &error) override;
  void save() override;

private:
  std::string currentLibraryPath;
  std::string customSaveRootPath;
  std::string rememberFilePath;
  std::string rememberSaveSplitPath;
  std::string rememberSaveRootPath;
  std::string rememberGameParamsPath;
  std::string filterText;
  std::vector<DiscoveredGameEntry> catalog;
  std::map<std::string, GameLaunchParameters> savedGameParams;
  int selected = 0;
  bool isolateSavesPerGame = true;

  void rescan();
  void loadSavedGameParams();
  void saveGameParams() const;
};

} // namespace KotonohaLauncher
