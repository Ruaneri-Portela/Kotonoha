package org.kotonoha.android;

import android.content.Context;
import android.content.Intent;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.AutoCompleteTextView;
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
        String rendererBackend = "";
        String upscalerBackend = "";
        String upscalerPreset = "";
        String upscalerFactor = "";

        boolean hasCustomParams() {
            return !assetOverlayPath.isEmpty()
                    || !translationPath.isEmpty()
                    || !subtitleStylePath.isEmpty()
                    || !customDocumentsDir.isEmpty()
                    || !ktrfPath.isEmpty()
                    || !scriptsDir.isEmpty()
                    || !startScenario.isEmpty()
                    || !rendererBackend.isEmpty()
                    || !upscalerBackend.isEmpty()
                    || !upscalerPreset.isEmpty()
                    || !upscalerFactor.isEmpty();
        }
    }

    private static final String[] RENDERER_VALUES = new String[]{
            "", "vulkan", "opengles2", "software"
    };
    private static final String[] UPSCALER_VALUES = new String[]{
            "none", "auto", "mpv", "linear"
    };
    private static final String[] FACTOR_VALUES = new String[]{
            "1.5", "2.0", "3.0", "4.0"
    };
    private static final String[] FACTOR_LABELS = new String[]{
            "1.5x", "2.0x", "3.0x", "4.0x"
    };
    private static final String LAUNCHER_LANGUAGE_PREFS = "kotonoha_launcher_ui";
    private static final String LAUNCHER_LANGUAGE_KEY = "interface_language";
    private static final String[] LAUNCHER_LANGUAGE_TAGS = {
            "en", "pt-BR", "es", "ru", "zh-CN"
    };

    private static String launcherLanguage(Context context) {
        String selected = context.getSharedPreferences(LAUNCHER_LANGUAGE_PREFS, MODE_PRIVATE)
                .getString(LAUNCHER_LANGUAGE_KEY, "");
        for (String tag : LAUNCHER_LANGUAGE_TAGS) {
            if (tag.equals(selected)) return tag;
        }
        Locale system = context.getResources().getConfiguration().getLocales().get(0);
        switch (system.getLanguage()) {
            case "pt": return "pt-BR";
            case "es": return "es";
            case "ru": return "ru";
            case "zh":
                // Do not silently replace Traditional Chinese with Simplified.
                if ("Hant".equalsIgnoreCase(system.getScript())
                        || "TW".equalsIgnoreCase(system.getCountry())
                        || "HK".equalsIgnoreCase(system.getCountry())
                        || "MO".equalsIgnoreCase(system.getCountry())) return "en";
                return "zh-CN";
            default: return "en";
        }
    }

    @Override
    protected void attachBaseContext(Context base) {
        Configuration config = new Configuration(base.getResources().getConfiguration());
        Locale locale = Locale.forLanguageTag(launcherLanguage(base));
        config.setLocale(locale);
        config.setLayoutDirection(locale);
        super.attachBaseContext(base.createConfigurationContext(config));
    }
    private static final String[] DEFAULT_PRESET_LABELS = new String[]{
            "Anime4K: Mode A (HQ)",
            "Anime4K: Mode B (HQ)",
            "Anime4K: Mode C (HQ)",
            "Anime4K: Mode A+A (HQ)",
            "Anime4K: Mode B+B (HQ)",
            "Anime4K: Mode C+A (HQ)",
            "Anime4K: Mode A (Fast)",
            "Anime4K: Mode B (Fast)",
            "Anime4K: Mode C (Fast)",
            "Anime4K: Mode A+A (Fast)",
            "Anime4K: Mode B+B (Fast)",
            "Anime4K: Mode C+A (Fast)",
            "ArtCNN C4F16",
            "ArtCNN C4F16 DS",
            "ArtCNN C4F32"
    };

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
                TextInputEditText target = pendingPickerTarget;
                pendingPickerTarget = null;
                if (uri != null && target != null) {
                    SafBridge.persistUriPermission(this, uri);
                    importParameterFolder(uri, target);
                }
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
        // Keep the help path accurate for both launcher DEV and DIST variants.
        ((TextView) findViewById(R.id.launcher_empty_hint)).setText(
                getString(R.string.empty_library_hint, BuildConfig.APPLICATION_ID));
        recyclerView = findViewById(R.id.launcher_recycler);
        splitSavesSwitch = findViewById(R.id.switch_split_saves);

        AutoCompleteTextView languageDropdown = findViewById(R.id.dropdown_launcher_language);
        String[] languageLabels = getResources().getStringArray(R.array.launcher_languages);
        languageDropdown.setAdapter(new ArrayAdapter<>(this,
                android.R.layout.simple_list_item_1, languageLabels));
        int currentLanguage = indexOfValue(LAUNCHER_LANGUAGE_TAGS, launcherLanguage(this));
        languageDropdown.setText(languageLabels[currentLanguage], false);
        languageDropdown.setOnItemClickListener((parent, view, position, id) -> {
            if (position == currentLanguage) return;
            getSharedPreferences(LAUNCHER_LANGUAGE_PREFS, MODE_PRIVATE).edit()
                    .putString(LAUNCHER_LANGUAGE_KEY, LAUNCHER_LANGUAGE_TAGS[position]).apply();
            recreate();
        });

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

        MaterialButton graphicsBtn = findViewById(R.id.btn_graphics_settings);
        graphicsBtn.setOnClickListener(v -> showGraphicsSettingsSheet());

        ExtendedFloatingActionButton addFolderFab = findViewById(R.id.fab_add_folder);
        addFolderFab.setOnClickListener(v -> addFolderPicker.launch(null));

        ExtendedFloatingActionButton addIsoFab = findViewById(R.id.fab_add_iso);
        addIsoFab.setOnClickListener(v -> addDiscPicker.launch(new String[]{"*/*"}));

        ExtendedFloatingActionButton addZipFab = findViewById(R.id.fab_add_zip);
        addZipFab.setOnClickListener(v -> addZipPicker.launch(new String[]{"*/*"}));

        try {
            configureNativeStorage();
        } catch (ErrnoException error) {
            statusText.setText(getString(R.string.storage_init_failed, error.getMessage()));
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
        if (getApplicationInfo() != null && getApplicationInfo().nativeLibraryDir != null)
            Os.setenv("KOTONOHA_ANDROID_NATIVE_LIB_DIR",
                    getApplicationInfo().nativeLibraryDir, true);
        Os.setenv("KOTONOHA_ANDROID_PACKAGE_ID", getPackageName(), true);
    }

    private void importZip(Uri uri) {
        progressIndicator.setVisibility(View.VISIBLE);
        statusText.setText(R.string.importing_zip);
        executor.execute(() -> {
            try {
                File root = new File(getExternalFilesDir(null) != null
                        ? getExternalFilesDir(null) : getFilesDir(), "games");
                if (!root.isDirectory() && !root.mkdirs())
                    throw new IOException(getString(R.string.error_create_games_directory));
                File staging = new File(root, ".import-" + UUID.randomUUID());
                if (!staging.mkdir()) throw new IOException(getString(R.string.error_create_import_directory));
                try {
                    long total = 0;
                    int entries = 0;
                    String base = staging.getCanonicalPath() + File.separator;
                    try (InputStream source = getContentResolver().openInputStream(uri)) {
                        if (source == null) throw new IOException(getString(R.string.error_open_zip));
                        try (ZipInputStream zip = new ZipInputStream(source)) {
                            ZipEntry entry;
                            byte[] buffer = new byte[65536];
                            while ((entry = zip.getNextEntry()) != null) {
                                if (++entries > 100000) throw new IOException(getString(R.string.error_many_zip_entries));
                                String path = entry.getName().replace('\\', '/');
                                if (path.startsWith("/") || path.indexOf('\0') >= 0
                                        || path.matches("^[A-Za-z]:.*"))
                                    throw new IOException(getString(R.string.error_unsafe_zip_path));
                                File output = new File(staging, path);
                                if (!output.getCanonicalPath().startsWith(base))
                                    throw new IOException(getString(R.string.error_unsafe_zip_path));
                                if (entry.isDirectory()) {
                                    if (!output.isDirectory() && !output.mkdirs())
                                        throw new IOException(getString(R.string.error_create_zip_directory));
                                } else {
                                    File parent = output.getParentFile();
                                    if (!parent.isDirectory() && !parent.mkdirs())
                                        throw new IOException(getString(R.string.error_create_zip_directory));
                                    try (FileOutputStream out = new FileOutputStream(output)) {
                                        int count;
                                        while ((count = zip.read(buffer)) != -1) {
                                            total += count;
                                            if (total > 64L * 1024 * 1024 * 1024)
                                                throw new IOException(getString(R.string.error_zip_too_large));
                                            out.write(buffer, 0, count);
                                        }
                                    }
                                }
                                zip.closeEntry();
                            }
                        }
                    }
                    if (entries == 0) throw new IOException(getString(R.string.error_empty_zip));
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
                    statusText.setText(getString(R.string.zip_import_failed, error.getMessage()));
                });
            }
        });
    }

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) deleteTree(child);
        file.delete();
    }

    private void copyDirectory(File source, File target) throws IOException {
        if (source.isDirectory()) {
            if (!target.exists() && !target.mkdirs()) {
                throw new IOException(getString(R.string.error_create_directory, target));
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

    private void importParameterFolder(Uri uri, TextInputEditText target) {
        Toast.makeText(this, R.string.importing_selected_folder, Toast.LENGTH_SHORT).show();
        executor.execute(() -> {
            try {
                File root = new File(getExternalFilesDir(null) != null
                        ? getExternalFilesDir(null) : getFilesDir(), "overlays");
                if (!root.isDirectory() && !root.mkdirs())
                    throw new IOException(getString(R.string.error_create_overlays_directory));

                DocumentFile sourceTree = DocumentFile.fromTreeUri(this, uri);
                if (sourceTree == null || !sourceTree.isDirectory())
                    throw new IOException(getString(R.string.error_open_selected_folder));

                String folderName = sourceTree.getName();
                if (folderName == null || folderName.isEmpty()) folderName = "Overlay";
                folderName = folderName.replaceAll("[^A-Za-z0-9._ -]", "_");
                if (folderName.isEmpty() || folderName.equals(".") || folderName.equals(".."))
                    folderName = "Overlay";

                File destination = new File(root, folderName);
                deleteTree(destination);
                if (!destination.mkdirs())
                    throw new IOException(getString(R.string.error_create_overlay_destination));

                try {
                    copyDocumentTree(sourceTree, destination);
                    verifyImportedTree(sourceTree, destination, destination.getName(), false);
                } catch (Exception error) {
                    deleteTree(destination);
                    throw error;
                }

                String resolvedPath = destination.getAbsolutePath();
                runOnUiThread(() -> {
                    if (isFinishing() || isDestroyed()) return;
                    target.setText(resolvedPath);
                    Toast.makeText(this, getString(R.string.folder_imported, destination.getName()),
                            Toast.LENGTH_SHORT).show();
                });
            } catch (Exception error) {
                runOnUiThread(() -> {
                    if (isFinishing() || isDestroyed()) return;
                    Toast.makeText(this, getString(R.string.folder_import_failed, error.getMessage()),
                            Toast.LENGTH_LONG).show();
                });
            }
        });
    }

    private void importFolder(Uri uri) {
        progressIndicator.setVisibility(View.VISIBLE);
        statusText.setText(R.string.importing_folder);
        executor.execute(() -> {
            try {
                File root = new File(getExternalFilesDir(null) != null
                        ? getExternalFilesDir(null) : getFilesDir(), "games");
                if (!root.isDirectory() && !root.mkdirs())
                    throw new IOException(getString(R.string.error_create_games_directory));

                DocumentFile sourceTree = DocumentFile.fromTreeUri(this, uri);
                if (sourceTree == null || !sourceTree.isDirectory())
                    throw new IOException(getString(R.string.error_open_selected_folder));

                String folderName = sourceTree.getName();
                if (folderName == null || folderName.isEmpty()) folderName = "Imported Game";
                folderName = folderName.replaceAll("[^A-Za-z0-9._ -]", "_");
                if (folderName.isEmpty() || folderName.equals(".") || folderName.equals(".."))
                    folderName = "Imported Game";

                File destination = new File(root, folderName);
                int suffix = 2;
                while (destination.exists()) destination = new File(root, folderName + "-" + suffix++);
                if (!destination.mkdirs())
                    throw new IOException(getString(R.string.error_create_game_destination));

                try {
                    copyDocumentTree(sourceTree, destination);
                    verifyImportedTree(sourceTree, destination, destination.getName(), true);
                } catch (Exception error) {
                    // Never leave a partial game on disk: a later rescan would
                    // register it and reproduce the audio-only symptom.
                    deleteTree(destination);
                    throw error;
                }

                runOnUiThread(this::rescanLibrary);
            } catch (Exception error) {
                runOnUiThread(() -> {
                    progressIndicator.setVisibility(View.GONE);
                    statusText.setText(getString(R.string.folder_import_failed, error.getMessage()));
                });
            }
        });
    }

    private void copyDocumentTree(DocumentFile source, File targetDir) throws IOException {
        DocumentFile[] children = source.listFiles();
        if (children == null)
            throw new IOException("Cannot list folder: " + source.getName());
        for (DocumentFile child : children) {
            String name = child.getName();
            if (name == null)
                throw new IOException("Cannot read an entry name in: " + source.getName());
            File targetFile = new File(targetDir, name);
            if (child.isDirectory()) {
                if (!targetFile.isDirectory() && !targetFile.mkdirs())
                    throw new IOException(getString(R.string.error_create_directory, targetFile));
                copyDocumentTree(child, targetFile);
            } else {
                try (InputStream in = getContentResolver().openInputStream(child.getUri())) {
                    if (in == null)
                        throw new IOException("Cannot open asset: " + name);
                    try (FileOutputStream out = new FileOutputStream(targetFile)) {
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

    /** Number of entries in a DocumentFile subtree; -1 if it cannot be listed. */
    private static int countDocumentEntries(DocumentFile source) {
        DocumentFile[] children = source.listFiles();
        if (children == null) return -1;
        int count = 0;
        for (DocumentFile child : children) {
            if (child.getName() == null) return -1;
            count++;
            if (child.isDirectory()) {
                int nested = countDocumentEntries(child);
                if (nested < 0) return -1;
                count += nested;
            }
        }
        return count;
    }

    /** Number of entries actually written below a destination directory. */
    private static int countFileEntries(File directory) {
        File[] children = directory.listFiles();
        if (children == null) return -1;
        int count = 0;
        for (File child : children) {
            count++;
            if (child.isDirectory()) {
                int nested = countFileEntries(child);
                if (nested < 0) return -1;
                count += nested;
            }
        }
        return count;
    }

    private static File findIgnoreCase(File parent, String name) {
        File[] children = parent.listFiles();
        if (children == null) return null;
        for (File child : children)
            if (child.getName().equalsIgnoreCase(name)) return child;
        return null;
    }

    private static File findPathIgnoreCase(File root, String relativePath) {
        File current = root;
        for (String segment : relativePath.split("/")) {
            if (segment.isEmpty()) continue;
            current = findIgnoreCase(current, segment);
            if (current == null) return null;
        }
        return current;
    }

    private static boolean isNonEmptyDirectory(File directory) {
        File[] children = directory.listFiles();
        return children != null && children.length > 0;
    }

    private static boolean hasLooseGraphics(File root) {
        File[] children = root.listFiles();
        if (children == null) return false;
        for (File child : children) {
            if (!child.isFile()) continue;
            String name = child.getName().toLowerCase(Locale.ROOT);
            if (name.endsWith(".g00") || name.endsWith(".pdt")) return true;
        }
        return false;
    }

    /**
     * Engine-specific checks for directories without which the game would be
     * registered but only partially playable (e.g. RealLive audio without g00).
     */
    private static String missingEngineAssets(File root) {
        for (String prefix : new String[]{"", "kineticdata/"}) {
            if (findPathIgnoreCase(root, prefix + "Seen.txt") != null
                    && findPathIgnoreCase(root, prefix + "Gameexe.ini") != null) {
                File g00 = findPathIgnoreCase(root, prefix + "G00");
                File pdt = findPathIgnoreCase(root, prefix + "PDT");
                if (!((g00 != null && isNonEmptyDirectory(g00))
                        || (pdt != null && isNonEmptyDirectory(pdt))
                        || hasLooseGraphics(root)))
                    return "g00/ is empty (RealLive graphics missing)";
                break;
            }
        }
        for (String prefix : new String[]{"", "files/"}) {
            String[] required = {"PARAM.PAK", "SCRIPT.PAK", "SYSSE.PAK"};
            boolean any = false;
            for (String name : required)
                if (findPathIgnoreCase(root, prefix + name) != null) any = true;
            if (!any) continue;
            for (String name : required)
                if (findPathIgnoreCase(root, prefix + name) == null)
                    return name + " missing (LucaSystem package incomplete)";
            break;
        }
        return null;
    }

    /**
     * Rejects incomplete copies before the library is rescanned, so the user
     * never gets a game entry that plays audio but cannot display images.
     */
    private static void verifyImportedTree(DocumentFile source, File destination,
                                           String label, boolean gameFolder)
            throws IOException {
        int expected = countDocumentEntries(source);
        int copied = countFileEntries(destination);
        if (expected >= 0 && copied != expected)
            throw new IOException(label + ": incomplete copy (" + copied
                    + " of " + expected + " entries). Re-import the folder.");
        if (gameFolder) {
            String issue = missingEngineAssets(destination);
            if (issue != null)
                throw new IOException(label + ": incomplete copy — " + issue
                        + ". Re-import the folder.");
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        executor.shutdownNow();
    }

    private void rescanLibrary() {
        progressIndicator.setVisibility(View.VISIBLE);
        statusText.setText(R.string.scanning_library);
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
                        ? getString(R.string.no_games_detected)
                        : getResources().getQuantityString(R.plurals.games_detected,
                                finalScanned.size(), finalScanned.size()));
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
            Toast.makeText(this, R.string.engine_not_linked, Toast.LENGTH_SHORT).show();
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
        String effectiveOverlay = !game.assetOverlayPath.isEmpty()
                ? game.assetOverlayPath
                : game.overlayPath;
        if (!effectiveOverlay.isEmpty()) {
            args.add("--asset-overlay");
            args.add(effectiveOverlay);
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

        String effectiveRenderer = !game.rendererBackend.isEmpty()
                ? game.rendererBackend
                : SafBridge.getRendererBackend(this);
        String effectiveUpscaler = !game.upscalerBackend.isEmpty()
                ? game.upscalerBackend
                : SafBridge.getUpscalerBackend(this);
        String effectivePreset = !game.upscalerPreset.isEmpty()
                ? game.upscalerPreset
                : SafBridge.getUpscalerPreset(this);
        String effectiveFactor = !game.upscalerFactor.isEmpty()
                ? game.upscalerFactor
                : SafBridge.getUpscalerFactor(this);

        if (!effectiveRenderer.isEmpty()) {
            args.add("--renderer");
            args.add(effectiveRenderer);
        }
        if (!effectiveUpscaler.isEmpty()) {
            args.add("--upscaler");
            args.add(effectiveUpscaler);
            if ("none".equals(effectiveUpscaler)) {
                args.add("--upscaler-factor");
                args.add("1.0");
            } else {
                args.add("--upscaler-factor");
                args.add(effectiveFactor.isEmpty() ? "2.0" : effectiveFactor);
                if (!effectivePreset.isEmpty()) {
                    args.add("--upscaler-preset");
                    args.add(effectivePreset);
                }
            }
        }

        Intent intent = new Intent(this, GameActivity.class);
        intent.putExtra("kotonoha_args", args.toArray(new String[0]));
        startActivity(intent);
    }

    private String[] loadPresetLabels() {
        if (LauncherBridge.ensureLoaded()) {
            try {
                String json = LauncherBridge.nativeUpscalePresetsJson();
                if (json != null && !json.isEmpty()) {
                    JSONArray array = new JSONArray(json);
                    if (array.length() > 0) {
                        String[] labels = new String[array.length()];
                        for (int i = 0; i < array.length(); ++i) {
                            labels[i] = array.getJSONObject(i).optString("name",
                                    getString(R.string.preset_number, i));
                        }
                        return labels;
                    }
                }
            } catch (Exception ignored) {
            }
        }
        return DEFAULT_PRESET_LABELS;
    }

    private static int indexOfValue(String[] values, String target) {
        if (target == null) return 0;
        for (int i = 0; i < values.length; ++i) {
            if (values[i].equals(target)) return i;
        }
        return 0;
    }

    private static String[] prependOption(String first, String[] rest) {
        String[] combined = new String[rest.length + 1];
        combined[0] = first;
        System.arraycopy(rest, 0, combined, 1, rest.length);
        return combined;
    }

    private void showGraphicsSettingsSheet() {
        BottomSheetDialog dialog = new BottomSheetDialog(this);
        View sheet = LayoutInflater.from(this).inflate(R.layout.dialog_graphics_settings, null);
        dialog.setContentView(sheet);

        AutoCompleteTextView rendererDropdown = sheet.findViewById(R.id.dropdown_global_renderer);
        AutoCompleteTextView upscalerDropdown = sheet.findViewById(R.id.dropdown_global_upscaler);
        AutoCompleteTextView factorDropdown = sheet.findViewById(R.id.dropdown_global_factor);
        AutoCompleteTextView presetDropdown = sheet.findViewById(R.id.dropdown_global_preset);
        View factorLayout = sheet.findViewById(R.id.layout_global_factor);
        View presetLayout = sheet.findViewById(R.id.layout_global_preset);

        String[] presetLabels = loadPresetLabels();
        String[] presetValues = new String[presetLabels.length];
        for (int i = 0; i < presetLabels.length; ++i) presetValues[i] = String.valueOf(i);

        final int[] selRenderer = {indexOfValue(RENDERER_VALUES, SafBridge.getRendererBackend(this))};
        final int[] selUpscaler = {indexOfValue(UPSCALER_VALUES, SafBridge.getUpscalerBackend(this))};
        final int[] selFactor = {indexOfValue(FACTOR_VALUES, SafBridge.getUpscalerFactor(this))};
        final int[] selPreset = {indexOfValue(presetValues, SafBridge.getUpscalerPreset(this))};

        String[] rendererLabels = getResources().getStringArray(R.array.renderer_labels);
        String[] upscalerLabels = getResources().getStringArray(R.array.upscaler_labels);
        rendererDropdown.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_list_item_1, rendererLabels));
        upscalerDropdown.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_list_item_1, upscalerLabels));
        factorDropdown.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_list_item_1, FACTOR_LABELS));
        presetDropdown.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_list_item_1, presetLabels));

        rendererDropdown.setText(rendererLabels[selRenderer[0]], false);
        upscalerDropdown.setText(upscalerLabels[selUpscaler[0]], false);
        factorDropdown.setText(FACTOR_LABELS[selFactor[0]], false);
        presetDropdown.setText(presetLabels[selPreset[0]], false);

        Runnable syncVisibility = () -> {
            String u = UPSCALER_VALUES[selUpscaler[0]];
            String r = RENDERER_VALUES[selRenderer[0]];
            boolean enabled = !"none".equals(u);
            factorLayout.setVisibility(enabled ? View.VISIBLE : View.GONE);
            boolean showPreset = enabled && ("mpv".equals(u) || ("auto".equals(u) && r.startsWith("opengl")));
            presetLayout.setVisibility(showPreset ? View.VISIBLE : View.GONE);
        };
        syncVisibility.run();

        rendererDropdown.setOnItemClickListener((parent, view, pos, id) -> {
            selRenderer[0] = pos;
            syncVisibility.run();
        });
        upscalerDropdown.setOnItemClickListener((parent, view, pos, id) -> {
            selUpscaler[0] = pos;
            syncVisibility.run();
        });
        factorDropdown.setOnItemClickListener((parent, view, pos, id) -> selFactor[0] = pos);
        presetDropdown.setOnItemClickListener((parent, view, pos, id) -> selPreset[0] = pos);

        sheet.findViewById(R.id.btn_graphics_cancel).setOnClickListener(v -> dialog.dismiss());
        sheet.findViewById(R.id.btn_graphics_save).setOnClickListener(v -> {
            SafBridge.setRendererBackend(this, RENDERER_VALUES[selRenderer[0]]);
            SafBridge.setUpscalerBackend(this, UPSCALER_VALUES[selUpscaler[0]]);
            SafBridge.setUpscalerFactor(this, FACTOR_VALUES[selFactor[0]]);
            SafBridge.setUpscalerPreset(this, presetValues[selPreset[0]]);
            Toast.makeText(this, R.string.graphics_settings_saved, Toast.LENGTH_SHORT).show();
            dialog.dismiss();
        });

        dialog.show();
    }

    private void showParametersSheet(GameEntry game) {
        BottomSheetDialog dialog = new BottomSheetDialog(this);
        View sheet = LayoutInflater.from(this).inflate(R.layout.dialog_game_parameters, null);
        dialog.setContentView(sheet);

        TextView titleView = sheet.findViewById(R.id.params_sheet_title);
        titleView.setText(getString(R.string.parameters_for_game, game.title));

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
        AutoCompleteTextView paramRendererDropdown = sheet.findViewById(R.id.dropdown_param_renderer);
        AutoCompleteTextView paramUpscalerDropdown = sheet.findViewById(R.id.dropdown_param_upscaler);
        AutoCompleteTextView paramFactorDropdown = sheet.findViewById(R.id.dropdown_param_factor);
        AutoCompleteTextView paramPresetDropdown = sheet.findViewById(R.id.dropdown_param_preset);

        String[] presetLabelsBase = loadPresetLabels();
        String[] presetValuesBase = new String[presetLabelsBase.length];
        for (int i = 0; i < presetLabelsBase.length; ++i) presetValuesBase[i] = String.valueOf(i);

        String[] paramRendererValues = new String[]{"", "vulkan", "opengles2", "software"};
        String[] rendererLabels = getResources().getStringArray(R.array.renderer_labels);
        String[] upscalerLabels = getResources().getStringArray(R.array.upscaler_labels);
        String[] paramRendererLabels = prependOption(getString(R.string.use_global_setting),
                new String[]{rendererLabels[1], rendererLabels[2], rendererLabels[3]});
        String[] paramUpscalerValues = prependOption("", UPSCALER_VALUES);
        String[] paramUpscalerLabels = prependOption(getString(R.string.use_global_setting), upscalerLabels);
        String[] paramFactorValues = prependOption("", FACTOR_VALUES);
        String[] paramFactorLabels = prependOption(getString(R.string.use_global_setting), FACTOR_LABELS);
        String[] paramPresetValues = prependOption("", presetValuesBase);
        String[] paramPresetLabels = prependOption(getString(R.string.use_global_setting), presetLabelsBase);

        final int[] selParamRenderer = {indexOfValue(paramRendererValues, game.rendererBackend)};
        final int[] selParamUpscaler = {indexOfValue(paramUpscalerValues, game.upscalerBackend)};
        final int[] selParamFactor = {indexOfValue(paramFactorValues, game.upscalerFactor)};
        final int[] selParamPreset = {indexOfValue(paramPresetValues, game.upscalerPreset)};

        paramRendererDropdown.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_list_item_1, paramRendererLabels));
        paramUpscalerDropdown.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_list_item_1, paramUpscalerLabels));
        paramFactorDropdown.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_list_item_1, paramFactorLabels));
        paramPresetDropdown.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_list_item_1, paramPresetLabels));

        paramRendererDropdown.setText(paramRendererLabels[selParamRenderer[0]], false);
        paramUpscalerDropdown.setText(paramUpscalerLabels[selParamUpscaler[0]], false);
        paramFactorDropdown.setText(paramFactorLabels[selParamFactor[0]], false);
        paramPresetDropdown.setText(paramPresetLabels[selParamPreset[0]], false);

        paramRendererDropdown.setOnItemClickListener((p, v, pos, id) -> selParamRenderer[0] = pos);
        paramUpscalerDropdown.setOnItemClickListener((p, v, pos, id) -> selParamUpscaler[0] = pos);
        paramFactorDropdown.setOnItemClickListener((p, v, pos, id) -> selParamFactor[0] = pos);
        paramPresetDropdown.setOnItemClickListener((p, v, pos, id) -> selParamPreset[0] = pos);

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
                        ? getString(R.string.route_valid, res.substring(3))
                        : getString(R.string.route_warning,
                                res.startsWith("ERR|") ? res.substring(4) : res));
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
            selParamRenderer[0] = 0;
            selParamUpscaler[0] = 0;
            selParamFactor[0] = 0;
            selParamPreset[0] = 0;
            paramRendererDropdown.setText(paramRendererLabels[0], false);
            paramUpscalerDropdown.setText(paramUpscalerLabels[0], false);
            paramFactorDropdown.setText(paramFactorLabels[0], false);
            paramPresetDropdown.setText(paramPresetLabels[0], false);
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
            game.rendererBackend = paramRendererValues[selParamRenderer[0]];
            game.upscalerBackend = paramUpscalerValues[selParamUpscaler[0]];
            game.upscalerFactor = paramFactorValues[selParamFactor[0]];
            game.upscalerPreset = paramPresetValues[selParamPreset[0]];

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
                        game.startScenario,
                        game.rendererBackend,
                        game.upscalerBackend,
                        game.upscalerPreset,
                        game.upscalerFactor);
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
                    e.rendererBackend = params.optString("rendererBackend", "");
                    e.upscalerBackend = params.optString("upscalerBackend", "");
                    e.upscalerPreset = params.optString("upscalerPreset", "");
                    e.upscalerFactor = params.optString("upscalerFactor", "");
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
            holder.sourceBadge.setText(game.isDiscImage
                    ? R.string.source_disc_image : R.string.source_folder);
            boolean hasOverlay = !game.overlayPath.isEmpty() || !game.assetOverlayPath.isEmpty();
            holder.overlayBadge.setVisibility(hasOverlay ? View.VISIBLE : View.GONE);
            holder.path.setText(game.path);
            holder.paramsBtn.setText(game.hasCustomParams()
                    ? R.string.parameters_modified : R.string.parameters);
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
