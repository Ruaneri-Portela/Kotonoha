// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include "LauncherModel.h"

#include <KotonohaLauncher/LauncherBridge.h>

#include <windows.h>
#include <shlobj.h>

#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>

#include <fstream>
#include <sstream>

namespace Kotonoha::WinUI {
namespace {

namespace Json = winrt::Windows::Data::Json;

std::wstring widen(std::string_view value) {
  if (value.empty())
    return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      out.data(), size);
  return out;
}

std::string narrow(std::wstring_view value) {
  if (value.empty())
    return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr, 0,
                                       nullptr, nullptr);
  std::string out(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

std::wstring appDataRoot() {
  wchar_t *raw = nullptr;
  std::wstring result;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr,
                                     &raw)) &&
      raw) {
    result = raw;
    CoTaskMemFree(raw);
  }
  return result;
}

std::wstring stateDirectory() {
  return appDataRoot() + L"\\Kotonoha\\me.hirameki.kotonoha";
}

std::wstring statePath(const wchar_t *name) {
  return stateDirectory() + L"\\" + name;
}

std::ifstream openRead(const wchar_t *name) {
  CreateDirectoryW(stateDirectory().c_str(), nullptr);
  return std::ifstream(statePath(name), std::ios::binary);
}

std::string readText(const wchar_t *name) {
  auto input = openRead(name);
  if (!input)
    return {};
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void writeText(const wchar_t *name, const std::string &content) {
  CreateDirectoryW(stateDirectory().c_str(), nullptr);
  std::ofstream output(statePath(name), std::ios::binary | std::ios::trunc);
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string jsonString(const Json::JsonObject &object, std::wstring_view key) {
  if (object.HasKey(key))
    return narrow(object.GetNamedString(key).c_str());
  return {};
}

double jsonNumber(const Json::JsonObject &object, std::wstring_view key,
                  double fallback) {
  if (object.HasKey(key))
    return object.GetNamedNumber(key);
  return fallback;
}

bool jsonBool(const Json::JsonObject &object, std::wstring_view key,
              bool fallback) {
  if (object.HasKey(key))
    return object.GetNamedBoolean(key);
  return fallback;
}

std::string callBridge(char *value) {
  if (!value)
    return {};
  std::string result(value);
  KotonohaLauncherFreeString(value);
  return result;
}

} // namespace

std::wstring DocumentsRoot() { return stateDirectory(); }

LauncherState LoadLauncherState() {
  LauncherState state;
  const std::string text = readText(L"launcher-options.json");
  if (text.empty())
    return state;
  try {
    auto root = Json::JsonObject::Parse(widen(text));
    auto library = root.GetNamedArray(L"libraryRoots");
    for (const auto &entry : library)
      state.libraryRoots.push_back(narrow(entry.GetString().c_str()));

    if (root.HasKey(L"global")) {
      auto global = root.GetNamedObject(L"global");
      state.global.renderer = jsonString(global, L"renderer");
      state.global.upscalerBackend = jsonString(global, L"upscalerBackend");
      state.global.upscalerPreset = jsonString(global, L"upscalerPreset");
      state.global.upscalerFactor =
          static_cast<float>(jsonNumber(global, L"upscalerFactor", 1.0));
      state.global.masterVolume =
          static_cast<float>(jsonNumber(global, L"masterVolume", 1.0));
      state.global.splitSavesByGame =
          jsonBool(global, L"splitSavesByGame", true);
      state.global.customSaveRoot = jsonString(global, L"customSaveRoot");
    }
    if (root.HasKey(L"theme")) {
      auto theme = root.GetNamedObject(L"theme");
      state.theme.appTheme = jsonString(theme, L"appTheme");
      if (state.theme.appTheme.empty())
        state.theme.appTheme = "Default";
      state.theme.accentColor = jsonString(theme, L"accentColor");
      if (state.theme.accentColor.empty())
        state.theme.accentColor = "#6A5BE7";
      state.theme.fontFamily = jsonString(theme, L"fontFamily");
      if (state.theme.fontFamily.empty())
        state.theme.fontFamily = "Segoe UI Variable";
      state.theme.uiScale =
          static_cast<float>(jsonNumber(theme, L"uiScale", 1.0));
    }
  } catch (...) {
  }
  return state;
}

void SaveLauncherState(const LauncherState &state) {
  Json::JsonObject root;
  Json::JsonArray roots;
  for (const auto &entry : state.libraryRoots)
    roots.Append(Json::JsonValue::CreateStringValue(widen(entry)));
  root.Insert(L"libraryRoots", roots);

  Json::JsonObject global;
  global.Insert(L"renderer",
                Json::JsonValue::CreateStringValue(widen(state.global.renderer)));
  global.Insert(L"upscalerBackend", Json::JsonValue::CreateStringValue(
                                        widen(state.global.upscalerBackend)));
  global.Insert(L"upscalerPreset", Json::JsonValue::CreateStringValue(
                                       widen(state.global.upscalerPreset)));
  global.Insert(L"upscalerFactor", Json::JsonValue::CreateNumberValue(
                                       state.global.upscalerFactor));
  global.Insert(L"masterVolume", Json::JsonValue::CreateNumberValue(
                                     state.global.masterVolume));
  global.Insert(L"splitSavesByGame", Json::JsonValue::CreateBooleanValue(
                                         state.global.splitSavesByGame));
  global.Insert(L"customSaveRoot", Json::JsonValue::CreateStringValue(
                                       widen(state.global.customSaveRoot)));
  root.Insert(L"global", global);

  Json::JsonObject theme;
  theme.Insert(L"appTheme",
               Json::JsonValue::CreateStringValue(widen(state.theme.appTheme)));
  theme.Insert(L"accentColor", Json::JsonValue::CreateStringValue(
                                   widen(state.theme.accentColor)));
  theme.Insert(L"fontFamily", Json::JsonValue::CreateStringValue(
                                  widen(state.theme.fontFamily)));
  theme.Insert(L"uiScale",
               Json::JsonValue::CreateNumberValue(state.theme.uiScale));
  root.Insert(L"theme", theme);

  writeText(L"launcher-options.json", narrow(root.Stringify().c_str()));
}

std::string ScanRoots(const LauncherState &state) {
  std::vector<std::string> roots;
  for (const auto &entry : state.libraryRoots) {
    if (!entry.empty())
      roots.push_back(entry);
  }
  std::vector<const char *> pointers;
  pointers.reserve(roots.size());
  for (const auto &entry : roots)
    pointers.push_back(entry.c_str());
  return callBridge(KotonohaLauncherScanRootsJson(
      pointers.data(), static_cast<int>(pointers.size()),
      state.global.splitSavesByGame ? 1 : 0,
      state.global.customSaveRoot.c_str()));
}

GameParams LoadGameParams(const std::string &profileId) {
  GameParams params;
  const std::string json = callBridge(KotonohaLauncherLoadGameParamsJson(
      profileId.c_str()));
  if (json.empty())
    return params;
  try {
    auto root = Json::JsonObject::Parse(widen(json));
    params.assetOverlayPath = jsonString(root, L"assetOverlayPath");
    params.translationPath = jsonString(root, L"translationPath");
    params.subtitleStylePath = jsonString(root, L"subtitleStylePath");
    params.customDocumentsDir = jsonString(root, L"customDocumentsDir");
    params.ktrfPath = jsonString(root, L"ktrfPath");
    params.scriptsDir = jsonString(root, L"scriptsDir");
    params.startScenario = jsonString(root, L"startScenario");
    params.rendererBackend = jsonString(root, L"rendererBackend");
    params.upscalerBackend = jsonString(root, L"upscalerBackend");
    params.upscalerPreset = jsonString(root, L"upscalerPreset");
    params.upscalerFactor = jsonString(root, L"upscalerFactor");
  } catch (...) {
  }
  return params;
}

bool SaveGameParams(const std::string &profileId, const GameParams &params) {
  return KotonohaLauncherSaveGameParams(
             profileId.c_str(), params.assetOverlayPath.c_str(),
             params.translationPath.c_str(), params.subtitleStylePath.c_str(),
             params.customDocumentsDir.c_str(), params.ktrfPath.c_str(),
             params.scriptsDir.c_str(), params.startScenario.c_str(),
             params.rendererBackend.c_str(), params.upscalerBackend.c_str(),
             params.upscalerPreset.c_str(), params.upscalerFactor.c_str()) == 1;
}

std::string SaveProfileEngineSettings(const std::string &profileId,
                                      const std::string &partialJson) {
  return callBridge(KotonohaLauncherSaveProfileEngineSettingsJson(
      profileId.c_str(), partialJson.c_str()));
}

std::string UpscalePresets() {
  return callBridge(KotonohaLauncherUpscalePresetsJson());
}

bool LaunchGame(const std::string &executable, const GameParams &params,
                const GlobalLaunchOptions &global, const std::string &profileId,
                std::wstring &error) {
  std::wstring command = L"\"" + widen(executable) + L"\"";
  command += L" --no-launcher --library \"" + widen(executable) + L"\"";
  command += L" --config \"" + DocumentsRoot() + L"\\Kotonoha-Profiles\\" +
             widen(profileId) + L"\\settings.json\"";

  auto appendIfSet = [&](const char *flag, const std::string &value) {
    if (!value.empty())
      command += std::wstring(L" ") + widen(flag) + L" \"" + widen(value) + L"\"";
  };
  appendIfSet("--asset-overlay", params.assetOverlayPath);
  appendIfSet("--translation", params.translationPath);
  appendIfSet("--subtitle-style", params.subtitleStylePath);
  appendIfSet("--ktrf", params.ktrfPath);
  appendIfSet("--scripts", params.scriptsDir);
  appendIfSet("--start", params.startScenario);
  appendIfSet("--renderer",
              params.rendererBackend.empty() ? global.renderer
                                             : params.rendererBackend);
  appendIfSet("--upscaler",
              params.upscalerBackend.empty() ? global.upscalerBackend
                                             : params.upscalerBackend);
  appendIfSet("--upscaler-preset",
              params.upscalerPreset.empty() ? global.upscalerPreset
                                            : params.upscalerPreset);
  if (!global.splitSavesByGame)
    command += L" --split-saves 0";

  std::wstring environment;
  const std::wstring userData = L"KOTONOHA_USER_DATA_DIR=" + appDataRoot();
  environment.assign(userData.begin(), userData.end());
  environment.push_back(L'\0');
  environment.push_back(L'\0');

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  std::wstring mutableCommand = command;
  const BOOL ok = CreateProcessW(
      nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
      CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP, environment.data(),
      nullptr, &startup, &process);
  if (!ok) {
    error = L"CreateProcessW failed: " + std::to_wstring(GetLastError());
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

} // namespace Kotonoha::WinUI
