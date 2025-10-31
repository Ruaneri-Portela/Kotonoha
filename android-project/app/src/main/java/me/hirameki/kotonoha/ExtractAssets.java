package me.hirameki.kotonoha;

import android.content.Context;
import android.content.res.AssetManager;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public class ExtractAssets {
    private static final String TAG = "ExtractAssets";

    public static void copyAssetsToDataFiles(Context context) {
        AssetManager assetManager = context.getAssets();
        copyDirectory(assetManager, "", "/data/data/" + context.getPackageName() + "/files/assets");
    }

    private static void copyDirectory(AssetManager assetManager, String srcDir, String destDir) {
        try {
            String[] files = assetManager.list(srcDir);
            if (files == null || files.length == 0) {
                return;
            }

            for (String file : files) {
                if (file.equals("images") || file.equals("webkit")) {
                    continue;
                }
                String srcPath = srcDir.isEmpty() ? file : srcDir + "/" + file;
                String destPath = destDir + "/" + file;
                File outFile = new File(destPath);

                try {
                    InputStream in = assetManager.open(srcPath);

                    if (!outFile.exists()) {
                        outFile.getParentFile().mkdirs();

                        OutputStream out = new FileOutputStream(outFile);
                        byte[] buffer = new byte[1024];
                        int length;
                        while ((length = in.read(buffer)) > 0) {
                            out.write(buffer, 0, length);
                        }
                        in.close();
                        out.close();
                        Log.i(TAG, "Arquivo copiado: " + srcPath);
                    }
                } catch (IOException e) {
                    if (!outFile.exists()) {
                        outFile.mkdirs();
                    }
                    Log.i(TAG, "Diretório criado: " + srcPath);
                    copyDirectory(assetManager, srcPath, destPath);
                }
            }
        } catch (IOException e) {
            Log.e(TAG, "Erro ao copiar assets", e);
        }
    }
}
