package org.kotonoha.android;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.ImageView;
import android.widget.TextView;

import java.io.IOException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

/** Portrait setup for the existing native port. Storage grants belong to this app ID. */
public final class SchoolDaysSetup extends Activity {
    private static final int PICK_FOLDER = 1;
    private static final int PLAY = 2;
    private boolean gameFailed;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private DocumentTree source;
    private StartupNotice notice;
    private Future<?> scan;
    private int generation;
    private boolean options;
    private boolean privateReady;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.school_days_setup);
        source = new DocumentTree(this);
        notice = new StartupNotice(findViewById(R.id.status));
        TextView version = findViewById(R.id.version);
        version.setText(getString(R.string.sd_info, BuildConfig.VERSION_NAME));
        findViewById(R.id.play).setOnClickListener(v -> {
            if (privateReady && source.selected() == null) {
                gameFailed = false;
                startActivityForResult(new Intent(this, BootstrapActivity.class), PLAY);
            } else {
                panel(true);
                refresh();
            }
        });
        findViewById(R.id.pick_home).setOnClickListener(v -> {
            panel(true);
            findViewById(R.id.pick).performClick();
        });
        findViewById(R.id.forget).setOnClickListener(v -> {
            try {
                source.forget();
                refresh();
            } catch (SecurityException error) {
                notice.show(getText(R.string.sd_denied));
            }
        });
        findViewById(R.id.options).setOnClickListener(v -> panel(true));
        findViewById(R.id.quit).setOnClickListener(v -> finish());
        findViewById(R.id.back).setOnClickListener(v -> panel(false));
        findViewById(R.id.retry).setOnClickListener(v -> refresh());
        findViewById(R.id.pick).setOnClickListener(v -> {
            try {
                startActivityForResult(DocumentTree.picker(), PICK_FOLDER);
            } catch (ActivityNotFoundException e) {
                notice.show(getText(R.string.sd_picker_error));
            }
        });
        gameFailed = state != null && state.getBoolean("gameFailed");
        panel(state != null && state.getBoolean("options"));
    }

    private void panel(boolean open) {
        options = open;
        findViewById(R.id.home).setVisibility(open ? View.GONE : View.VISIBLE);
        findViewById(R.id.settings).setVisibility(open ? View.VISIBLE : View.GONE);
    }

    @Override
    protected void onResume() {
        super.onResume();
        refresh();
    }

    private void refresh() {
        int token = ++generation;
        if (scan != null) scan.cancel(true);
        privateReady = false;
        showArt(null);
        TextView languages = findViewById(R.id.languages);
        languages.setText(R.string.sd_no_translations);
        Uri tree = source.selected();
        findViewById(R.id.pick_home).setVisibility(tree == null ? View.VISIBLE : View.GONE);
        findViewById(R.id.forget).setVisibility(tree != null ? View.VISIBLE : View.GONE);
        notice.show(getText(tree == null ? R.string.sd_missing : R.string.sd_checking));
        scan = worker.submit(() -> {
            if (tree == null) {
                boolean ready = SchoolDaysFolder.hasPrivateInstallation(getDataDir());
                runOnUiThread(() -> {
                    if (token != generation || !ready) return;
                    privateReady = true;
                    notice.show(getText(gameFailed ? R.string.sd_game_failed : R.string.sd_private_ready));
                });
                return;
            }
            try {
                if (!source.canRead(tree)) throw new SecurityException();
                SchoolDaysFolder found = new SchoolDaysFolder(this, source, tree);
                runOnUiThread(() -> {
                    if (token != generation) return;
                    notice.show(found.missing.isEmpty() ? getString(R.string.sd_pending)
                            : getString(R.string.sd_absent, String.join(", ", found.missing)));
                    if (!found.languages.isEmpty()) languages.setText(
                            getString(R.string.sd_detected, String.join(", ", found.languages)));
                    showArt(found);
                });
            } catch (IOException | RuntimeException e) {
                runOnUiThread(() -> {
                    if (token == generation) notice.show(getText(e instanceof SecurityException
                            ? R.string.sd_denied : R.string.sd_unreadable));
                });
            }
        });
    }

    private void showArt(SchoolDaysFolder folder) {
        ImageView art = findViewById(R.id.art);
        boolean visible = folder != null && folder.artwork != null;
        art.setImageBitmap(visible ? folder.artwork : null);
        art.setVisibility(visible ? View.VISIBLE : View.GONE);
        findViewById(R.id.title).setVisibility(visible ? View.GONE : View.VISIBLE);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == PLAY) {
            gameFailed = result != RESULT_OK;
            if (gameFailed) panel(true);
        }
        if (request == PICK_FOLDER && result == RESULT_OK && data != null) {
            try {
                source.remember(data);
            } catch (IOException | RuntimeException e) {
                android.widget.Toast.makeText(this, R.string.sd_denied,
                        android.widget.Toast.LENGTH_LONG).show();
            }
        }
    }

    @Override
    public void onBackPressed() {
        if (options) panel(false);
        else super.onBackPressed();
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        state.putBoolean("options", options);
        state.putBoolean("gameFailed", gameFailed);
    }

    @Override
    protected void onDestroy() {
        ++generation;
        worker.shutdownNow();
        super.onDestroy();
    }
}
