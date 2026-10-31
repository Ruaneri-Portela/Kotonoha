package org.kotonoha.android;

import org.libsdl.app.SDLActivity;

import java.io.File;

public final class GameActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[]{"c++_shared", "SDL3", "kotonoha_imgui",
                            "SDL3_ttf", "kotonoha", BuildConfig.NATIVE_TARGET};
    }

    public String getKotonohaResourceDirectory() {
        String path = getIntent().getStringExtra("resources");
        if (path == null || !new File(path, ".complete").isFile()) {
            throw new IllegalStateException("Packaged assets have not been prepared");
        }
        return path;
    }

    public String getKotonohaDataDirectory() {
        return directory(getObbDir(), "Android OBB");
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
