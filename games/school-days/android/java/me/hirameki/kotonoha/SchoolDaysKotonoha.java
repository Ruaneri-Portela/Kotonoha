package me.hirameki.kotonoha;

import android.os.Build;
import android.view.Display;
import android.view.Surface;
import android.view.WindowManager;

import java.io.File;

public class SchoolDaysKotonoha extends Kotonoha {

    public int[] getKotonohaDisplayModes() {
        final Display display = getWindowManager().getDefaultDisplay();
        final Display.Mode[] modes = display.getSupportedModes();
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
        boolean found = false;
        float targetRefreshRate = 0.0f;
        for (Display.Mode mode : getWindowManager().getDefaultDisplay().getSupportedModes()) {
            if (mode.getModeId() == modeId) {
                found = true;
                targetRefreshRate = mode.getRefreshRate();
                break;
            }
        }
        if (!found) return false;

        final int requestedMode = modeId;
        final float requestedRefreshRate = targetRefreshRate;
        runOnUiThread(() -> {
            final WindowManager.LayoutParams attributes = getWindow().getAttributes();
            if (attributes.preferredDisplayModeId != requestedMode ||
                    Math.abs(attributes.preferredRefreshRate - requestedRefreshRate) > 0.01f) {
                attributes.preferredDisplayModeId = requestedMode;
                attributes.preferredRefreshRate = requestedRefreshRate;
                getWindow().setAttributes(attributes);
            }

            if (mSurface != null && mSurface.getHolder() != null) {
                final Surface surface = mSurface.getHolder().getSurface();
                if (surface != null && surface.isValid()) {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                        surface.setFrameRate(requestedRefreshRate,
                                Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE,
                                Surface.CHANGE_FRAME_RATE_ALWAYS);
                    } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                        surface.setFrameRate(requestedRefreshRate,
                                Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
                    }
                }
            }
        });
        return true;
    }

    @Override
    protected String[] getArguments() {
        File root = SchoolDaysDataManager.gameRoot(this);

        // Legacy/private QA fallback: keep old OBB layouts bootable, but the
        // distributable flow imports into app-specific external storage instead
        // of asking users to write Android/obb themselves.
        File obb = getObbDir();
        if (!new File(root, "school-days-hq.ktnroute").isFile()
                && new File(obb, "school-days-hq.ktnroute").isFile()) {
            root = obb;
        }

        File route = new File(root, "school-days-hq.ktnroute");
        File styles = new File(root, "styles.skot");
        File packs = new File(root, "Packs");

        return new String[]{
                "-r", "opengles2",
                "-s", styles.getAbsolutePath(),
                "-K", route.getAbsolutePath(),
                "--sd-gpk-root", packs.getAbsolutePath()
        };
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {
                "SDL3_shared_bundle",
                "SDL3_ttf_shared_bundle",
                "SchoolDays"
        };
    }
}
