package org.kotonoha.android;

import android.app.Activity;
import android.app.AlertDialog;
import java.io.File;
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
    private int failedStage;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private DocumentTree source;
    private StartupNotice notice;
    private Future<?> scan;
    private int generation;
    private boolean options;
    private boolean privateReady;
    private SchoolDaysFolder folder;
    private File session;
    private boolean preparing;
    private String locale;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.school_days_setup);
        source = new DocumentTree(this);
        locale = getPreferences(MODE_PRIVATE).getString("locale", "en");
        if (state != null && state.getString("session") != null)
            session = new File(getCacheDir(), state.getString("session"));
        notice = new StartupNotice(findViewById(R.id.status));
        TextView version = findViewById(R.id.version);
        version.setText(getString(R.string.sd_info, BuildConfig.VERSION_NAME));
        findViewById(R.id.play).setOnClickListener(v -> {
            if (preparing) return;
            if (folder != null && folder.missing.isEmpty()) {
                if (!folder.languages.contains(locale)) {
                    panel(true);
                    chooseLanguage();
                } else {
                    launchFolder();
                }
            } else if (privateReady && source.selected() == null) {
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
                gameFailed = false;
                refresh();
            } catch (SecurityException error) {
                notice.show(getText(R.string.sd_denied));
            }
        });
        findViewById(R.id.options).setOnClickListener(v -> panel(true));
        findViewById(R.id.choose_language).setOnClickListener(v -> chooseLanguage());
        findViewById(R.id.quit).setOnClickListener(v -> finish());
        findViewById(R.id.back).setOnClickListener(v -> panel(false));
        findViewById(R.id.retry).setOnClickListener(v -> { gameFailed = false; refresh(); });
        findViewById(R.id.pick).setOnClickListener(v -> {
            try {
                startActivityForResult(DocumentTree.picker(), PICK_FOLDER);
            } catch (ActivityNotFoundException e) {
                notice.show(getText(R.string.sd_picker_error));
            }
        });
        gameFailed = state != null && state.getBoolean("gameFailed");
        failedStage = state == null ? 0 : state.getInt("failedStage");
        panel(state != null && state.getBoolean("options"));
    }

    private int failureMessage() {
        switch (failedStage) {
            case 1: return R.string.sd_failed_packs;
            case 2: return R.string.sd_failed_route;
            case 4: return R.string.sd_failed_language;
            default: return R.string.sd_game_failed;
        }
    }

    private void chooseLanguage() {
        if (folder == null || folder.languages.isEmpty()) return;
        String[] items = folder.languages.toArray(new String[0]);
        new AlertDialog.Builder(this).setTitle(R.string.sd_language)
                .setSingleChoiceItems(items, folder.languages.indexOf(locale), (dialog, which) -> {
                    locale = items[which];
                    getPreferences(MODE_PRIVATE).edit().putString("locale", locale).apply();
                    gameFailed = false;
                    dialog.dismiss();
                    refresh();
                }).show();
    }

    private void launchFolder() {
        final SchoolDaysFolder selected = folder;
        final Uri tree = source.selected();
        final String language = locale;
        final int token = generation;
        preparing = true;
        gameFailed = false;
        notice.show(getText(R.string.sd_checking));
        worker.submit(() -> {
            try {
                if (tree == null || !source.canRead(tree)) throw new SecurityException();
                File ready = selected.prepare(this, source, language);
                runOnUiThread(() -> {
                    preparing = false;
                    if (isFinishing() || isDestroyed() || generation != token) {
                        SchoolDaysFolder.discard(ready);
                        return;
                    }
                    session = ready;
                    Intent start = new Intent(this, BootstrapActivity.class);
                    start.putExtra(GameActivity.LAUNCH_ARGUMENTS,
                            SchoolDaysFolder.launchArguments(session, language));
                    startActivityForResult(start, PLAY);
                });
            } catch (IOException | RuntimeException error) {
                runOnUiThread(() -> {
                    preparing = false;
                    if (generation == token) notice.show(getText(error instanceof SecurityException
                            ? R.string.sd_denied : R.string.sd_unreadable));
                });
            }
        });
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
        folder = null;
        findViewById(R.id.choose_language).setVisibility(View.GONE);
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
                    folder = found;
                    if (!found.missing.isEmpty())
                        notice.show(getString(R.string.sd_absent, String.join(", ", found.missing)));
                    else if (gameFailed) notice.show(getText(failureMessage()));
                    else if (found.languages.isEmpty()) notice.show(getText(R.string.sd_no_translations));
                    else if (!found.languages.contains(locale)) notice.show(getText(R.string.sd_choose));
                    else notice.show(getString(R.string.sd_ready, locale));
                    if (!found.languages.isEmpty()) findViewById(R.id.choose_language).setVisibility(View.VISIBLE);
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
            SchoolDaysFolder.discard(session);
            session = null;
            gameFailed = result != RESULT_OK;
            failedStage = data == null ? 0 : data.getIntExtra(GameActivity.STARTUP_STAGE, 0);
            if (gameFailed) panel(true);
        }
        if (request == PICK_FOLDER && result == RESULT_OK && data != null) {
            try {
                source.remember(data);
                gameFailed = false;
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
        if (session != null) state.putString("session", session.getName());
        state.putBoolean("gameFailed", gameFailed);
        state.putInt("failedStage", failedStage);
    }

    @Override
    protected void onDestroy() {
        ++generation;
        worker.shutdownNow();
        super.onDestroy();
    }
}
