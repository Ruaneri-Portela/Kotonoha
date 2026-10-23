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

    private static String[] customArguments = null;

    private String resolveTarget() {
        String target = getIntent() == null ? null : getIntent().getStringExtra(EXTRA_TARGET);
        return TARGET_GENERIC.equalsIgnoreCase(target) ? TARGET_GENERIC : TARGET_SCHOOL_DAYS;
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

    @Override
    protected String[] getArguments() {
        String[] defaultArgs = new String[]{
                "-v", "-u", "-z", "-t", "-r", "opengles2"
        };
        String[] extra = customArguments;
        if ((extra == null || extra.length == 0) && TARGET_SCHOOL_DAYS.equals(resolveTarget())) {
            extra = schoolDaysDefaultArguments();
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
                "SDL3",
                "SDL3_ttf",
                TARGET_GENERIC.equals(resolveTarget()) ? "KotonohaGeneric" : "KotonohaSchoolDays"
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
