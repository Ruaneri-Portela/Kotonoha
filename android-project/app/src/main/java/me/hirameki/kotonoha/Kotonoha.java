package me.hirameki.kotonoha;

import android.content.Context;
import android.util.DisplayMetrics;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.util.Locale;

public class Kotonoha extends SDLActivity {

    private static String[] customArguments = null;
    private static int screenWidth;
    private static int screenHeight;

    @Override
    protected String[] getArguments() {
        getScreenResolution();
        String[] defaultArgs = new String[]{
                "-z", "-o", "-r", "opengles2", "-f", "-x", String.format(Locale.ROOT,"%d", screenWidth), String.format(Locale.ROOT,"%d", screenHeight)
        };
        if (customArguments != null) {
            String[] args = new String[defaultArgs.length + customArguments.length];
            System.arraycopy(defaultArgs, 0, args, 0, defaultArgs.length);
            System.arraycopy(customArguments, 0, args, defaultArgs.length, customArguments.length);
            return args;
        }
        return defaultArgs;
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {
                "SDL3",
                "SDL3_ttf",
                "Kotonoha"
        };
    }

    public static void setArguments(String[] arguments) {
        customArguments = arguments;
    }

    @Override
    protected void onResume() {
        super.onResume();
        getScreenResolution();
    }


    private void getScreenResolution() {
        WindowManager wm = (WindowManager) getSystemService(Context.WINDOW_SERVICE);

        DisplayMetrics displayMetrics = new DisplayMetrics();
        wm.getDefaultDisplay().getMetrics(displayMetrics);

        screenWidth = displayMetrics.widthPixels;
        screenHeight = displayMetrics.heightPixels;
    }

    private void closeKotonoha() {
        setResult(RESULT_OK);
        finish();
    }
}
