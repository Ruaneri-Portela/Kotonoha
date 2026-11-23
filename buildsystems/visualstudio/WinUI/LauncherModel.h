// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Native WinUI launcher state. Plain C++/WinRT types are used only for JSON and
// storage; all game detection/validation is delegated to the C ABI bridge in
// launcher/include/KotonohaLauncher/LauncherBridge.h.
#pragma once

#include <string>
#include <vector>

namespace Kotonoha::WinUI {

struct GlobalLaunchOptions {
  std::string renderer;
  std::string upscalerBackend;
  std::string upscalerPreset;
  float upscalerFactor = 1.0f;
  float masterVolume = 1.0f;
  bool splitSavesByGame = true;
  std::string customSaveRoot;
};

struct GameParams {
  std::string assetOverlayPath;
  std::string translationPath;
  std::string subtitleStylePath;
  std::string customDocumentsDir;
  std::string ktrfPath;
  std::string scriptsDir;
  std::string startScenario;
  std::string rendererBackend;
  std::string upscalerBackend;
  std::string upscalerPreset;
  std::string upscalerFactor;
};

struct ThemeState {
  std::string appTheme = "Default"; // Default / Light / Dark / HighContrast
  std::string accentColor = "#6A5BE7";
  std::string fontFamily = "Segoe UI Variable";
  float uiScale = 1.0f;
};

struct LauncherState {
  std::vector<std::string> libraryRoots;
  GlobalLaunchOptions global;
  ThemeState theme;
};

// Documents root used by the engine: <user data>/Kotonoha/<app-id>.
std::wstring DocumentsRoot();

// Persisted with Windows.Data.Json in %APPDATA%\Kotonoha\<app-id>\.
LauncherState LoadLauncherState();
void SaveLauncherState(const LauncherState &state);

// Bridge-backed game library scan. Returns a UTF-8 JSON array of
// DiscoveredGameEntry objects (same schema as the macOS/Android launchers).
std::string ScanRoots(const LauncherState &state);

// Bridge-backed per-game params (game-params.txt, never written directly).
GameParams LoadGameParams(const std::string &profileId);
bool SaveGameParams(const std::string &profileId, const GameParams &params);

// Merges a partial EngineSettings JSON into Kotonoha-Profiles/<id>/settings.json.
std::string SaveProfileEngineSettings(const std::string &profileId,
                                      const std::string &partialJson);

// Returns the list of upscaler presets as UTF-8 JSON from the bridge.
std::string UpscalePresets();

// Starts the selected game with the chosen options. Returns false when
// CreateProcessW fails; `error` receives a human-readable message.
bool LaunchGame(const std::string &executable, const GameParams &params,
                const GlobalLaunchOptions &global, const std::string &profileId,
                std::wstring &error);

} // namespace Kotonoha::WinUI
