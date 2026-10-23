package me.hirameki.kotonoha;

import android.content.Context;
import android.util.DisplayMetrics;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.util.Locale;

public class Kotonoha extends SDLActivity {

    public static final String EXTRA_TARGET = "target";
    public static final String TARGET_SCHOOL_DAYS = "SchoolDays";
    public static final String TARGET_GENERIC = "Generic";
    public static final String TARGET_ZERO_NO_TSUKAIMA = "ZeroNoTsukaima";
    public static final String TARGET_SNOW_RADISH = "SnowRadish";
    public static final String TARGET_DDLC = "DokiDoki";

    private static String[] customArguments = null;

    private String resolveTarget() {
        String target = getIntent() == null ? null : getIntent().getStringExtra(EXTRA_TARGET);
        if (TARGET_GENERIC.equalsIgnoreCase(target)) return TARGET_GENERIC;
        if (TARGET_ZERO_NO_TSUKAIMA.equalsIgnoreCase(target)) return TARGET_ZERO_NO_TSUKAIMA;
        if (TARGET_SNOW_RADISH.equalsIgnoreCase(target)) return TARGET_SNOW_RADISH;
        if (TARGET_DDLC.equalsIgnoreCase(target)) return TARGET_DDLC;
        return TARGET_SCHOOL_DAYS;
    }

    // Retail GPKs live in the OBB directory: Android/obb/<package>/.
    private String[] schoolDaysDefaultArguments() {
        File obb = getObbDir();
        File assets = new File(getFilesDir(), "assets");
        File route = new File(obb, "school-days-hq.ktnroute");
        if (!route.isFile()) {
            route = new File(assets, "school-days-hq.ktnroute");
        }
        File styles = new File(assets, "STYLES.SKOT");
        if (!styles.isFile()) {
            styles = new File(assets, "styles.skot");
        }
        return new String[]{
                "-s", styles.getAbsolutePath(),
                "-K", route.getAbsolutePath(),
                "--sd-gpk-root", obb.getAbsolutePath()
        };
    }

    private String[] snowRadishDefaultArguments() {
        File dir = new File(getFilesDir(), "pack");
        return new String[]{"--snow", dir.getAbsolutePath()};
    }

    private String[] ddlcDefaultArguments() {
        File dir = new File(getFilesDir(), "game");
        return new String[]{"--ddlc", dir.getAbsolutePath()};
    }

    private String[] zeroDefaultArguments() {
        File file = new File(getFilesDir(), "z.bin");
        return new String[]{"--zero", file.getAbsolutePath()};
    }

    @Override
    protected String[] getArguments() {
        String[] defaultArgs = new String[]{
                "-v", "-u", "-z", "-t", "-r", "opengles2"
        };
        String[] extra = customArguments;
        if (extra == null || extra.length == 0) {
            String target = resolveTarget();
            if (TARGET_SCHOOL_DAYS.equals(target)) {
                extra = schoolDaysDefaultArguments();
            } else if (TARGET_SNOW_RADISH.equals(target)) {
                extra = snowRadishDefaultArguments();
            } else if (TARGET_DDLC.equals(target)) {
                extra = ddlcDefaultArguments();
            } else if (TARGET_ZERO_NO_TSUKAIMA.equals(target)) {
                extra = zeroDefaultArguments();
            }
        }
        if (extra != null && extra.length > 0) {
            String[] args = new String[defaultArgs.length + extra.length];
            System.arraycopy(defaultArgs, 0, args, 0, defaultArgs.length);
            System.arraycopy(extra, 0, args, defaultArgs.length, extra.length);
            return args;
        }
        return defaultArgs;
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {
                "SDL3_shared_bundle",
                "SDL3_ttf_shared_bundle",
                resolveTarget()
        };
    }

    public static void setArguments(String[] arguments) {
        customArguments = arguments;
    }

    @Override
    protected void onResume() {
        super.onResume();
    }
}
