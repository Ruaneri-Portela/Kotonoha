package me.hirameki.kotonoha;

import android.content.Context;
import android.util.DisplayMetrics;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.util.Locale;

public class Kotonoha extends SDLActivity {

    private static String[] customArguments = null;

    @Override
    protected String[] getArguments() {
        String[] defaultArgs = new String[]{
                "-v", "-u", "-z", "-t", "-r", "opengles2", "-f", "-i"
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
    }
}
