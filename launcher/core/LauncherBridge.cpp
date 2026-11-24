// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <KotonohaLauncher/LauncherBridge.h>
#include <KotonohaLauncher/UnifiedLauncher.hpp>

#include <Kotonoha/core/Engine.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(KOTONOHA_LAUNCH_SCHOOL_DAYS)
#include "SchoolDaysLauncher.hpp"
#include <SchoolDays/SchoolDaysAssets.hpp>
#endif

namespace {

namespace fs = std::filesystem;

char *dupCString(const std::string &str) {
  auto *out = static_cast<char *>(std::malloc(str.size() + 1));
  if (!out)
    return nullptr;
  std::memcpy(out, str.data(), str.size());
  out[str.size()] = '\0';
  return out;
}

std::string safeString(const char *ptr) { return ptr ? std::string(ptr) : ""; }

void appendJsonString(std::ostringstream &out, std::string_view value) {
  out << '"';
  for (unsigned char ch : value) {
    switch (ch) {
    case '"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\b':
      out << "\\b";
      break;
    case '\f':
      out << "\\f";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      if (ch < 0x20) {
        char buf[7];
        std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
        out << buf;
      } else {
        out << static_cast<char>(ch);
      }
      break;
    }
  }
  out << '"';
}

bool isDiscImagePath(const std::string &path) {
  std::string ext = fs::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return ext == ".iso" || ext == ".bin" || ext == ".img";
}

void appendParamsObject(std::ostringstream &out,
                        const KotonohaLauncher::GameLaunchParameters &params) {
  out << '{';
  out << "\"assetOverlayPath\":";
  appendJsonString(out, params.assetOverlayPath);
  out << ",\"translationPath\":";
  appendJsonString(out, params.translationPath);
  out << ",\"subtitleStylePath\":";
  appendJsonString(out, params.subtitleStylePath);
  out << ",\"customDocumentsDir\":";
  appendJsonString(out, params.customDocumentsDir);
  out << ",\"ktrfPath\":";
  appendJsonString(out, params.ktrfPath);
  out << ",\"scriptsDir\":";
  appendJsonString(out, params.scriptsDir);
  out << ",\"startScenario\":";
  appendJsonString(out, params.startScenario);
  out << ",\"rendererBackend\":";
  appendJsonString(out, params.rendererBackend);
  out << ",\"upscalerBackend\":";
  appendJsonString(out, params.upscalerBackend);
  out << ",\"upscalerPreset\":";
  appendJsonString(out, params.upscalerPreset);
  out << ",\"upscalerFactor\":";
  appendJsonString(out, params.upscalerFactor);
  out << '}';
}

void appendEntryObject(std::ostringstream &out,
                       const KotonohaLauncher::DiscoveredGameEntry &entry,
                       bool splitSavesByGame,
                       const std::string &customSaveRoot) {
  const auto &registry = KotonohaLauncher::EngineLibraryRegistry::instance();
  const bool linked = registry.canInstantiateInProcess(entry.engine.kind);
  const std::string saveDir = KotonohaLauncher::resolveGameSaveDirectory(
      entry, splitSavesByGame, customSaveRoot);
  const bool isDisc = isDiscImagePath(entry.path);
  const bool isSchoolDays =
      entry.engine.kind == Kotonoha::Filesystem::EngineKind::SchoolDays;

  out << '{';
  out << "\"title\":";
  appendJsonString(out, entry.title);
  out << ",\"saveProfileId\":";
  appendJsonString(out, entry.saveProfileId);
  out << ",\"path\":";
  appendJsonString(out, entry.path);
  out << ",\"overlayPath\":";
  appendJsonString(out, entry.overlayPath);
  out << ",\"engineSlug\":";
  appendJsonString(out, entry.engine.slug);
  out << ",\"engineDisplayName\":";
  appendJsonString(out, entry.engine.displayName);
  out << ",\"evidence\":";
  appendJsonString(out, entry.engine.evidence);
  out << ",\"libraryTarget\":";
  appendJsonString(out, entry.libraryTarget);
  out << ",\"targetBinary\":";
  appendJsonString(out, entry.targetBinary);
  out << ",\"packageId\":";
  appendJsonString(out, entry.packageId);
  out << ",\"saveDirectory\":";
  appendJsonString(out, saveDir);
  out << ",\"isDiscImage\":" << (isDisc ? "true" : "false");
  out << ",\"linkedInProcess\":" << (linked ? "true" : "false");
  out << ",\"isSchoolDays\":" << (isSchoolDays ? "true" : "false");
  out << ",\"params\":";
  appendParamsObject(out, entry.params);
  out << '}';
}

char *launchResponseJson(bool ok, std::string_view phase,
                         std::string_view message, int frames, bool dryRun,
                         std::string_view path, std::string_view slug) {
  std::ostringstream out;
  out << "{\"ok\":" << (ok ? "true" : "false") << ",\"phase\":";
  appendJsonString(out, phase);
  out << ",\"message\":";
  appendJsonString(out, message);
  out << ",\"frames\":" << frames << ",\"dryRun\":"
      << (dryRun ? "true" : "false") << ",\"path\":";
  appendJsonString(out, path);
  out << ",\"engineSlug\":";
  appendJsonString(out, slug);
  out << '}';
  return dupCString(out.str());
}

bool envFlagEnabled(const char *name) {
  const char *value = std::getenv(name);
  if (!value || value[0] == '\0')
    return false;
  const std::string normalized = [value]() {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
      return static_cast<char>(std::tolower(ch));
    });
    return out;
  }();
  return normalized != "0" && normalized != "false" && normalized != "off";
}

// Restores the process-wide save-root override after one in-process launch,
// so a host launching several games in sequence cannot leak a previous
// profile's KOTONOHA_USER_DATA_DIR into the next one.
class SaveEnvironmentGuard {
public:
  SaveEnvironmentGuard() {
    if (const char *value = SDL_getenv("KOTONOHA_USER_DATA_DIR")) {
      hadPrevious_ = true;
      previous_ = value;
    }
  }

  ~SaveEnvironmentGuard() {
    if (auto *env = SDL_GetEnvironment()) {
      if (hadPrevious_)
        SDL_SetEnvironmentVariable(env, "KOTONOHA_USER_DATA_DIR",
                                   previous_.c_str(), true);
      else
        SDL_UnsetEnvironmentVariable(env, "KOTONOHA_USER_DATA_DIR");
    }
#if !defined(_WIN32)
    if (hadPrevious_)
      setenv("KOTONOHA_USER_DATA_DIR", previous_.c_str(), 1);
    else
      unsetenv("KOTONOHA_USER_DATA_DIR");
#endif
  }

  SaveEnvironmentGuard(const SaveEnvironmentGuard &) = delete;
  SaveEnvironmentGuard &operator=(const SaveEnvironmentGuard &) = delete;

private:
  bool hadPrevious_ = false;
  std::string previous_;
};

} // namespace

extern "C" {

char *KotonohaLauncherScanRootsJson(const char *const *roots, int rootCount,
                                    int splitSavesByGame,
                                    const char *customSaveRoot) {
  try {
    std::vector<std::string> rootList;
    if (roots && rootCount > 0) {
      rootList.reserve(static_cast<std::size_t>(rootCount));
      for (int i = 0; i < rootCount; ++i) {
        if (roots[i] && roots[i][0] != '\0')
          rootList.emplace_back(roots[i]);
      }
    }
    const auto entries = KotonohaLauncher::scanGameLibraryRoots(rootList);
    const std::string saveRootStr = safeString(customSaveRoot);
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < entries.size(); ++i) {
      if (i > 0)
        out << ',';
      appendEntryObject(out, entries[i], splitSavesByGame != 0, saveRootStr);
    }
    out << ']';
    return dupCString(out.str());
  } catch (...) {
    return dupCString("[]");
  }
}

char *KotonohaLauncherLaunchGame(const KotonohaGameLaunchRequest *request) {
  try {
    if (!request || !request->gamePath || request->gamePath[0] == '\0')
      return launchResponseJson(false, "request", "gamePath is required", 0,
                                false, "", "");
    const std::string path = safeString(request->gamePath);
    const auto detected = KotonohaLauncher::detectKotonohaEngineFromPath(path);
    if (detected.kind == Kotonoha::Filesystem::EngineKind::Unknown)
      return launchResponseJson(false, "detect", "Unrecognized game path", 0,
                                false, path, "");
    auto entry = KotonohaLauncher::buildDiscoveredGameEntry(path, detected);
    if (request->engineSlug && request->engineSlug[0] != '\0' &&
        request->engineSlug != entry.engine.slug) {
      return launchResponseJson(
          false, "detect",
          std::string("engineSlug mismatch: expected '") + entry.engine.slug +
              "', got '" + request->engineSlug + "'",
          0, false, path, entry.engine.slug);
    }
    if (request->saveProfileId && request->saveProfileId[0] != '\0')
      entry.saveProfileId = request->saveProfileId;

    auto &params = entry.params;
    params.assetOverlayPath = safeString(request->assetOverlayPath);
    params.translationPath = safeString(request->translationPath);
    params.subtitleStylePath = safeString(request->subtitleStylePath);
    params.customDocumentsDir = safeString(request->customDocumentsDir);
    params.ktrfPath = safeString(request->ktrfPath);
    params.scriptsDir = safeString(request->scriptsDir);
    params.startScenario = safeString(request->startScenario);
    params.rendererBackend = safeString(request->rendererBackend);
    params.upscalerBackend = safeString(request->upscalerBackend);
    params.upscalerPreset = safeString(request->upscalerPreset);
    params.upscalerFactor = safeString(request->upscalerFactor);
    if (params.assetOverlayPath.empty() && !entry.overlayPath.empty())
      params.assetOverlayPath = entry.overlayPath;
    if (params.ktrfPath.empty()) {
      const auto route = KotonohaLauncher::loadGameParamsForProfile(
          entry.saveProfileId);
      if (!route.ktrfPath.empty())
        params.ktrfPath = route.ktrfPath;
    }

    const auto &registry = KotonohaLauncher::EngineLibraryRegistry::instance();
    if (!registry.canInstantiateInProcess(detected.kind)) {
      return launchResponseJson(false, "registry",
                                "No in-process runtime linked for '" +
                                    detected.slug + "'",
                                0, false, path, detected.slug);
    }

    const bool dryRun = envFlagEnabled("KOTONOHA_LAUNCHER_LAUNCH_DRY_RUN");
    int maxFrames = 2;
    if (const char *value = std::getenv("KOTONOHA_LAUNCHER_LAUNCH_FRAMES")) {
      const int parsed = std::atoi(value);
      if (parsed > 0)
        maxFrames = parsed;
    }
    if (dryRun) {
      return launchResponseJson(true, "validate",
                                "Request accepted; graphical launch skipped "
                                "(dry-run)",
                                0, true, path, detected.slug);
    }

    // Headless harnesses need SDL dummy drivers before the first SDL_Init.
    if (auto *env = SDL_GetEnvironment()) {
      if (!SDL_GetEnvironmentVariable(env, "SDL_VIDEODRIVER"))
        SDL_SetEnvironmentVariable(env, "SDL_VIDEODRIVER", "dummy", true);
      if (!SDL_GetEnvironmentVariable(env, "SDL_AUDIODRIVER"))
        SDL_SetEnvironmentVariable(env, "SDL_AUDIODRIVER", "dummy", true);
    }

    SaveEnvironmentGuard environmentGuard;
    Kotonoha::EngineOptions options;
    options.launcher = false;
    options.persistSettings = false;
    options.settingsPath.clear();
    const std::string effectiveSaveRoot =
        !params.customDocumentsDir.empty() ? params.customDocumentsDir
                                           : safeString(request->customSaveRoot);
    KotonohaLauncher::applyPerGameSaveIsolation(
        entry, options, request->splitSavesByGame != 0, effectiveSaveRoot);

    const std::string phase = "runtime construction";
    auto mounted = registry.instantiate(entry, options);
    if (!mounted.runtime)
      return launchResponseJson(false, phase.c_str(), "Runtime is empty", 0,
                                false, path, detected.slug);
    options.assets = mounted.assets;
    Kotonoha::Engine engine(*mounted.runtime, options);
    if (!params.startScenario.empty()) {
      try {
        mounted.runtime->SetState(Kotonoha::GameState::Gameplay);
        engine.debugJumpToNode(params.startScenario);
      } catch (const std::exception &error) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "KotonohaLauncherLaunchGame: startScenario '%s': %s",
                    params.startScenario.c_str(), error.what());
      }
    }

    int frames = 0;
    SDL_AppResult result = SDL_APP_CONTINUE;
    for (; frames < maxFrames; ++frames) {
      result = engine.Iterate();
      if (result != SDL_APP_CONTINUE)
        break;
    }
    if (mounted.shutdown) {
      try {
        mounted.shutdown(*mounted.runtime);
      } catch (const std::exception &error) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "KotonohaLauncherLaunchGame: shutdown: %s", error.what());
      }
    }
    const bool ok = result != SDL_APP_FAILURE;
    const char *message = result == SDL_APP_SUCCESS
                              ? "Game exited cleanly"
                              : ok ? "Game launched"
                                   : "SDL reported a fatal error";
    return launchResponseJson(ok, "iterate", message, frames, false, path,
                              detected.slug);
  } catch (const std::exception &error) {
    return launchResponseJson(false, "exception", error.what(), 0, false,
                              request && request->gamePath ? request->gamePath
                                                           : "",
                              "");
  } catch (...) {
    return launchResponseJson(false, "exception", "Unknown C++ exception", 0,
                              false, "", "");
  }
}

char *KotonohaLauncherDetectPathJson(const char *path, int splitSavesByGame,
                                     const char *customSaveRoot) {
  try {
    const std::string target = safeString(path);
    if (target.empty())
      return dupCString("{}");
    const auto detected =
        KotonohaLauncher::detectKotonohaEngineFromPath(target);
    if (detected.kind == Kotonoha::Filesystem::EngineKind::Unknown)
      return dupCString("{}");
    auto entry = KotonohaLauncher::buildDiscoveredGameEntry(target, detected);
    entry.params =
        KotonohaLauncher::loadGameParamsForProfile(entry.saveProfileId);
    std::ostringstream out;
    appendEntryObject(out, entry, splitSavesByGame != 0,
                      safeString(customSaveRoot));
    return dupCString(out.str());
  } catch (...) {
    return dupCString("{}");
  }
}

char *KotonohaLauncherLoadGameParamsJson(const char *saveProfileId) {
  try {
    const auto params =
        KotonohaLauncher::loadGameParamsForProfile(safeString(saveProfileId));
    std::ostringstream out;
    appendParamsObject(out, params);
    return dupCString(out.str());
  } catch (...) {
    return dupCString("{}");
  }
}

int KotonohaLauncherSaveGameParams(
    const char *saveProfileId, const char *assetOverlayPath,
    const char *translationPath, const char *subtitleStylePath,
    const char *customDocumentsDir, const char *ktrfPath,
    const char *scriptsDir, const char *startScenario,
    const char *rendererBackend, const char *upscalerBackend,
    const char *upscalerPreset, const char *upscalerFactor) {
  try {
    KotonohaLauncher::GameLaunchParameters params;
    params.assetOverlayPath = safeString(assetOverlayPath);
    params.translationPath = safeString(translationPath);
    params.subtitleStylePath = safeString(subtitleStylePath);
    params.customDocumentsDir = safeString(customDocumentsDir);
    params.ktrfPath = safeString(ktrfPath);
    params.scriptsDir = safeString(scriptsDir);
    params.startScenario = safeString(startScenario);
    params.rendererBackend = safeString(rendererBackend);
    params.upscalerBackend = safeString(upscalerBackend);
    params.upscalerPreset = safeString(upscalerPreset);
    params.upscalerFactor = safeString(upscalerFactor);
    return KotonohaLauncher::saveGameParamsForProfile(safeString(saveProfileId),
                                                      params)
               ? 1
               : 0;
  } catch (...) {
    return 0;
  }
}

char *KotonohaLauncherUpscalePresetsJson(void) {
  try {
    const auto &presets = Kotonoha::UpscalePresets();
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < presets.size(); ++i) {
      if (i > 0)
        out << ',';
      out << "{\"index\":" << i << ",\"name\":";
      appendJsonString(out, Kotonoha::UpscalePresetName(presets[i]));
      out << '}';
    }
    out << ']';
    return dupCString(out.str());
  } catch (...) {
    return dupCString("[]");
  }
}

int KotonohaLauncherValidateRoute(const char *ktrfPath, char *infoBuf,
                                  int infoBufSize) {
  auto writeInfo = [&](const std::string &msg) {
    if (infoBuf && infoBufSize > 0) {
      std::snprintf(infoBuf, static_cast<std::size_t>(infoBufSize), "%s",
                    msg.c_str());
    }
  };
  try {
    const std::string path = safeString(ktrfPath);
    if (path.empty()) {
      writeInfo("No route file selected (bundled default will be used).");
      return 1;
    }
#if defined(KOTONOHA_LAUNCH_SCHOOL_DAYS)
    std::string info;
    const bool ok = SchoolDays::Launcher::validateRoute(path, info);
    writeInfo(info);
    return ok ? 1 : 0;
#else
    std::error_code ec;
    if (fs::is_regular_file(fs::path(path), ec)) {
      writeInfo("Route file found.");
      return 1;
    }
    writeInfo("Route file not found.");
    return 0;
#endif
  } catch (const std::exception &e) {
    writeInfo(e.what());
    return 0;
  } catch (...) {
    writeInfo("Unknown error validating route file.");
    return 0;
  }
}

char *KotonohaLauncherDefaultSchoolDaysRoute(void) {
  try {
#if defined(KOTONOHA_LAUNCH_SCHOOL_DAYS)
    return dupCString(
        Kotonoha::Providers::SchoolDaysAssets::defaultRoute().string());
#else
    return dupCString("");
#endif
  } catch (...) {
    return dupCString("");
  }
}

char *KotonohaLauncherResolveSaveDirectory(const char *gamePath,
                                           const char *engineSlug,
                                           const char *saveProfileId,
                                           int splitSavesByGame,
                                           const char *customSaveRoot) {
  try {
    KotonohaLauncher::DiscoveredGameEntry entry;
    entry.path = safeString(gamePath);
    entry.title = fs::path(entry.path).filename().string();
    entry.saveProfileId =
        saveProfileId && saveProfileId[0] != '\0'
            ? std::string(saveProfileId)
            : KotonohaLauncher::sanitizeGameProfileId(entry.path);
    if (engineSlug && engineSlug[0] != '\0') {
      if (const auto *desc =
              KotonohaLauncher::EngineLibraryRegistry::instance().findBySlug(
                  engineSlug)) {
        entry.packageId = desc->packageId;
      }
    }
    return dupCString(KotonohaLauncher::resolveGameSaveDirectory(
        entry, splitSavesByGame != 0, safeString(customSaveRoot)));
  } catch (...) {
    return dupCString("");
  }
}

void KotonohaLauncherFreeString(char *ptr) { std::free(ptr); }

} // extern "C"
