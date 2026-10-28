package me.hirameki.kotonoha;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

public class SchoolDaysMainActivity extends Activity {
    private static final int REQUEST_GAME_DATA = 4102;

    private TextView statusView;
    private Button importButton;
    private Button playButton;
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_school_days_main);
        ExtractAssets.copyAssetsToDataFiles(this);
        final boolean bootstrapReady = SchoolDaysDataManager.ensureBootstrap(this);

        statusView = findViewById(R.id.data_status);
        importButton = findViewById(R.id.import_data);
        playButton = findViewById(R.id.play_game);

        importButton.setOnClickListener(view -> chooseGameData());
        playButton.setOnClickListener(view -> launchSchoolDays());

        refreshState();
        if (!bootstrapReady) {
            statusView.setText("Falha ao preparar os arquivos internos do port. Reinstale o APK.");
            importButton.setEnabled(false);
            playButton.setEnabled(false);
            return;
        }
        if (SchoolDaysDataManager.isReady(this)) {
            launchSchoolDays();
        }
    }

    private void refreshState() {
        final boolean ready = SchoolDaysDataManager.isReady(this);
        playButton.setEnabled(ready);
        importButton.setEnabled(true);
        importButton.setText(ready ? R.string.reimport_data : R.string.import_data);
        statusView.setText(ready
                ? getString(R.string.data_ready) + "\n" + SchoolDaysDataManager.gameRoot(this).getAbsolutePath()
                : getString(R.string.data_missing));
    }

    private void chooseGameData() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        startActivityForResult(intent, REQUEST_GAME_DATA);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_GAME_DATA || resultCode != RESULT_OK || data == null) {
            return;
        }

        final Uri tree = data.getData();
        if (tree == null) return;
        try {
            getContentResolver().takePersistableUriPermission(
                    tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException ignored) {
            // Some document providers grant only the lifetime permission. The
            // import happens immediately, so this does not block the operation.
        }

        importButton.setEnabled(false);
        playButton.setEnabled(false);
        statusView.setText("Validando dados do School Days HQ...");

        new Thread(() -> {
            final SchoolDaysDataManager.ImportResult result =
                    SchoolDaysDataManager.importTree(this, tree,
                            (message, completed, total) -> runOnUiThread(() ->
                                    statusView.setText(message + "\n" + completed + "/" + total)));
            runOnUiThread(() -> {
                refreshState();
                Toast.makeText(this, result.message, Toast.LENGTH_LONG).show();
                if (result.success) launchSchoolDays();
            });
        }, "SchoolDaysDataImport").start();
    }

    private void launchSchoolDays() {
        if (!SchoolDaysDataManager.isReady(this)) {
            refreshState();
            return;
        }
        Intent intent = new Intent(this, SchoolDaysKotonoha.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        startActivity(intent);
    }
}
