// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
package me.hirameki.kotonoha.launcher;

/**
 * Reusable Java facade over the KotonohaLauncherCore C ABI.
 *
 * <p>The native side is {@code launcher/bridge/java/JavaBridge.cpp}, compiled
 * into {@code libKotonohaLauncherCore.so} by the Android/CMake build. Every
 * method mirrors the C entry point in {@code LauncherBridge.h}; strings are
 * returned as UTF-8 Java strings and native buffers are released in JNI.</p>
 *
 * <p>The shipped Android app keeps its legacy
 * {@code me.hirameki.kotonoha.android.LauncherBridge} class for compatibility; both
 * classes are exported by the same native library.</p>
 */
public final class KotonohaLauncherBridge {
    private static volatile boolean loaded = false;

    private KotonohaLauncherBridge() {}

    /** Load the shared core once. Safe to call from any thread. */
    public static synchronized boolean ensureLoaded() {
        if (loaded) return true;
        try {
            System.loadLibrary("c++_shared");
            System.loadLibrary("KotonohaEngine");
            System.loadLibrary("KotonohaLauncherCore");
            loaded = true;
            return true;
        } catch (UnsatisfiedLinkError error) {
            return false;
        }
    }

    public static native String nativeScanRootsJson(
            String[] roots, boolean splitSavesByGame, String customSaveRoot);

    public static native String nativeDetectPathJson(
            String path, boolean splitSavesByGame, String customSaveRoot);

    public static native String nativeLoadGameParamsJson(String saveProfileId);

    public static native boolean nativeSaveGameParams(
            String saveProfileId,
            String assetOverlayPath,
            String translationPath,
            String subtitleStylePath,
            String customDocumentsDir,
            String ktrfPath,
            String scriptsDir,
            String startScenario,
            String rendererBackend,
            String upscalerBackend,
            String upscalerPreset,
            String upscalerFactor);

    public static native String nativeUpscalePresetsJson();

    public static native String nativeDefaultSchoolDaysRoute();

    public static native String nativeResolveSaveDirectory(
            String gamePath,
            String engineSlug,
            String saveProfileId,
            boolean splitSavesByGame,
            String customSaveRoot);

    /** Returns "OK|&lt;diagnostics&gt;" or "ERR|&lt;diagnostics&gt;". */
    public static native String nativeValidateRoute(String ktrfPath);

    /**
     * Launches one game in-process and returns a JSON result with ok, phase,
     * message, frames and dryRun fields. Empty strings mean "use defaults".
     */
    public static native String nativeLaunchJson(
            String gamePath, String engineSlug, String saveProfileId,
            boolean splitSavesByGame, String customSaveRoot,
            String assetOverlayPath, String translationPath,
            String subtitleStylePath, String customDocumentsDir,
            String ktrfPath, String scriptsDir, String startScenario,
            String rendererBackend, String upscalerBackend,
            String upscalerPreset, String upscalerFactor);
}
