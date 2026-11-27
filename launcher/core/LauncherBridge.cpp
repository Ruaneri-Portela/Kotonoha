// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <KotonohaLauncher/LauncherBridge.h>

#include "PathUtf8.hpp"
#include <KotonohaLauncher/UnifiedLauncher.hpp>

#include <Kotonoha/core/Engine.hpp>
#include <SDL3/SDL.h>
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include <json-c/json.h>
#endif

#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#include <pthread.h>
#endif

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
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

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
const char *jsonString(json_object *root, const char *key) {
  json_object *value = nullptr;
  return root && json_object_object_get_ex(root, key, &value) &&
                 json_object_is_type(value, json_type_string)
             ? json_object_get_string(value)
             : nullptr;
}

bool jsonBool(json_object *root, const char *key) {
  json_object *value = nullptr;
  return root && json_object_object_get_ex(root, key, &value) &&
         json_object_get_boolean(value);
}
#endif

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
  // A real launch is playable only after it reached the iterate phase without
  // a fatal SDL error. A dry-run never iterates; `playable` is then the
  // prediction for the successfully validated request (phase "validated").
  const bool playable =
      ok && (dryRun ? phase == "validated" : phase == "iterate");
  std::ostringstream out;
  out << "{\"ok\":" << (ok ? "true" : "false")
      << ",\"playable\":" << (playable ? "true" : "false") << ",\"phase\":";
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

// Installs SDL dummy drivers only for explicitly requested headless launches.
// Restores the previous environment afterwards so a host that launches a
// headless game and then a graphical one still gets a real window.
class HeadlessDriverGuard {
public:
  explicit HeadlessDriverGuard(bool enabled) {
    if (!enabled)
      return;
    auto *env = SDL_GetEnvironment();
    if (!env)
      return;
    video_ = SDL_GetEnvironmentVariable(env, "SDL_VIDEODRIVER") == nullptr;
    audio_ = SDL_GetEnvironmentVariable(env, "SDL_AUDIODRIVER") == nullptr;
    if (video_)
      SDL_SetEnvironmentVariable(env, "SDL_VIDEODRIVER", "dummy", true);
    if (audio_)
      SDL_SetEnvironmentVariable(env, "SDL_AUDIODRIVER", "dummy", true);
  }

  ~HeadlessDriverGuard() {
    if (!video_ && !audio_)
      return;
    if (auto *env = SDL_GetEnvironment()) {
      if (video_)
        SDL_UnsetEnvironmentVariable(env, "SDL_VIDEODRIVER");
      if (audio_)
        SDL_UnsetEnvironmentVariable(env, "SDL_AUDIODRIVER");
    }
  }

  HeadlessDriverGuard(const HeadlessDriverGuard &) = delete;
  HeadlessDriverGuard &operator=(const HeadlessDriverGuard &) = delete;

private:
  bool video_ = false;
  bool audio_ = false;
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

void KotonohaLauncherScanRootsJsonWithCallback(
    const char *const *roots, int rootCount, int splitSavesByGame,
    const char *customSaveRoot, int maxDepth,
    void (*callback)(const char *gameJson, void *userData), void *userData) {
  try {
    std::vector<std::string> rootList;
    if (roots && rootCount > 0) {
      rootList.reserve(static_cast<std::size_t>(rootCount));
      for (int i = 0; i < rootCount; ++i) {
        if (roots[i] && roots[i][0] != '\0')
          rootList.emplace_back(roots[i]);
      }
    }
    const std::string saveRootStr = safeString(customSaveRoot);
    const bool split = splitSavesByGame != 0;
    KotonohaLauncher::GameFoundCallback found =
        [&](const KotonohaLauncher::DiscoveredGameEntry &entry) {
          if (!callback)
            return;
          std::ostringstream out;
          appendEntryObject(out, entry, split, saveRootStr);
          // O chamador libera com KotonohaLauncherFreeString.
          callback(dupCString(out.str()), userData);
        };
    KotonohaLauncher::scanGameLibraryRoots(rootList, maxDepth, found);
  } catch (...) {
    // Um root invalido nao deve abortar a aplicacao.
  }
}

#if defined(__APPLE__)
namespace {
// SDL's Cocoa video driver refuses to init unless [NSThread isMainThread].
// JNI and P/Invoke callers reach this C ABI on a non-main pthread, so the
// entire (blocking) game loop is dispatched to the native main thread.
struct NativeMainLaunch {
  const KotonohaGameLaunchRequest *request = nullptr;
  char *result = nullptr;
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
};

void runLaunchOnNativeMainThread(void *userData) {
  auto *call = static_cast<NativeMainLaunch *>(userData);
  call->result = KotonohaLauncherLaunchGame(call->request);
  {
    std::lock_guard<std::mutex> lock(call->mutex);
    call->done = true;
  }
  call->cv.notify_all();
}
} // namespace
#endif

char *KotonohaLauncherLaunchGame(const KotonohaGameLaunchRequest *request) {
#if defined(__APPLE__)
  if (!pthread_main_np()) {
    NativeMainLaunch call;
    call.request = request;
    dispatch_async_f(dispatch_get_main_queue(), &call,
                     &runLaunchOnNativeMainThread);
    std::unique_lock<std::mutex> lock(call.mutex);
    call.cv.wait(lock, [&] { return call.done; });
    return call.result;
  }
#endif
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

    // "Test launcher only" (request field) and the env flag both select the
    // dry-run path. Dry-run never touches SDL, so no window is created.
    const bool dryRun = (request->dryRun != 0) ||
                        envFlagEnabled("KOTONOHA_LAUNCHER_LAUNCH_DRY_RUN");
    if (dryRun) {
      return launchResponseJson(true, "validated",
                                "Request validated; graphical launch skipped "
                                "(dry-run)",
                                0, true, path, detected.slug);
    }

    // The graphical launch MUST use the platform's real SDL video/audio
    // drivers so the game window appears. Dummy drivers are installed only
    // when explicitly requested by a CI/test harness.
    const bool headless =
        envFlagEnabled("KOTONOHA_LAUNCHER_LAUNCH_HEADLESS");
    HeadlessDriverGuard driverGuard(headless);

    // A real launch runs until the user quits the game. Only headless launches
    // get a frame cap (KOTONOHA_LAUNCHER_LAUNCH_FRAMES, default 2).
    int maxFrames = 0;
    if (headless) {
      maxFrames = 2;
      if (const char *value =
              std::getenv("KOTONOHA_LAUNCHER_LAUNCH_FRAMES")) {
        const int parsed = std::atoi(value);
        if (parsed > 0)
          maxFrames = parsed;
      }
    }

    {
      auto *sdlEnv = SDL_GetEnvironment();
      const char *sdlVideo = sdlEnv
                                 ? SDL_GetEnvironmentVariable(
                                       sdlEnv, "SDL_VIDEODRIVER")
                                 : nullptr;
      const char *sdlAudio = sdlEnv
                                 ? SDL_GetEnvironmentVariable(
                                       sdlEnv, "SDL_AUDIODRIVER")
                                 : nullptr;
      const char *libcVideo = std::getenv("SDL_VIDEODRIVER");
      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                  "LaunchGame diag: mainThread=%s nativeMain=%d "
                  "SDL_VIDEODRIVER='%s' SDL_AUDIODRIVER='%s' libcVideo='%s' "
                  "headless=%d dryRun=%d",
                  SDL_IsMainThread() ? "yes" : "no",
#if defined(__APPLE__)
                  pthread_main_np(),
#else
                  0,
#endif
                  sdlVideo ? sdlVideo : "(unset)",
                  sdlAudio ? sdlAudio : "(unset)",
                  libcVideo ? libcVideo : "(unset)", headless ? 1 : 0,
                  dryRun ? 1 : 0);
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
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "LaunchGame diag: video driver='%s' audio driver='%s'",
                SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver()
                                            : "(none)",
                SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver()
                                            : "(none)");
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
    // maxFrames == 0 means "run until the user closes the window".
    // The engine relies on the SDL main-callback host to pump events; a plain
    // C ABI launch must do it itself or the Cocoa window never gets mapped
    // (and SDL_APP_QUIT from closing the window never arrives).
    for (; maxFrames == 0 || frames < maxFrames; ++frames) {
      SDL_PumpEvents();
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        const SDL_AppResult eventResult = engine.Event(&event);
        if (eventResult != SDL_APP_CONTINUE) {
          result = eventResult;
          break;
        }
      }
      if (result != SDL_APP_CONTINUE)
        break;
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
    const auto path = KotonohaLauncher::detail::pathFromUtf8(entry.path);
    entry.title = KotonohaLauncher::detail::pathUtf8(path.filename());
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

char *KotonohaLauncherLaunchGameJson(const char *requestJson) {
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
  (void)requestJson;
  return launchResponseJson(false, "error", "JSON launch is unavailable", 0,
                            false, "", "");
#else
  if (!requestJson || !*requestJson)
    return launchResponseJson(false, "error", "Empty launch request", 0,
                              false, "", "");
  json_object *root =
      json_tokener_parse(requestJson);
  if (!root)
    return launchResponseJson(false, "error", "Invalid launch JSON", 0,
                              false, "", "");
  KotonohaGameLaunchRequest request{};
  request.gamePath = jsonString(root, "GamePath");
  request.engineSlug = jsonString(root, "EngineSlug");
  request.saveProfileId = jsonString(root, "SaveProfileId");
  request.splitSavesByGame = jsonBool(root, "SplitSavesByGame") ? 1 : 0;
  request.customSaveRoot = jsonString(root, "CustomSaveRoot");
  request.assetOverlayPath = jsonString(root, "AssetOverlayPath");
  request.translationPath = jsonString(root, "TranslationPath");
  request.subtitleStylePath = jsonString(root, "SubtitleStylePath");
  request.customDocumentsDir = jsonString(root, "CustomDocumentsDir");
  request.ktrfPath = jsonString(root, "KtrfPath");
  request.scriptsDir = jsonString(root, "ScriptsDir");
  request.startScenario = jsonString(root, "StartScenario");
  request.rendererBackend = jsonString(root, "RendererBackend");
  request.upscalerBackend = jsonString(root, "UpscalerBackend");
  request.upscalerPreset = jsonString(root, "UpscalerPreset");
  request.upscalerFactor = jsonString(root, "UpscalerFactor");
  request.dryRun = jsonBool(root, "DryRun") ? 1 : 0;
  char *result = KotonohaLauncherLaunchGame(&request);
  json_object_put(root);
  return result;
#endif
}

#if defined(_WIN32)
void CALLBACK KotonohaLauncherRunDll(void *, void *, char *commandLine, int) {
  // O "fork" do Windows: o RUNDLL32 hospeda o jogo na propria main thread.
  const char *request = nullptr;
  if (const char *env = std::getenv("KOTONOHA_LAUNCH_REQUEST_JSON"))
    if (*env)
      request = env;
  if (!request && commandLine && *commandLine)
    request = commandLine;
  if (!request)
    return;
  char *result = KotonohaLauncherLaunchGameJson(request);
  if (result) {
    std::fprintf(stderr, "[Kotonoha] game result: %s\n", result);
    KotonohaLauncherFreeString(result);
  }
}
#endif

} // extern "C"
