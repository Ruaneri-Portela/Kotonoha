package org.kotonoha.android;

import android.util.Log;

/** Read approved embedded resources without starting SDL or extracting files. */
final class SchoolDaysEmbeddedAssetsBridge {
    private static final String TAG = "SchoolDaysEmbeddedAssets";

    private SchoolDaysEmbeddedAssetsBridge() {}

    static byte[] read(String logicalPath) {
        if (!BuildConfig.SCHOOL_DAYS_EMBEDDED_ASSETS) return null;
        // Reuse the existing loader and the same library order as GameActivity.
        // JNI registration does not create an SDL window or initialize gameplay.
        if (!LauncherBridge.ensureLoaded()) return null;
        try {
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
