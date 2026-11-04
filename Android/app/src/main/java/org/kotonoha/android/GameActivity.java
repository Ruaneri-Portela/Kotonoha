package org.kotonoha.android;

import org.libsdl.app.SDLActivity;

import java.io.File;

public final class GameActivity extends SDLActivity {
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
