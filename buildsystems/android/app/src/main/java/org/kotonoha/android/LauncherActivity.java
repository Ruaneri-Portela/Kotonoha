package org.kotonoha.android;

import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.documentfile.provider.DocumentFile;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.google.android.material.bottomsheet.BottomSheetDialog;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.chip.Chip;
import com.google.android.material.color.DynamicColors;
import com.google.android.material.floatingactionbutton.ExtendedFloatingActionButton;
import com.google.android.material.materialswitch.MaterialSwitch;
import com.google.android.material.progressindicator.LinearProgressIndicator;
import com.google.android.material.textfield.TextInputEditText;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Native Material Design 3 Universal Launcher for Android.
 * Uses Storage Access Framework (SAF) for folders & ISO/BIN disc images,
 * delegates engine detection & parameter persistence to C++ via LauncherBridge,
 * and launches GameActivity with kotonoha_args.
 */
public final class LauncherActivity extends AppCompatActivity {

    private static final class GameEntry {
        String title = "";
        String saveProfileId = "";
        String path = "";
        String overlayPath = "";
        String engineSlug = "";
        String engineDisplayName = "";
        String evidence = "";
        String saveDirectory = "";
        boolean isDiscImage = false;
        boolean linkedInProcess = true;
        boolean isSchoolDays = false;

        // Per-game optional parameters
        String assetOverlayPath = "";
        String translationPath = "";
        String subtitleStylePath = "";
        String customDocumentsDir = "";
        String ktrfPath = "";
        String scriptsDir = "";
        String startScenario = "";

        boolean hasCustomParams() {
            return !assetOverlayPath.isEmpty()
                    || !translationPath.isEmpty()
                    || !subtitleStylePath.isEmpty()
                    || !customDocumentsDir.isEmpty()
                    || !ktrfPath.isEmpty()
                    || !scriptsDir.isEmpty()
                    || !startScenario.isEmpty();
        }
    }

    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final List<GameEntry> allGames = new ArrayList<>();
    private final List<GameEntry> filteredGames = new ArrayList<>();
    private GameAdapter adapter;

    private TextView statusText;
    private LinearProgressIndicator progressIndicator;
    private View emptyView;
    private RecyclerView recyclerView;
    private MaterialSwitch splitSavesSwitch;
    private String searchQuery = "";

    // Pending SAF picker callback for per-game optional parameter fields.
    private TextInputEditText pendingPickerTarget;

    private final ActivityResultLauncher<Uri> addFolderPicker =
            registerForActivityResult(new ActivityResultContracts.OpenDocumentTree(), uri -> {
                if (uri != null) {
                    SafBridge.persistUriPermission(this, uri);
                    importFolder(uri);
                }
            });

    private final ActivityResultLauncher<String[]> addDiscPicker =
            registerForActivityResult(new ActivityResultContracts.OpenDocument(), uri -> {
                if (uri != null) {
                    SafBridge.persistUriPermission(this, uri);
                    rescanLibrary();
                }
            });

    private final ActivityResultLauncher<String[]> addZipPicker =
            registerForActivityResult(new ActivityResultContracts.OpenDocument(), uri -> {
                if (uri != null) {
                    SafBridge.persistUriPermission(this, uri);
                    importZip(uri);
                }
            });

    private final ActivityResultLauncher<Uri> paramFolderPicker =
            registerForActivityResult(new ActivityResultContracts.OpenDocumentTree(), uri -> {
                if (uri != null && pendingPickerTarget != null) {
                    SafBridge.persistUriPermission(this, uri);
                    pendingPickerTarget.setText(uri.toString());
                }
                pendingPickerTarget = null;
            });

    private final ActivityResultLauncher<String[]> paramFilePicker =
            registerForActivityResult(new ActivityResultContracts.OpenDocument(), uri -> {
                if (uri != null && pendingPickerTarget != null) {
                    SafBridge.persistUriPermission(this, uri);
                    pendingPickerTarget.setText(uri.toString());
                }
                pendingPickerTarget = null;
            });

    private static final int STORAGE_PERMISSION_REQUEST_CODE = 1001;

    private void checkAndRequestStoragePermission() {
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R) {
            if (!android.os.Environment.isExternalStorageManager()) {
                try {
                    Intent intent = new Intent(android.provider.Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                    intent.setData(Uri.parse("package:" + getPackageName()));
                    startActivity(intent);
                } catch (Exception e) {
                    Intent intent = new Intent(android.provider.Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                    startActivity(intent);
                }
            }
        } else if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M) {
            if (checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[]{
                        android.Manifest.permission.READ_EXTERNAL_STORAGE,
                        android.Manifest.permission.WRITE_EXTERNAL_STORAGE
                }, STORAGE_PERMISSION_REQUEST_CODE);
            }
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        DynamicColors.applyToActivityIfAvailable(this);
        super.onCreate(savedInstanceState);
        checkAndRequestStoragePermission();
        setContentView(R.layout.activity_launcher);

        statusText = findViewById(R.id.launcher_status_text);
        progressIndicator = findViewById(R.id.launcher_progress);
        emptyView = findViewById(R.id.launcher_empty_view);
        recyclerView = findViewById(R.id.launcher_recycler);
        splitSavesSwitch = findViewById(R.id.switch_split_saves);

        recyclerView.setLayoutManager(new LinearLayoutManager(this));
        adapter = new GameAdapter();
        recyclerView.setAdapter(adapter);

        splitSavesSwitch.setChecked(SafBridge.isSplitSavesEnabled(this));
        splitSavesSwitch.setOnCheckedChangeListener((buttonView, isChecked) -> {
            SafBridge.setSplitSavesEnabled(this, isChecked);
            rescanLibrary();
        });

        TextInputEditText searchInput = findViewById(R.id.launcher_search_input);
        searchInput.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {}
            @Override public void afterTextChanged(Editable s) {
                searchQuery = s != null ? s.toString().trim() : "";
                applyFilter();
            }
        });

        MaterialButton rescanBtn = findViewById(R.id.btn_rescan);
        rescanBtn.setOnClickListener(v -> rescanLibrary());

        ExtendedFloatingActionButton addFolderFab = findViewById(R.id.fab_add_folder);
        addFolderFab.setOnClickListener(v -> addFolderPicker.launch(null));

        ExtendedFloatingActionButton addIsoFab = findViewById(R.id.fab_add_iso);
        addIsoFab.setOnClickListener(v -> addDiscPicker.launch(new String[]{"*/*"}));

        ExtendedFloatingActionButton addZipFab = findViewById(R.id.fab_add_zip);
        addZipFab.setOnClickListener(v -> addZipPicker.launch(new String[]{"*/*"}));

        try {
            configureNativeStorage();
        } catch (ErrnoException error) {
            statusText.setText("Cannot initialize launcher storage: " + error.getMessage());
            return;
        }
        rescanLibrary();
    }

    private void configureNativeStorage() throws ErrnoException {
        File documents = getExternalFilesDir(null);
        if (documents == null) documents = getFilesDir();
        Os.setenv("KOTONOHA_ANDROID_RESOURCE_DIR", getFilesDir().getAbsolutePath(), true);
        Os.setenv("KOTONOHA_ANDROID_DOCUMENTS_DIR", documents.getAbsolutePath(), true);
        Os.setenv("KOTONOHA_ANDROID_GAME_ASSET_DIR",
                new File(getDataDir(), "game_assets").getAbsolutePath(), true);
        File obb = getObbDir();
        if (obb != null)
            Os.setenv("KOTONOHA_ANDROID_DATA_DIR", obb.getAbsolutePath(), true);
        Os.setenv("KOTONOHA_ANDROID_PACKAGE_ID", getPackageName(), true);
    }

    private void importZip(Uri uri) {
        progressIndicator.setVisibility(View.VISIBLE);
        statusText.setText("Importing ZIP...");
        executor.execute(() -> {
            try {
                File root = new File(getExternalFilesDir(null) != null
                        ? getExternalFilesDir(null) : getFilesDir(), "games");
                if (!root.isDirectory() && !root.mkdirs())
                    throw new IOException("Cannot create games directory");
                File staging = new File(root, ".import-" + UUID.randomUUID());
                if (!staging.mkdir()) throw new IOException("Cannot create import directory");
                try {
                    long total = 0;
                    int entries = 0;
                    String base = staging.getCanonicalPath() + File.separator;
                    try (InputStream source = getContentResolver().openInputStream(uri)) {
                        if (source == null) throw new IOException("Cannot open ZIP");
                        try (ZipInputStream zip = new ZipInputStream(source)) {
                            ZipEntry entry;
                            byte[] buffer = new byte[65536];
                            while ((entry = zip.getNextEntry()) != null) {
                                if (++entries > 100000) throw new IOException("Too many ZIP entries");
                                String path = entry.getName().replace('\\', '/');
                                if (path.startsWith("/") || path.indexOf('\0') >= 0
                                        || path.matches("^[A-Za-z]:.*"))
                                    throw new IOException("Unsafe ZIP path");
                                File output = new File(staging, path);
                                if (!output.getCanonicalPath().startsWith(base))
                                    throw new IOException("Unsafe ZIP path");
                                if (entry.isDirectory()) {
                                    if (!output.isDirectory() && !output.mkdirs())
                                        throw new IOException("Cannot create ZIP directory");
                                } else {
                                    File parent = output.getParentFile();
                                    if (!parent.isDirectory() && !parent.mkdirs())
                                        throw new IOException("Cannot create ZIP directory");
                                    try (FileOutputStream out = new FileOutputStream(output)) {
                                        int count;
                                        while ((count = zip.read(buffer)) != -1) {
                                            total += count;
                                            if (total > 64L * 1024 * 1024 * 1024)
                                                throw new IOException("ZIP expands beyond 64 GiB");
                                            out.write(buffer, 0, count);
                                        }
                                    }
                                }
                                zip.closeEntry();
                            }
                        }
                    }
                    if (entries == 0) throw new IOException("ZIP is empty");
                    String filename = uri.getLastPathSegment();
                    if (filename == null) filename = "Imported Game";
                    filename = filename.substring(filename.lastIndexOf('/') + 1)
                            .replaceAll("(?i)\\.zip$", "").replaceAll("[^A-Za-z0-9._ -]", "_");
                    if (filename.isEmpty() || filename.equals(".") || filename.equals(".."))
                        filename = "Imported Game";
                    File destination = new File(root, filename);
                    int suffix = 2;
                    while (destination.exists()) destination = new File(root, filename + "-" + suffix++);
                    File sourceDirectory = staging;
                    File[] topLevel = staging.listFiles();
                    if (topLevel != null && topLevel.length == 1 && topLevel[0].isDirectory())
                        sourceDirectory = topLevel[0];
                    try {
                        java.nio.file.Files.move(sourceDirectory.toPath(), destination.toPath(),
                                java.nio.file.StandardCopyOption.REPLACE_EXISTING);
                    } catch (IOException moveError) {
                        copyDirectory(sourceDirectory, destination);
                        deleteTree(sourceDirectory);
                    }
                } finally {
                    deleteTree(staging);
                }
                runOnUiThread(this::rescanLibrary);
            } catch (Exception error) {
                runOnUiThread(() -> {
                    progressIndicator.setVisibility(View.GONE);
                    statusText.setText("ZIP import failed: " + error.getMessage());
                });
            }
        });
    }

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) deleteTree(child);
        file.delete();
    }

    private static void copyDirectory(File source, File target) throws IOException {
        if (source.isDirectory()) {
            if (!target.exists() && !target.mkdirs()) {
                throw new IOException("Cannot create directory: " + target);
            }
            String[] children = source.list();
            if (children != null) {
                for (String child : children) {
                    copyDirectory(new File(source, child), new File(target, child));
                }
            }
        } else {
            java.nio.file.Files.copy(source.toPath(), target.toPath(),
                    java.nio.file.StandardCopyOption.REPLACE_EXISTING);
        }
    }

    private void importFolder(Uri uri) {
        progressIndicator.setVisibility(View.VISIBLE);
        statusText.setText("Importing Folder...");
        executor.execute(() -> {
            try {
                File root = new File(getExternalFilesDir(null) != null
                        ? getExternalFilesDir(null) : getFilesDir(), "games");
                if (!root.isDirectory() && !root.mkdirs())
                    throw new IOException("Cannot create games directory");

                DocumentFile sourceTree = DocumentFile.fromTreeUri(this, uri);
                if (sourceTree == null || !sourceTree.isDirectory())
                    throw new IOException("Cannot open selected folder");

                String folderName = sourceTree.getName();
                if (folderName == null || folderName.isEmpty()) folderName = "Imported Game";
                folderName = folderName.replaceAll("[^A-Za-z0-9._ -]", "_");
                if (folderName.isEmpty() || folderName.equals(".") || folderName.equals(".."))
                    folderName = "Imported Game";

                File destination = new File(root, folderName);
                int suffix = 2;
                while (destination.exists()) destination = new File(root, folderName + "-" + suffix++);
                if (!destination.mkdirs())
                    throw new IOException("Cannot create game destination directory");

                copyDocumentTree(sourceTree, destination);

                runOnUiThread(this::rescanLibrary);
            } catch (Exception error) {
                runOnUiThread(() -> {
                    progressIndicator.setVisibility(View.GONE);
                    statusText.setText("Folder import failed: " + error.getMessage());
                });
            }
        });
    }

    private void copyDocumentTree(DocumentFile source, File targetDir) throws IOException {
        DocumentFile[] children = source.listFiles();
        if (children != null) {
            for (DocumentFile child : children) {
                String name = child.getName();
                if (name == null) continue;
                File targetFile = new File(targetDir, name);
                if (child.isDirectory()) {
                    if (!targetFile.isDirectory() && !targetFile.mkdirs()) {
                        throw new IOException("Cannot create directory: " + targetFile);
                    }
                    copyDocumentTree(child, targetFile);
                } else {
                    try (InputStream in = getContentResolver().openInputStream(child.getUri());
                         FileOutputStream out = new FileOutputStream(targetFile)) {
                        if (in != null) {
                            byte[] buffer = new byte[65536];
                            int count;
                            while ((count = in.read(buffer)) != -1) {
                                out.write(buffer, 0, count);
                            }
                        }
                    }
                }
            }
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        executor.shutdownNow();
    }

    private void rescanLibrary() {
        progressIndicator.setVisibility(View.VISIBLE);
        statusText.setText("Scanning library...");
        final List<String> roots = SafBridge.getScanRoots(this);
        final boolean splitSaves = SafBridge.isSplitSavesEnabled(this);

        executor.execute(() -> {
            List<GameEntry> scanned = new ArrayList<>();
            if (LauncherBridge.ensureLoaded()) {
                String json = LauncherBridge.nativeScanRootsJson(
                        roots.toArray(new String[0]), splitSaves, "");
                scanned = parseCatalogJson(json);
            }
            final List<GameEntry> finalScanned = scanned;
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) return;
                progressIndicator.setVisibility(View.GONE);
                allGames.clear();
                allGames.addAll(finalScanned);
                applyFilter();
                statusText.setText(finalScanned.isEmpty()
                        ? "No games detected"
                        : finalScanned.size() + " game(s) detected");
            });
        });
    }

    private void applyFilter() {
        filteredGames.clear();
        String q = searchQuery.toLowerCase(Locale.ROOT);
        for (GameEntry item : allGames) {
            if (q.isEmpty()
                    || item.title.toLowerCase(Locale.ROOT).contains(q)
                    || item.engineDisplayName.toLowerCase(Locale.ROOT).contains(q)
                    || item.engineSlug.toLowerCase(Locale.ROOT).contains(q)) {
                filteredGames.add(item);
            }
        }
        adapter.notifyDataSetChanged();
        emptyView.setVisibility(filteredGames.isEmpty() ? View.VISIBLE : View.GONE);
        recyclerView.setVisibility(filteredGames.isEmpty() ? View.GONE : View.VISIBLE);
    }

    private void launchGame(GameEntry game) {
        if (!game.linkedInProcess) {
            Toast.makeText(this, "Engine runtime is not linked in this build.", Toast.LENGTH_SHORT).show();
            return;
        }
        List<String> args = new ArrayList<>();
        args.add("--no-launcher");
        args.add("--library");
        args.add(game.path);
        if (SafBridge.isSplitSavesEnabled(this)) {
            args.add("--split-saves");
        } else {
            args.add("--shared-engine-saves");
        }
        if (!game.assetOverlayPath.isEmpty()) {
            args.add("--asset-overlay");
            args.add(game.assetOverlayPath);
        }
        if (!game.translationPath.isEmpty()) {
            args.add("--translation");
            args.add(game.translationPath);
        }
        if (!game.subtitleStylePath.isEmpty()) {
            args.add("--subtitle-style");
            args.add(game.subtitleStylePath);
        }
        if (!game.customDocumentsDir.isEmpty()) {
            args.add("--documents");
            args.add(game.customDocumentsDir);
        }
        if (!game.ktrfPath.isEmpty()) {
            args.add("--ktrf");
            args.add(game.ktrfPath);
        }
        if (!game.scriptsDir.isEmpty()) {
            args.add("--scripts");
            args.add(game.scriptsDir);
        }
        if (!game.startScenario.isEmpty()) {
            args.add("--start");
            args.add(game.startScenario);
        }

        Intent intent = new Intent(this, GameActivity.class);
        intent.putExtra("kotonoha_args", args.toArray(new String[0]));
        startActivity(intent);
    }

    private void showParametersSheet(GameEntry game) {
        BottomSheetDialog dialog = new BottomSheetDialog(this);
        View sheet = LayoutInflater.from(this).inflate(R.layout.dialog_game_parameters, null);
        dialog.setContentView(sheet);

        TextView titleView = sheet.findViewById(R.id.params_sheet_title);
        titleView.setText("Parameters — " + game.title);

        LinearLayout schoolDaysSection = sheet.findViewById(R.id.params_schooldays_section);
        schoolDaysSection.setVisibility(game.isSchoolDays ? View.VISIBLE : View.GONE);

        TextInputEditText inputKtrf = sheet.findViewById(R.id.input_ktrf);
        TextView ktrfStatus = sheet.findViewById(R.id.text_ktrf_status);
        TextInputEditText inputScripts = sheet.findViewById(R.id.input_scripts);
        TextInputEditText inputOverlay = sheet.findViewById(R.id.input_overlay);
        TextInputEditText inputTranslation = sheet.findViewById(R.id.input_translation);
        TextInputEditText inputSubtitleStyle = sheet.findViewById(R.id.input_subtitle_style);
        TextInputEditText inputDocuments = sheet.findViewById(R.id.input_documents);
        TextInputEditText inputStart = sheet.findViewById(R.id.input_start_scenario);
        MaterialSwitch sheetSplitSwitch = sheet.findViewById(R.id.switch_sheet_split_saves);

        inputKtrf.setText(game.ktrfPath);
        inputScripts.setText(game.scriptsDir);
        inputOverlay.setText(game.assetOverlayPath);
        inputTranslation.setText(game.translationPath);
        inputSubtitleStyle.setText(game.subtitleStylePath);
        inputDocuments.setText(game.customDocumentsDir);
        inputStart.setText(game.startScenario);
        sheetSplitSwitch.setChecked(SafBridge.isSplitSavesEnabled(this));

        Runnable updateRouteStatus = () -> {
            if (!game.isSchoolDays) return;
            String candidate = textOf(inputKtrf);
            if (LauncherBridge.ensureLoaded()) {
                String res = LauncherBridge.nativeValidateRoute(candidate);
                ktrfStatus.setText(res.startsWith("OK|")
                        ? "Valid: " + res.substring(3)
                        : "Warning: " + (res.startsWith("ERR|") ? res.substring(4) : res));
            }
        };
        updateRouteStatus.run();

        inputKtrf.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {}
            @Override public void afterTextChanged(Editable s) { updateRouteStatus.run(); }
        });

        sheet.findViewById(R.id.btn_pick_ktrf).setOnClickListener(v -> {
            pendingPickerTarget = inputKtrf;
            paramFilePicker.launch(new String[]{"*/*"});
        });
        sheet.findViewById(R.id.btn_pick_scripts).setOnClickListener(v -> {
            pendingPickerTarget = inputScripts;
            paramFolderPicker.launch(null);
        });
        sheet.findViewById(R.id.btn_pick_overlay).setOnClickListener(v -> {
            pendingPickerTarget = inputOverlay;
            paramFolderPicker.launch(null);
        });
        sheet.findViewById(R.id.btn_pick_translation).setOnClickListener(v -> {
            pendingPickerTarget = inputTranslation;
            paramFilePicker.launch(new String[]{"*/*"});
        });
        sheet.findViewById(R.id.btn_pick_subtitle_style).setOnClickListener(v -> {
            pendingPickerTarget = inputSubtitleStyle;
            paramFilePicker.launch(new String[]{"*/*"});
        });
        sheet.findViewById(R.id.btn_pick_documents).setOnClickListener(v -> {
            pendingPickerTarget = inputDocuments;
            paramFolderPicker.launch(null);
        });

        sheet.findViewById(R.id.btn_params_reset).setOnClickListener(v -> {
            inputKtrf.setText("");
            inputScripts.setText("");
            inputOverlay.setText("");
            inputTranslation.setText("");
            inputSubtitleStyle.setText("");
            inputDocuments.setText("");
            inputStart.setText("");
        });

        sheet.findViewById(R.id.btn_params_cancel).setOnClickListener(v -> dialog.dismiss());
        sheet.findViewById(R.id.btn_params_save).setOnClickListener(v -> {
            game.ktrfPath = textOf(inputKtrf);
            game.scriptsDir = textOf(inputScripts);
            game.assetOverlayPath = textOf(inputOverlay);
            game.translationPath = textOf(inputTranslation);
            game.subtitleStylePath = textOf(inputSubtitleStyle);
            game.customDocumentsDir = textOf(inputDocuments);
            game.startScenario = textOf(inputStart);

            boolean split = sheetSplitSwitch.isChecked();
            SafBridge.setSplitSavesEnabled(this, split);
            splitSavesSwitch.setChecked(split);

            if (LauncherBridge.ensureLoaded()) {
                LauncherBridge.nativeSaveGameParams(
                        game.saveProfileId,
                        game.assetOverlayPath,
                        game.translationPath,
                        game.subtitleStylePath,
                        game.customDocumentsDir,
                        game.ktrfPath,
                        game.scriptsDir,
                        game.startScenario);
            }
            adapter.notifyDataSetChanged();
            dialog.dismiss();
        });

        dialog.show();
    }

    private static String textOf(TextInputEditText editText) {
        Editable e = editText.getText();
        return e != null ? e.toString().trim() : "";
    }

    private static List<GameEntry> parseCatalogJson(String json) {
        List<GameEntry> list = new ArrayList<>();
        if (json == null || json.isEmpty()) return list;
        try {
            JSONArray array = new JSONArray(json);
            for (int i = 0; i < array.length(); ++i) {
                JSONObject obj = array.getJSONObject(i);
                GameEntry e = new GameEntry();
                e.title = obj.optString("title", "");
                e.saveProfileId = obj.optString("saveProfileId", "");
                e.path = obj.optString("path", "");
                e.overlayPath = obj.optString("overlayPath", "");
                e.engineSlug = obj.optString("engineSlug", "");
                e.engineDisplayName = obj.optString("engineDisplayName", "");
                e.evidence = obj.optString("evidence", "");
                e.saveDirectory = obj.optString("saveDirectory", "");
                e.isDiscImage = obj.optBoolean("isDiscImage", false);
                e.linkedInProcess = obj.optBoolean("linkedInProcess", true);
                e.isSchoolDays = obj.optBoolean("isSchoolDays", false);
                JSONObject params = obj.optJSONObject("params");
                if (params != null) {
                    e.assetOverlayPath = params.optString("assetOverlayPath", "");
                    e.translationPath = params.optString("translationPath", "");
                    e.subtitleStylePath = params.optString("subtitleStylePath", "");
                    e.customDocumentsDir = params.optString("customDocumentsDir", "");
                    e.ktrfPath = params.optString("ktrfPath", "");
                    e.scriptsDir = params.optString("scriptsDir", "");
                    e.startScenario = params.optString("startScenario", "");
                }
                list.add(e);
            }
        } catch (Exception ignored) {
        }
        return list;
    }

    private final class GameAdapter extends RecyclerView.Adapter<GameAdapter.GameViewHolder> {
        final class GameViewHolder extends RecyclerView.ViewHolder {
            final TextView title;
            final Chip engineChip;
            final TextView sourceBadge;
            final TextView overlayBadge;
            final TextView path;
            final MaterialButton paramsBtn;
            final MaterialButton playBtn;

            GameViewHolder(@NonNull View itemView) {
                super(itemView);
                title = itemView.findViewById(R.id.game_title);
                engineChip = itemView.findViewById(R.id.game_engine_chip);
                sourceBadge = itemView.findViewById(R.id.game_source_badge);
                overlayBadge = itemView.findViewById(R.id.game_overlay_badge);
                path = itemView.findViewById(R.id.game_path);
                paramsBtn = itemView.findViewById(R.id.btn_game_params);
                playBtn = itemView.findViewById(R.id.btn_game_play);
            }
        }

        @NonNull
        @Override
        public GameViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
            View view = LayoutInflater.from(parent.getContext())
                    .inflate(R.layout.item_game_card, parent, false);
            return new GameViewHolder(view);
        }

        @Override
        public void onBindViewHolder(@NonNull GameViewHolder holder, int position) {
            GameEntry game = filteredGames.get(position);
            holder.title.setText(game.title);
            holder.engineChip.setText(game.engineDisplayName);
            holder.sourceBadge.setText(game.isDiscImage ? "DISC IMAGE" : "FOLDER");
            boolean hasOverlay = !game.overlayPath.isEmpty() || !game.assetOverlayPath.isEmpty();
            holder.overlayBadge.setVisibility(hasOverlay ? View.VISIBLE : View.GONE);
            holder.path.setText(game.path);
            holder.paramsBtn.setText(game.hasCustomParams() ? "Parameters *" : "Parameters");
            holder.paramsBtn.setOnClickListener(v -> showParametersSheet(game));
            holder.playBtn.setEnabled(game.linkedInProcess);
            holder.playBtn.setOnClickListener(v -> launchGame(game));
        }

        @Override
        public int getItemCount() {
            return filteredGames.size();
        }
    }
}
