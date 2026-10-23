package me.hirameki.kotonoha;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;

import java.io.File;

/**
 * Development-only Android KTRF boot smoke entry point.
 *
 * This activity is intentionally separate from MainActivity so Gate 7 can be
 * exercised through adb without changing the normal launcher flow or local
 * launcher experiments.
 *
 * Preferred development layout:
 *   <external-files>/SchoolDays/
 *
 * The full School Days tree can be deployed there once and reused across
 * incremental `adb install -r` builds. The APK's minimal staged assets remain
 * a deterministic fallback for Gate 7 boot-only testing.
 */
public class KtrfGate7Activity extends Activity {
    private static final String TAG = "KTRF-GATE7";
    private static final int REQUEST_KOTONOHA = 7007;

    private static boolean isReady(File root) {
        if (root == null) return false;
        return new File(root, "school-days-hq.ktnroute").isFile()
                && new File(root, "00/00-00-A00.ENG.ORS").isFile()
                && new File(root, "styles.skot").isFile();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Keep the minimal APK staging available as a fallback. ExtractAssets
        // deliberately does not overwrite existing files, so the Gate runner
        // refreshes only this internal directory when needed.
        ExtractAssets.copyAssetsToDataFiles(this);

        File internalRoot = new File(getFilesDir(), "assets");
        File externalBase = getExternalFilesDir(null);
        File externalRoot = externalBase == null ? null : new File(externalBase, "SchoolDays");

        final File assetsRoot;
        final String source;
        if (isReady(externalRoot)) {
            assetsRoot = externalRoot;
            source = "external-full";
        } else if (isReady(internalRoot)) {
            assetsRoot = internalRoot;
            source = "internal-smoke";
        } else {
            Log.e(TAG,
                    "staging incomplete external="
                            + (externalRoot == null ? "null" : externalRoot.getAbsolutePath())
                            + " internal=" + internalRoot.getAbsolutePath());
            finish();
            return;
        }

        File routeFile = new File(assetsRoot, "school-days-hq.ktnroute");
        File firstOrs = new File(assetsRoot, "00/00-00-A00.ENG.ORS");
        File styles = new File(assetsRoot, "styles.skot");

        String root = assetsRoot.getAbsolutePath();
        String mediaPrefix = root + File.separator;
        String[] args = new String[]{
                "-s", styles.getAbsolutePath(),
                "-p", mediaPrefix,
                "-K", routeFile.getAbsolutePath(), root
        };

        // Warning level is intentional for the deterministic Gate 7 marker.
        // Some Android vendor log configurations suppress application INFO
        // messages from `adb logcat -d`, while WARN remains visible.
        Log.w(TAG,
                "launch source=" + source
                        + " route=" + routeFile.getAbsolutePath()
                        + " ors_root=" + root
                        + " first_ors=" + firstOrs.getAbsolutePath());

        Kotonoha.setArguments(args);
        Intent intent = new Intent(this, Kotonoha.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        startActivityForResult(intent, REQUEST_KOTONOHA);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_KOTONOHA) {
            finish();
        }
    }
}
