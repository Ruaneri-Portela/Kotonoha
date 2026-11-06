package org.kotonoha.android;

import android.content.Context;
import android.os.Build;
import android.view.Display;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

import java.io.File;

public final class GameActivity extends SDLActivity {
    private int kotonohaPreferredModeId;
    private float kotonohaPreferredRefreshRate;

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new SDLSurface(context) {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                super.surfaceCreated(holder);
                // A resumed Activity can receive a new Surface without another
                // onResume or focus callback. The physical frame-rate request
                // belongs to this Surface and must be applied again.
                runOnUiThread(GameActivity.this::applyKotonohaDisplayPreference);
            }
        };
    }

    public int[] getKotonohaDisplayModes() {
        final Display.Mode[] modes = getWindowManager().getDefaultDisplay().getSupportedModes();
        final int[] packed = new int[modes.length * 4];
        for (int i = 0; i < modes.length; ++i) {
            final Display.Mode mode = modes[i];
            final int base = i * 4;
            packed[base] = mode.getModeId();
            packed[base + 1] = mode.getPhysicalWidth();
            packed[base + 2] = mode.getPhysicalHeight();
            packed[base + 3] = Math.round(mode.getRefreshRate() * 1000.0f);
        }
        return packed;
    }

    public int getKotonohaCurrentDisplayModeId() {
        return getWindowManager().getDefaultDisplay().getMode().getModeId();
    }

    public boolean setKotonohaPreferredDisplayMode(int modeId) {
        if (modeId <= 0) return false;
        float targetRefreshRate = 0.0f;
        boolean found = false;
        for (Display.Mode mode : getWindowManager().getDefaultDisplay().getSupportedModes()) {
            if (mode.getModeId() == modeId) {
                targetRefreshRate = mode.getRefreshRate();
                found = true;
                break;
            }
        }
        if (!found) return false;

        final int requestedMode = modeId;
        final float requestedRefreshRate = targetRefreshRate;
        kotonohaPreferredModeId = requestedMode;
        kotonohaPreferredRefreshRate = requestedRefreshRate;
        runOnUiThread(() -> {
            applyKotonohaDisplayPreference();
        });
        return true;
    }

    private void applyKotonohaDisplayPreference() {
        if (kotonohaPreferredModeId <= 0) return;
        final WindowManager.LayoutParams attributes = getWindow().getAttributes();
        if (attributes.preferredDisplayModeId != kotonohaPreferredModeId ||
                Math.abs(attributes.preferredRefreshRate - kotonohaPreferredRefreshRate) > 0.01f) {
            attributes.preferredDisplayModeId = kotonohaPreferredModeId;
            attributes.preferredRefreshRate = kotonohaPreferredRefreshRate;
            getWindow().setAttributes(attributes);
        }
        if (mSurface != null && mSurface.getHolder() != null) {
            final Surface surface = mSurface.getHolder().getSurface();
            if (surface != null && surface.isValid()) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    surface.setFrameRate(kotonohaPreferredRefreshRate,
                            Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE,
                            Surface.CHANGE_FRAME_RATE_ALWAYS);
                } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    surface.setFrameRate(kotonohaPreferredRefreshRate,
                            Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
                }
            }
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        runOnUiThread(this::applyKotonohaDisplayPreference);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) runOnUiThread(this::applyKotonohaDisplayPreference);
    }

    @Override
    protected String[] getLibraries() {
        return new String[]{"c++_shared", "SDL3",
                            "SDL3_ttf", "kotonoha_imgui", "kotonoha_engine", BuildConfig.NATIVE_TARGET};
    }

    public String getKotonohaResourceDirectory() {
        File files = getFilesDir();
        if (!new File(files, ".kotonoha-assets.sha256").isFile()) {
            throw new IllegalStateException("Packaged assets have not been prepared");
        }
        return files.getAbsolutePath();
    }

    public String getKotonohaPackageIdentifier() {
        return getPackageName();
    }

    public String getKotonohaGameAssetDirectory() {
        return new File(getDataDir(), "game_assets").getAbsolutePath();
    }

    public String getKotonohaDataDirectory() {
        File obb = getObbDir();
        return obb == null ? "" : obb.getAbsolutePath();
    }

    public String getKotonohaDocumentsDirectory() {
        return directory(getExternalFilesDir(null), "Android data");
    }

    private static String directory(File directory, String name) {
        if (directory == null || (!directory.isDirectory() && !directory.mkdirs())) {
            throw new IllegalStateException("Cannot access " + name + " directory");
        }
        return directory.getAbsolutePath();
    }
}
