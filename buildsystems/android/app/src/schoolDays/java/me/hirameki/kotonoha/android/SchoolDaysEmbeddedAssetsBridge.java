package me.hirameki.kotonoha.android;

import android.util.Log;

/** Read approved embedded resources without starting SDL or extracting files. */
final class SchoolDaysEmbeddedAssetsBridge {
    private static final String TAG = "SchoolDaysEmbeddedAssets";

    private SchoolDaysEmbeddedAssetsBridge() {}

    static byte[] read(String logicalPath) {
        if (!BuildConfig.SCHOOL_DAYS_EMBEDDED_ASSETS) return null;
        // The universal LauncherBridge loads KotonohaLauncherCore, which is not
        // built into the standalone School Days APK. Use the native game loader
        // order instead; loading libraries does not start SDL gameplay.
        try {
            for (String library : GameActivity.gameLibraries()) {
                System.loadLibrary(library);
            }
            byte[] bytes = nativeRead(logicalPath);
            if (bytes == null) Log.w(TAG, "Missing embedded resource: " + logicalPath);
            return bytes;
        } catch (UnsatisfiedLinkError | RuntimeException error) {
            Log.e(TAG, "Cannot read embedded resource: " + logicalPath, error);
            return null;
        }
    }

    private static native byte[] nativeRead(String logicalPath);
}
