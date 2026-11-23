package org.kotonoha.android;

import android.util.Log;

/**
 * JNI bridge to launcher/src/LauncherBridge.cpp so Android's Material 3
 * LauncherActivity shares 100% of C++ engine detection, parameter persistence,
 * and School Days route validation.
 */
public final class LauncherBridge {
    private static final String TAG = "KotonohaLauncherBridge";
    private static volatile boolean loaded = false;

    private LauncherBridge() {}

    public static synchronized boolean ensureLoaded() {
        if (loaded) return true;
        try {
            String[] libs = new String[]{
                    "c++_shared", "SDL3", "SDL3_ttf", "imgui",
                    "kotonoha_engine", BuildConfig.NATIVE_TARGET
            };
            for (String lib : libs) {
                System.loadLibrary(lib);
            }
            loaded = true;
            return true;
        } catch (UnsatisfiedLinkError error) {
            Log.e(TAG, "Failed to load native launcher libraries", error);
            return false;
        }
    }

    public static native String nativeScanRootsJson(
            String[] roots,
            boolean splitSavesByGame,
            String customSaveRoot);

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

    public static native String nativeValidateRoute(String ktrfPath);
}
