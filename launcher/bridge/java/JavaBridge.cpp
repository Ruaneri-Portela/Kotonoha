// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Java/JNI bridge for KotonohaLauncherCore. This is the only translation unit
// exporting JNI symbols; it calls the shared C ABI declared in
// <KotonohaLauncher/LauncherBridge.h>. CMake compiles it into the core on every
// native platform (Android plus macOS/Linux/Windows), so a Java host loads the
// symbols directly from libKotonohaLauncherCore.so/.dylib/.dll with no shim.
//
// Two exported symbol sets are provided:
//   * me.hirameki.kotonoha.android.LauncherBridge -- the shipped Android app.
//   * me.hirameki.kotonoha.launcher.KotonohaLauncherBridge -- the reusable bridge in
//     launcher/bridge/java/ for generic Java hosts.
#include <KotonohaLauncher/LauncherBridge.h>

#include <cstddef>
#include <string>
#include <vector>

// Android always has jni.h from the NDK; desktop builds opt in via the CMake
// KOTONOHA_JNI_BINDINGS option (default ON when a JDK is found).
#if defined(__ANDROID__) || defined(KOTONOHA_JNI_BINDINGS)
#include <jni.h>

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

jstring newOwnedString(JNIEnv *env, char *value, const char *fallback) {
  jstring result = env->NewStringUTF(value ? value : fallback);
  KotonohaLauncherFreeString(value);
  return result;
}

jstring jniScanRoots(JNIEnv *env, jobjectArray roots, jboolean splitSavesByGame,
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
  return newOwnedString(env, json, "[]");
}

jstring jniScanRootsWithDepth(JNIEnv *env, jobjectArray roots,
                              jboolean splitSavesByGame,
                              jstring customSaveRoot, jint maxDepth) {
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
  std::vector<std::string> entries;
  auto callback = [](const char *gameJson, void *userData) {
    if (!gameJson) return;
    auto *list = static_cast<std::vector<std::string> *>(userData);
    list->emplace_back(gameJson);
    KotonohaLauncherFreeString(const_cast<char *>(gameJson));
  };
  KotonohaLauncherScanRootsJsonWithCallback(
      ptrs.empty() ? nullptr : ptrs.data(), static_cast<int>(ptrs.size()),
      splitSavesByGame ? 1 : 0, saveRoot.c_str(), static_cast<int>(maxDepth),
      callback, &entries);
  std::string combined = "[";
  for (std::size_t i = 0; i < entries.size(); ++i) {
    if (i > 0) combined += ',';
    combined += entries[i];
  }
  combined += ']';
  return env->NewStringUTF(combined.c_str());
}

jstring jniDetectPath(JNIEnv *env, jstring path, jboolean splitSavesByGame,
                      jstring customSaveRoot) {
  const std::string target = jstringToStd(env, path);
  const std::string saveRoot = jstringToStd(env, customSaveRoot);
  char *json = KotonohaLauncherDetectPathJson(
      target.c_str(), splitSavesByGame ? 1 : 0, saveRoot.c_str());
  return newOwnedString(env, json, "{}");
}

jstring jniLoadGameParamsJson(JNIEnv *env, jstring saveProfileId) {
  const std::string profile = jstringToStd(env, saveProfileId);
  char *json = KotonohaLauncherLoadGameParamsJson(profile.c_str());
  return newOwnedString(env, json, "{}");
}

jboolean jniSaveGameParams(
    JNIEnv *env, jstring saveProfileId, jstring assetOverlayPath,
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

jstring jniUpscalePresets(JNIEnv *env) {
  return newOwnedString(env, KotonohaLauncherUpscalePresetsJson(), "[]");
}

jstring jniDefaultSchoolDaysRoute(JNIEnv *env) {
  return newOwnedString(env, KotonohaLauncherDefaultSchoolDaysRoute(), "");
}

jstring jniResolveSaveDirectory(JNIEnv *env, jstring gamePath,
                                 jstring engineSlug, jstring saveProfileId,
                                 jboolean splitSavesByGame,
                                 jstring customSaveRoot) {
  const std::string path = jstringToStd(env, gamePath);
  const std::string slug = jstringToStd(env, engineSlug);
  const std::string profile = jstringToStd(env, saveProfileId);
  const std::string saveRoot = jstringToStd(env, customSaveRoot);
  char *dir = KotonohaLauncherResolveSaveDirectory(
      path.c_str(), slug.c_str(), profile.c_str(), splitSavesByGame ? 1 : 0,
      saveRoot.c_str());
  return newOwnedString(env, dir, "");
}

jstring jniValidateRoute(JNIEnv *env, jstring ktrfPath) {
  const std::string path = jstringToStd(env, ktrfPath);
  char buf[512] = {};
  const int ok = KotonohaLauncherValidateRoute(path.c_str(), buf, sizeof(buf));
  const std::string prefix = ok ? "OK|" : "ERR|";
  return env->NewStringUTF((prefix + buf).c_str());
}

jstring jniLaunchGame(
    JNIEnv *env, jstring gamePath, jstring engineSlug, jstring saveProfileId,
    jboolean splitSavesByGame, jstring customSaveRoot,
    jstring assetOverlayPath, jstring translationPath,
    jstring subtitleStylePath, jstring customDocumentsDir, jstring ktrfPath,
    jstring scriptsDir, jstring startScenario, jstring rendererBackend,
    jstring upscalerBackend, jstring upscalerPreset, jstring upscalerFactor,
    jboolean dryRun) {
  const std::string path = jstringToStd(env, gamePath);
  const std::string slug = jstringToStd(env, engineSlug);
  const std::string profile = jstringToStd(env, saveProfileId);
  const std::string saveRoot = jstringToStd(env, customSaveRoot);
  const std::string overlay = jstringToStd(env, assetOverlayPath);
  const std::string translation = jstringToStd(env, translationPath);
  const std::string subtitle = jstringToStd(env, subtitleStylePath);
  const std::string documents = jstringToStd(env, customDocumentsDir);
  const std::string ktrf = jstringToStd(env, ktrfPath);
  const std::string scripts = jstringToStd(env, scriptsDir);
  const std::string start = jstringToStd(env, startScenario);
  const std::string renderer = jstringToStd(env, rendererBackend);
  const std::string upscaler = jstringToStd(env, upscalerBackend);
  const std::string preset = jstringToStd(env, upscalerPreset);
  const std::string factor = jstringToStd(env, upscalerFactor);
  KotonohaGameLaunchRequest request{};
  request.gamePath = path.c_str();
  request.engineSlug = slug.c_str();
  request.saveProfileId = profile.c_str();
  request.splitSavesByGame = splitSavesByGame ? 1 : 0;
  request.customSaveRoot = saveRoot.c_str();
  request.assetOverlayPath = overlay.c_str();
  request.translationPath = translation.c_str();
  request.subtitleStylePath = subtitle.c_str();
  request.customDocumentsDir = documents.c_str();
  request.ktrfPath = ktrf.c_str();
  request.scriptsDir = scripts.c_str();
  request.startScenario = start.c_str();
  request.rendererBackend = renderer.c_str();
  request.upscalerBackend = upscaler.c_str();
  request.upscalerPreset = preset.c_str();
  request.upscalerFactor = factor.c_str();
  request.dryRun = dryRun ? 1 : 0;
  char *json = KotonohaLauncherLaunchGame(&request);
  return newOwnedString(env, json, "{}");
}

} // namespace

extern "C" {

// ---- Shipped Android app: me.hirameki.kotonoha.android.LauncherBridge ----

JNIEXPORT jstring JNICALL
Java_me_hirameki_kotonoha_android_LauncherBridge_nativeScanRootsJson(
    JNIEnv *env, jclass, jobjectArray roots, jboolean splitSavesByGame,
    jstring customSaveRoot) {
  return jniScanRoots(env, roots, splitSavesByGame, customSaveRoot);
}

JNIEXPORT jstring JNICALL
Java_me_hirameki_kotonoha_android_LauncherBridge_nativeScanRootsJsonWithDepth(
    JNIEnv *env, jclass, jobjectArray roots, jboolean splitSavesByGame,
    jstring customSaveRoot, jint maxDepth) {
  return jniScanRootsWithDepth(env, roots, splitSavesByGame, customSaveRoot,
                               maxDepth);
}

JNIEXPORT jstring JNICALL
Java_me_hirameki_kotonoha_android_LauncherBridge_nativeDetectPathJson(
    JNIEnv *env, jclass, jstring path, jboolean splitSavesByGame,
    jstring customSaveRoot) {
  return jniDetectPath(env, path, splitSavesByGame, customSaveRoot);
}

JNIEXPORT jstring JNICALL
Java_me_hirameki_kotonoha_android_LauncherBridge_nativeLoadGameParamsJson(
    JNIEnv *env, jclass, jstring saveProfileId) {
  return jniLoadGameParamsJson(env, saveProfileId);
}

JNIEXPORT jboolean JNICALL
Java_me_hirameki_kotonoha_android_LauncherBridge_nativeSaveGameParams(
    JNIEnv *env, jclass, jstring saveProfileId, jstring assetOverlayPath,
    jstring translationPath, jstring subtitleStylePath,
    jstring customDocumentsDir, jstring ktrfPath, jstring scriptsDir,
    jstring startScenario, jstring rendererBackend, jstring upscalerBackend,
    jstring upscalerPreset, jstring upscalerFactor) {
  return jniSaveGameParams(
      env, saveProfileId, assetOverlayPath, translationPath, subtitleStylePath,
      customDocumentsDir, ktrfPath, scriptsDir, startScenario, rendererBackend,
      upscalerBackend, upscalerPreset, upscalerFactor);
}

JNIEXPORT jstring JNICALL
Java_me_hirameki_kotonoha_android_LauncherBridge_nativeUpscalePresetsJson(
    JNIEnv *env, jclass) {
  return jniUpscalePresets(env);
}

JNIEXPORT jstring JNICALL
Java_me_hirameki_kotonoha_android_LauncherBridge_nativeDefaultSchoolDaysRoute(
    JNIEnv *env, jclass) {
  return jniDefaultSchoolDaysRoute(env);
}

JNIEXPORT jstring JNICALL
Java_me_hirameki_kotonoha_android_LauncherBridge_nativeValidateRoute(
    JNIEnv *env, jclass, jstring ktrfPath) {
  return jniValidateRoute(env, ktrfPath);
}

JNIEXPORT jstring JNICALL
Java_me_hirameki_kotonoha_android_LauncherBridge_nativeLaunchJson(
    JNIEnv *env, jclass, jstring gamePath, jstring engineSlug,
    jstring saveProfileId, jboolean splitSavesByGame, jstring customSaveRoot,
    jstring assetOverlayPath, jstring translationPath,
    jstring subtitleStylePath, jstring customDocumentsDir, jstring ktrfPath,
    jstring scriptsDir, jstring startScenario, jstring rendererBackend,
    jstring upscalerBackend, jstring upscalerPreset, jstring upscalerFactor,
    jboolean dryRun) {
  return jniLaunchGame(env, gamePath, engineSlug, saveProfileId,
                       splitSavesByGame, customSaveRoot, assetOverlayPath,
                       translationPath, subtitleStylePath, customDocumentsDir,
                       ktrfPath, scriptsDir, startScenario, rendererBackend,
                       upscalerBackend, upscalerPreset, upscalerFactor,
                       dryRun);
}

// ---- Reusable bridge: me.hirameki.kotonoha.launcher.KotonohaLauncherBridge ----

JNIEXPORT jstring JNICALL
Java_me_hirameki_kotonoha_launcher_KotonohaLauncherBridge_nativeScanRootsJson(
    JNIEnv *env, jclass, jobjectArray roots, jboolean splitSavesByGame,
    jstring customSaveRoot) {
  return jniScanRoots(env, roots, splitSavesByGame, customSaveRoot);
}

JNIEXPORT jstring JNICALL
Java_me_hirameki_kotonoha_launcher_KotonohaLauncherBridge_nativeScanRootsJsonWithDepth(
    JNIEnv *env, jclass, jobjectArray roots, jboolean splitSavesByGame,
    jstring customSaveRoot, jint maxDepth) {
  return jniScanRootsWithDepth(env, roots, splitSavesByGame, customSaveRoot,
                               maxDepth);
}

JNIEXPORT jstring JNICALL
Java_me_hirameki_kotonoha_launcher_KotonohaLauncherBridge_nativeDetectPathJson(
    JNIEnv *env, jclass, jstring path, jboolean splitSavesByGame,
    jstring customSaveRoot) {
  return jniDetectPath(env, path, splitSavesByGame, customSaveRoot);
}

JNIEXPORT jstring JNICALL
Java_me_hirameki_kotonoha_launcher_KotonohaLauncherBridge_nativeLoadGameParamsJson(
    JNIEnv *env, jclass, jstring saveProfileId) {
  return jniLoadGameParamsJson(env, saveProfileId);
}

JNIEXPORT jboolean JNICALL
Java_me_hirameki_kotonoha_launcher_KotonohaLauncherBridge_nativeSaveGameParams(
    JNIEnv *env, jclass, jstring saveProfileId, jstring assetOverlayPath,
    jstring translationPath, jstring subtitleStylePath,
    jstring customDocumentsDir, jstring ktrfPath, jstring scriptsDir,
    jstring startScenario, jstring rendererBackend, jstring upscalerBackend,
    jstring upscalerPreset, jstring upscalerFactor) {
  return jniSaveGameParams(
      env, saveProfileId, assetOverlayPath, translationPath, subtitleStylePath,
      customDocumentsDir, ktrfPath, scriptsDir, startScenario, rendererBackend,
      upscalerBackend, upscalerPreset, upscalerFactor);
}

JNIEXPORT jstring JNICALL
Java_me_hirameki_kotonoha_launcher_KotonohaLauncherBridge_nativeUpscalePresetsJson(
    JNIEnv *env, jclass) {
  return jniUpscalePresets(env);
}

JNIEXPORT jstring JNICALL
Java_me_hirameki_kotonoha_launcher_KotonohaLauncherBridge_nativeDefaultSchoolDaysRoute(
    JNIEnv *env, jclass) {
  return jniDefaultSchoolDaysRoute(env);
}

JNIEXPORT jstring JNICALL
Java_me_hirameki_kotonoha_launcher_KotonohaLauncherBridge_nativeResolveSaveDirectory(
    JNIEnv *env, jclass, jstring gamePath, jstring engineSlug,
    jstring saveProfileId, jboolean splitSavesByGame, jstring customSaveRoot) {
  return jniResolveSaveDirectory(env, gamePath, engineSlug, saveProfileId,
                                 splitSavesByGame, customSaveRoot);
}

JNIEXPORT jstring JNICALL
Java_me_hirameki_kotonoha_launcher_KotonohaLauncherBridge_nativeValidateRoute(
    JNIEnv *env, jclass, jstring ktrfPath) {
  return jniValidateRoute(env, ktrfPath);
}

JNIEXPORT jstring JNICALL
Java_me_hirameki_kotonoha_launcher_KotonohaLauncherBridge_nativeLaunchJson(
    JNIEnv *env, jclass, jstring gamePath, jstring engineSlug,
    jstring saveProfileId, jboolean splitSavesByGame, jstring customSaveRoot,
    jstring assetOverlayPath, jstring translationPath,
    jstring subtitleStylePath, jstring customDocumentsDir, jstring ktrfPath,
    jstring scriptsDir, jstring startScenario, jstring rendererBackend,
    jstring upscalerBackend, jstring upscalerPreset, jstring upscalerFactor,
    jboolean dryRun) {
  return jniLaunchGame(env, gamePath, engineSlug, saveProfileId,
                       splitSavesByGame, customSaveRoot, assetOverlayPath,
                       translationPath, subtitleStylePath, customDocumentsDir,
                       ktrfPath, scriptsDir, startScenario, rendererBackend,
                       upscalerBackend, upscalerPreset, upscalerFactor,
                       dryRun);
}

} // extern "C"
#endif
