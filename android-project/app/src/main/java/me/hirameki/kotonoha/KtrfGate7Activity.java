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
 * launcher experiments. It copies the APK asset staging tree into app-private
 * storage, builds absolute filesystem paths, and launches the production SDL
 * activity in -K mode.
 */
public class KtrfGate7Activity extends Activity {
    private static final String TAG = "KTRF-GATE7";
    private static final int REQUEST_KOTONOHA = 7007;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        ExtractAssets.copyAssetsToDataFiles(this);

        File assetsRoot = new File(getFilesDir(), "assets");
        File routeFile = new File(assetsRoot, "school-days-hq.ktnroute");
        File firstOrs = new File(assetsRoot, "00/00-00-A00.ENG.ORS");
        File styles = new File(assetsRoot, "styles.skot");

        if (!routeFile.isFile() || !firstOrs.isFile() || !styles.isFile()) {
            Log.e(TAG,
                    "staging incomplete route=" + routeFile.isFile()
                            + " first_ors=" + firstOrs.isFile()
                            + " styles=" + styles.isFile()
                            + " root=" + assetsRoot.getAbsolutePath());
            finish();
            return;
        }

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
                "launch route=" + routeFile.getAbsolutePath()
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
