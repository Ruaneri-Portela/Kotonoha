package org.kotonoha.android;

import android.app.Activity;
import android.content.Intent;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;
import android.widget.TextView;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.view.Gravity;
import android.graphics.Color;

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
        // Universal black splash, centered logo; no portrait/landscape artwork.
        FrameLayout splash = new FrameLayout(this);
        splash.setBackgroundColor(Color.BLACK);
        ImageView logo = new ImageView(this);
        logo.setImageResource(R.drawable.kotonoha_splash_mark);
        logo.setScaleType(ImageView.ScaleType.FIT_CENTER);
        int size = (int)(168 * getResources().getDisplayMetrics().density);
        splash.addView(logo, new FrameLayout.LayoutParams(size, size, Gravity.CENTER));
        TextView status = new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setText("Preparing game assets...");
        status.setGravity(Gravity.CENTER);
        FrameLayout.LayoutParams message = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
        message.bottomMargin = (int)(32 * getResources().getDisplayMetrics().density);
        splash.addView(status, message);
        setContentView(splash);
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
                    if (BuildConfig.NATIVE_TARGET.equals("DokiDoki")
                            || BuildConfig.NATIVE_TARGET.equals("KatawaShoujo")
                            || BuildConfig.NATIVE_TARGET.equals("KotonohaLauncher")) {
                        deleteRecursive(new File(directory, "python"));
                        copyDirectory(getAssets(), "python", directory, "python");
                    }
                    Files.write(complete.toPath(), fingerprint.getBytes(StandardCharsets.UTF_8));
                }
                runOnUiThread(() -> {
                    if (isFinishing() || isDestroyed()) return;
                    Class<?> nextActivity = BuildConfig.NATIVE_TARGET.equals("KotonohaLauncher")
                            ? LauncherActivity.class
                            : GameActivity.class;
                    Intent next = new Intent(this, nextActivity);
                    startActivity(next);
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

    private static void deleteRecursive(File target) {
        if (target == null || !target.exists()) return;
        if (target.isDirectory()) {
            File[] children = target.listFiles();
            if (children != null) {
                for (File child : children) deleteRecursive(child);
            }
        }
        target.delete();
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
            } catch (java.io.FileNotFoundException emptyDirOrMissing) {
                // AssetManager.list() returns an empty array for both regular files
                // and empty directories. If opening as a file fails, create directory.
                if (!destination.isDirectory() && !destination.mkdirs()) {
                    throw emptyDirOrMissing;
                }
            }
        }
    }
}
