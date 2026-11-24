// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Local copy of launcher/bridge/java/KotonohaLauncherBridge.java adapted for a
// macOS host: the JNI symbols are exported by the real
// libKotonohaLauncherCore.dylib, so there is no JNI shim. NativeLibraries
// preloads the @rpath dependencies before System.load(core).
package me.hirameki.kotonoha.launcher;

/** Reusable Java facade over the KotonohaLauncherCore JNI exports. */
public final class KotonohaLauncherBridge {
    private static volatile boolean loaded = false;
    private static volatile Throwable loadError = null;

    private KotonohaLauncherBridge() {}

    /** Loads the native core exactly once. Safe to call from any thread. */
    public static synchronized boolean ensureLoaded() {
        if (loaded) return true;
        try {
            NativeLibraries.loadCore();
            loaded = true;
            loadError = null;
            return true;
        } catch (Throwable error) {
            loadError = error;
            return false;
        }
    }

    public static Throwable getLoadError() {
        return loadError;
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
     * Launches one game in-process. Returns JSON with ok, phase, message,
     * message, playable, frames and dryRun fields. Set dryRun=true to validate
     * without starting SDL/gameplay ("Test launcher only"). Empty strings mean
     * "use defaults".
     */
    public static native String nativeLaunchJson(
            String gamePath, String engineSlug, String saveProfileId,
            boolean splitSavesByGame, String customSaveRoot,
            String assetOverlayPath, String translationPath,
            String subtitleStylePath, String customDocumentsDir,
            String ktrfPath, String scriptsDir, String startScenario,
            String rendererBackend, String upscalerBackend,
            String upscalerPreset, String upscalerFactor, boolean dryRun);
}
