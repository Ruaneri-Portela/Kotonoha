// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <KotonohaLauncher/LauncherBridge.h>
#include <KotonohaLauncher/UnifiedLauncher.hpp>

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

#if defined(__ANDROID__)
#include <jni.h>
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

#if defined(__ANDROID__)
namespace {
std::string jstringToStd(JNIEnv *env, jstring jstr) {
  if (!jstr)
    return {};
  const char *chars = env->GetStringUTFChars(jstr, nullptr);
  if (!chars)
    return {};
  std::string out(chars);
  env->ReleaseStringUTFChars(jstr, chars);
  return out;
}
} // namespace

extern "C" {

JNIEXPORT jstring JNICALL
Java_org_kotonoha_android_LauncherBridge_nativeScanRootsJson(
    JNIEnv *env, jclass, jobjectArray roots, jboolean splitSavesByGame,
    jstring customSaveRoot) {
  std::vector<std::string> storage;
  std::vector<const char *> ptrs;
  if (roots) {
    const jsize count = env->GetArrayLength(roots);
    storage.reserve(static_cast<std::size_t>(count));
    ptrs.reserve(static_cast<std::size_t>(count));
    for (jsize i = 0; i < count; ++i) {
      auto item = static_cast<jstring>(env->GetObjectArrayElement(roots, i));
      storage.push_back(jstringToStd(env, item));
      env->DeleteLocalRef(item);
    }
    for (const auto &s : storage)
      ptrs.push_back(s.c_str());
  }
  const std::string saveRoot = jstringToStd(env, customSaveRoot);
  char *json = KotonohaLauncherScanRootsJson(
      ptrs.empty() ? nullptr : ptrs.data(), static_cast<int>(ptrs.size()),
      splitSavesByGame ? 1 : 0, saveRoot.c_str());
  jstring result = env->NewStringUTF(json ? json : "[]");
  KotonohaLauncherFreeString(json);
  return result;
}

JNIEXPORT jstring JNICALL
Java_org_kotonoha_android_LauncherBridge_nativeLoadGameParamsJson(
    JNIEnv *env, jclass, jstring saveProfileId) {
  const std::string profile = jstringToStd(env, saveProfileId);
  char *json = KotonohaLauncherLoadGameParamsJson(profile.c_str());
  jstring result = env->NewStringUTF(json ? json : "{}");
  KotonohaLauncherFreeString(json);
  return result;
}

JNIEXPORT jboolean JNICALL
Java_org_kotonoha_android_LauncherBridge_nativeSaveGameParams(
    JNIEnv *env, jclass, jstring saveProfileId, jstring assetOverlayPath,
    jstring translationPath, jstring subtitleStylePath,
    jstring customDocumentsDir, jstring ktrfPath, jstring scriptsDir,
    jstring startScenario, jstring rendererBackend, jstring upscalerBackend,
    jstring upscalerPreset, jstring upscalerFactor) {
  const std::string profile = jstringToStd(env, saveProfileId);
  const std::string overlay = jstringToStd(env, assetOverlayPath);
  const std::string translation = jstringToStd(env, translationPath);
  const std::string subtitleStyle = jstringToStd(env, subtitleStylePath);
  const std::string customDocs = jstringToStd(env, customDocumentsDir);
  const std::string ktrf = jstringToStd(env, ktrfPath);
  const std::string scripts = jstringToStd(env, scriptsDir);
  const std::string start = jstringToStd(env, startScenario);
  const std::string renderer = jstringToStd(env, rendererBackend);
  const std::string upscaler = jstringToStd(env, upscalerBackend);
  const std::string preset = jstringToStd(env, upscalerPreset);
  const std::string factor = jstringToStd(env, upscalerFactor);
  const int ok = KotonohaLauncherSaveGameParams(
      profile.c_str(), overlay.c_str(), translation.c_str(),
      subtitleStyle.c_str(), customDocs.c_str(), ktrf.c_str(), scripts.c_str(),
      start.c_str(), renderer.c_str(), upscaler.c_str(), preset.c_str(),
      factor.c_str());
  return ok != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_org_kotonoha_android_LauncherBridge_nativeUpscalePresetsJson(JNIEnv *env,
                                                                  jclass) {
  char *json = KotonohaLauncherUpscalePresetsJson();
  jstring result = env->NewStringUTF(json ? json : "[]");
  KotonohaLauncherFreeString(json);
  return result;
}

JNIEXPORT jstring JNICALL
Java_org_kotonoha_android_LauncherBridge_nativeDefaultSchoolDaysRoute(
    JNIEnv *env, jclass) {
  char *route = KotonohaLauncherDefaultSchoolDaysRoute();
  jstring result = env->NewStringUTF(route ? route : "");
  KotonohaLauncherFreeString(route);
  return result;
}

JNIEXPORT jstring JNICALL
Java_org_kotonoha_android_LauncherBridge_nativeValidateRoute(JNIEnv *env,
                                                             jclass,
                                                             jstring ktrfPath) {
  const std::string path = jstringToStd(env, ktrfPath);
  char buf[512] = {};
  const int ok = KotonohaLauncherValidateRoute(path.c_str(), buf, sizeof(buf));
  const std::string prefix = ok ? "OK|" : "ERR|";
  return env->NewStringUTF((prefix + buf).c_str());
}

} // extern "C"
#endif
