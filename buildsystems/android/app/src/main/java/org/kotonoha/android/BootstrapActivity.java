package org.kotonoha.android;

import android.app.Activity;
import android.content.Intent;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

public final class BootstrapActivity extends Activity {
    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        TextView status = new TextView(this);
        status.setPadding(32, 32, 32, 32);
        status.setText("Preparing game assets...");
        setContentView(status);
        new Thread(() -> {
            try {
                String fingerprint;
                try (InputStream input = getAssets().open("packaged-assets.sha256")) {
                    fingerprint = new String(readAll(input), StandardCharsets.UTF_8).trim();
                }
                if (!fingerprint.matches("[a-f0-9]{64}")) {
                    throw new IOException("Invalid packaged asset fingerprint");
                }
                File directory = getFilesDir();
                File complete = new File(directory, ".kotonoha-assets.sha256");
                String installed = complete.isFile()
                        ? new String(Files.readAllBytes(complete.toPath()), StandardCharsets.UTF_8).trim()
                        : "";
                if (!fingerprint.equals(installed)) {
                    Files.deleteIfExists(complete.toPath());
                    copyDirectory(getAssets(), "assets", directory, "assets");
                    String[] gameFiles = getAssets().list("game-assets");
                    if (gameFiles == null) throw new IOException("Cannot list game-assets");
                    if (gameFiles.length > 0)
                        copyDirectory(getAssets(), "game-assets", getDataDir(), "game_assets");
                    File gameAssets = new File(getDataDir(), "game_assets");
                    if (!gameAssets.isDirectory() && !gameAssets.mkdirs()) {
                        throw new IOException("Cannot create game-assets directory");
                    }
                    if (BuildConfig.NATIVE_TARGET.equals("DokiDoki")) {
                        copyDirectory(getAssets(), "python", directory, "python");
                    }
                    Files.write(complete.toPath(), fingerprint.getBytes(StandardCharsets.UTF_8));
                }
                runOnUiThread(() -> {
                    if (isFinishing() || isDestroyed()) return;
                    Intent game = new Intent(this, GameActivity.class);
                    startActivity(game);
                    finish();
                });
            } catch (IOException error) {
                Log.e("Kotonoha", "Cannot prepare packaged assets", error);
                runOnUiThread(() -> status.setText("Cannot prepare game assets:\n" + error.getMessage()));
            }
        }, "Kotonoha-assets").start();
    }

    private static byte[] readAll(InputStream input) throws IOException {
        java.io.ByteArrayOutputStream output = new java.io.ByteArrayOutputStream();
        byte[] buffer = new byte[65536];
        int count;
        while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
        return output.toByteArray();
    }

    private static void copyDirectory(AssetManager assets, String path, File root,
                                      String relative) throws IOException {
        File destination = new File(root, relative);
        if (!destination.getCanonicalPath().startsWith(root.getCanonicalPath() + File.separator)) {
            throw new IOException("Asset path escapes resource directory");
        }
        String[] children = assets.list(path);
        if (children == null) throw new IOException("Cannot list assets: " + path);
        if (children.length > 0) {
            if (!destination.isDirectory() && !destination.mkdirs()) {
                throw new IOException("Cannot create asset directory: " + destination);
            }
            for (String child : children)
                copyDirectory(assets, path + "/" + child, root, relative + "/" + child);
        } else {
            File parent = destination.getParentFile();
            if (!parent.isDirectory() && !parent.mkdirs()) {
                throw new IOException("Cannot create asset parent: " + parent);
            }
            try (InputStream input = assets.open(path);
                 FileOutputStream output = new FileOutputStream(destination)) {
                byte[] buffer = new byte[65536];
                int count;
                while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
            }
        }
    }
}
