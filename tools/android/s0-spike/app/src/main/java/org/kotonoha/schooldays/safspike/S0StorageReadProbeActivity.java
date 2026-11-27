package org.kotonoha.schooldays.safspike;

import android.app.Activity;
import android.content.ContentValues;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.os.StatFs;
import android.provider.DocumentsContract;
import android.provider.MediaStore;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.util.Log;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.FileDescriptor;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Locale;
import java.util.Random;
import java.util.UUID;
import java.util.regex.Pattern;

/**
 * Standalone S0-only test APK. No native engine/GPK/save dependencies or access.
 * SAF selection accepts ONLY the synthetic document just created by this probe.
 */
public final class S0StorageReadProbeActivity extends Activity {
    private static final String TAG = "KTN_S0";
    private static final String TEST = "synthetic_fd_1gib";
    private static final int REQUEST_CREATE = 6010;
    private static final int REQUEST_OPEN = 6011;
    private static final int BLOCK_BYTES = 1024 * 1024;
    private static final int BLOCKS = 1024; // Exactly 1 GiB, streaming; never RAM-backed.
    private static final long SIZE_BYTES = (long) BLOCKS * BLOCK_BYTES;
    private static final long FREE_HEADROOM = 512L * 1024L * 1024L;
    private static final String PREFS = "s0_synthetic_session";
    private static final String PREF_CREATED = "created_content_uri";
    private static final String PREF_OPENED = "opened_content_uri";
    private static final String PREF_MEDIA = "mediastore_mode";
    private static final String PREF_STAGE = "stage";
    private static final String PRIVATE_NAME = "s0-synthetic-reference.bin";
    private static final String METRIC_PREFS = "s0_metrics_only";
    private static final String METRIC_HISTORY = "bounded_metric_lines";
    private static final int MAX_METRIC_LINES = 96;
    private static final int MAX_METRIC_CHARS = 32768;
    private static final int MAX_METRIC_LINE_CHARS = 384;
    // Strictly metrics-only: no persisted URI, path, filename or payload.
    private static final Pattern METRIC_ENTRY = Pattern.compile(
            "metric_id=[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12} "
                    + "test=synthetic_fd_1gib mode=(?:saf|mediastore|private) "
                    + "access=(?:sequential|random) pattern=(?:forward|reverse|strided) "
                    + "repetition=[123] block_bytes=1048576 blocks=1024 "
                    + "bytes=1073741824 equal=true read_ms=[0-9]+\\.[0-9]{3} "
                    + "throughput_mib_s=[0-9]+\\.[0-9]{3} "
                    + "lat_p50_ms=[0-9]+\\.[0-9]{3} lat_p95_ms=[0-9]+\\.[0-9]{3}");

    private Button startButton;
    private Button mediaStoreButton;
    private Button retestButton;
    private Button replayButton;
    private Button cleanupButton;
    private TextView resultView;
    private File privateSample;
    private Uri createdDocument;
    private Uri openedDocument;
    private Thread worker;
    private boolean busy;
    private boolean mediaStoreMode;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout body = new LinearLayout(this);
        body.setOrientation(LinearLayout.VERTICAL);
        int inset = (int) (20 * getResources().getDisplayMetrics().density);
        body.setPadding(inset, inset, inset, inset);

        TextView explanation = new TextView(this);
        explanation.setText("Standalone S0 / synthetic data only (1 GiB).\n"
                + "SAF mode: CREATE_DOCUMENT and reopen THAT SAME synthetic document "
                + "via OPEN_DOCUMENT, with persistable read grant.\n"
                + "MediaStore mode: create a synthetic Downloads document and read it "
                + "using its MediaStore descriptor (without any picker).\n"
                + "Both modes compare every byte against a private synthetic file "
                + "in 3 complete passes (sequential, reverse, strided) and probe "
                + "/proc/self/fd reopening.\n"
                + "SAF uses the system FILE picker: choose only this test's "
                + "synthetic document, never a folder or game file.\n"
                + "Requires at least 2.5 GiB available internally; the provider may need "
                + "another 1 GiB. After PASS, the files and SAF read grant are KEPT "
                + "for repeat tests after app exit or phone reboot. Only the explicit "
                + "Delete button attempts to remove this app's own test files and metric "
                + "history. Metrics replay with historic=true after app/reboot; "
                + "use Replay if logcat missed startup. "
                + "Never select a real game file or personal document.");
        body.addView(explanation);

        startButton = new Button(this);
        startButton.setText("Start synthetic SAF probe");
        startButton.setOnClickListener(view -> requestSyntheticDocument());
        body.addView(startButton);

        mediaStoreButton = new Button(this);
        mediaStoreButton.setText("Start MediaStore synthetic probe");
        mediaStoreButton.setOnClickListener(view -> startMediaStoreProbe());
        body.addView(mediaStoreButton);

        retestButton = new Button(this);
        retestButton.setText("Retest saved synthetic document (after app/reboot)");
        retestButton.setOnClickListener(view -> retestSavedDocument());
        body.addView(retestButton);

        replayButton = new Button(this);
        replayButton.setText("Replay saved metrics to KTN_S0 logcat");
        replayButton.setOnClickListener(view -> {
            int count = replayMetricHistory("manual");
            resultView.setText("Replayed " + count + " safe S0 metric records with "
                    + "historic=true; de-duplicate by metric_id when collecting logs.");
        });
        body.addView(replayButton);

        cleanupButton = new Button(this);
        cleanupButton.setText("Delete MY synthetic test files (manual)");
        cleanupButton.setOnClickListener(view -> deleteSavedSyntheticFiles());
        body.addView(cleanupButton);

        resultView = new TextView(this);
        resultView.setText("Idle; no files created.");
        body.addView(resultView);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(body);
        setContentView(scroll);
        restoreSavedSession();
        refreshButtons();
        replayMetricHistory("on_open");
        if (createdDocument != null) {
            resultView.setText("Saved " + (mediaStoreMode ? "MediaStore" : "SAF")
                    + " synthetic session (stage=" + preferences().getString(PREF_STAGE, "unknown")
                    + "). Retest after app restart/reboot, or delete it manually. "
                    + "No content or personal path was loaded.");
        }
    }

    private SharedPreferences preferences() {
        return getSharedPreferences(PREFS, MODE_PRIVATE);
    }

    private SharedPreferences metricPreferences() {
        return getSharedPreferences(METRIC_PREFS, MODE_PRIVATE);
    }

    private static boolean safeMetricEntry(String entry) {
        return entry != null && entry.length() <= MAX_METRIC_LINE_CHARS
                && METRIC_ENTRY.matcher(entry).matches();
    }

    private synchronized ArrayList<String> savedMetricHistory() {
        ArrayList<String> result = new ArrayList<>();
        String stored = metricPreferences().getString(METRIC_HISTORY, "");
        if (stored == null || stored.length() > MAX_METRIC_CHARS) return result;
        for (String entry : stored.split("\\n")) {
            if (safeMetricEntry(entry) && !result.contains(entry)
                    && result.size() < MAX_METRIC_LINES) result.add(entry);
        }
        return result;
    }

    private synchronized void persistAndLogMetric(String metric) {
        String entry = "metric_id=" + UUID.randomUUID() + " " + metric;
        if (!safeMetricEntry(entry)) {
            Log.e(TAG, "metric_rejected_invalid_schema");
            return;
        }
        ArrayList<String> entries = savedMetricHistory();
        entries.add(entry);
        String bounded = String.join("\n", entries);
        while (entries.size() > MAX_METRIC_LINES || bounded.length() > MAX_METRIC_CHARS) {
            entries.remove(0);
            bounded = String.join("\n", entries);
        }
        // Commit synchronously before logcat: a reboot may happen after a read.
        if (!metricPreferences().edit().putString(METRIC_HISTORY, bounded).commit())
            Log.e(TAG, "metric_history_commit_failed");
        Log.i(TAG, "historic=false replay=none " + entry);
    }

    private synchronized int replayMetricHistory(String source) {
        ArrayList<String> entries = savedMetricHistory();
        for (String entry : entries)
            Log.i(TAG, "historic=true replay=" + source + " " + entry);
        Log.i(TAG, "metric_history_replay=" + source + " count=" + entries.size());
        return entries.size();
    }

    private void restoreSavedSession() {
        String uri = preferences().getString(PREF_CREATED, null);
        if (uri == null) return;
        Uri saved = Uri.parse(uri);
        if (!"content".equals(saved.getScheme())) return;
        createdDocument = saved;
        String opened = preferences().getString(PREF_OPENED, null);
        openedDocument = opened == null ? null : Uri.parse(opened);
        mediaStoreMode = preferences().getBoolean(PREF_MEDIA, false);
        privateSample = new File(getFilesDir(), PRIVATE_NAME);
    }

    private void saveSession(String stage) throws IOException {
        if (createdDocument == null || !"content".equals(createdDocument.getScheme()))
            throw new ProbeFailure("invalid_synthetic_document_uri");
        if (!preferences().edit()
                .putString(PREF_CREATED, createdDocument.toString())
                .putString(PREF_OPENED, openedDocument == null ? null : openedDocument.toString())
                .putBoolean(PREF_MEDIA, mediaStoreMode)
                .putString(PREF_STAGE, stage)
                .commit()) {
            throw new ProbeFailure("session_metadata_not_durable");
        }
    }

    private void refreshButtons() {
        boolean hasSession = createdDocument != null;
        startButton.setEnabled(!busy && !hasSession);
        mediaStoreButton.setEnabled(!busy && !hasSession);
        retestButton.setEnabled(!busy && hasSession);
        replayButton.setEnabled(!busy && !savedMetricHistory().isEmpty());
        cleanupButton.setEnabled(!busy && hasSession);
    }

    private void requestSyntheticDocument() {
        if (!startOperation(false)) return;
        resultView.setText("Select a destination for the synthetic 1 GiB document.");
        Intent create = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        create.addCategory(Intent.CATEGORY_OPENABLE);
        create.setType("application/octet-stream");
        create.putExtra(Intent.EXTRA_TITLE, "kotonoha-s0-synthetic-1gib.bin");
        try {
            startActivityForResult(create, REQUEST_CREATE);
        } catch (RuntimeException failure) {
            completeUi("CREATE_DOCUMENT unavailable: " + failureKind(failure));
        }
    }

    private void startMediaStoreProbe() {
        if (!startOperation(true)) return;
        resultView.setText("Creating MediaStore + private synthetic streams (1 GiB each)...");
        launchWorker(this::createSyntheticFiles);
    }

    private boolean startOperation(boolean useMediaStore) {
        if (busy || createdDocument != null) return false;
        busy = true;
        mediaStoreMode = useMediaStore;
        createdDocument = null;
        openedDocument = null;
        privateSample = null;
        refreshButtons();
        return true;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_CREATE && requestCode != REQUEST_OPEN) return;
        if (!busy) return;

        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            if (requestCode == REQUEST_OPEN) {
                finishRun("reopen_cancelled", false, "");
            } else {
                completeUi("Document creation cancelled; no synthetic data written.");
            }
            return;
        }

        if (requestCode == REQUEST_CREATE) {
            createdDocument = data.getData();
            try {
                saveSession("writing");
            } catch (IOException failure) {
                finishRun("create_" + failureKind(failure), false, "");
                return;
            }
            resultView.setText("Creating private and SAF synthetic streams (1 GiB each)...");
            launchWorker(this::createSyntheticFiles);
            return;
        }

        Uri selected = data.getData();
        if (!sameDocument(createdDocument, selected)) {
            // Refuse all other files, even if a user picked a genuine GPK by accident.
            finishRun("different_document_refused", false, "");
            return;
        }
        int grantFlags = data.getFlags();
        launchWorker(() -> compareThreeRounds(selected, grantFlags));
    }

    private void createSyntheticFiles() {
        try {
            // Conservative same-volume guard: the private and SAF copies can both
            // consume internal storage. A 32 GB handset must have real free space.
            long available = new StatFs(getFilesDir().getAbsolutePath()).getAvailableBytes();
            if (available < 2L * SIZE_BYTES + FREE_HEADROOM) {
                throw new ProbeFailure("insufficient_storage");
            }

            if (mediaStoreMode) {
                createdDocument = createMediaStoreDocument();
                saveSession("writing");
            }
            privateSample = new File(getFilesDir(), PRIVATE_NAME);
            if (privateSample.exists())
                throw new ProbeFailure("private_baseline_already_exists");
            byte[] buffer = new byte[BLOCK_BYTES];
            Random random = new Random(0x5343484F4F4CL);
            try (OutputStream provider =
                         getContentResolver().openOutputStream(createdDocument, "wt")) {
                if (provider == null) throw new ProbeFailure("provider_output_unavailable");
                try (FileOutputStream local = new FileOutputStream(privateSample)) {
                    for (int index = 0; index < BLOCKS; ++index) {
                        if (Thread.currentThread().isInterrupted())
                            throw new ProbeFailure("interrupted");
                        random.nextBytes(buffer);
                        local.write(buffer);
                        provider.write(buffer);
                        if ((index + 1) % 128 == 0) {
                            int mebibytes = index + 1;
                            showStatus("Generated " + mebibytes + " / 1024 MiB; "
                                    + "synthetic bytes only.");
                        }
                    }
                    local.getFD().sync();
                    provider.flush();
                }
            }

            if (mediaStoreMode) {
                ContentValues published = new ContentValues();
                published.put(MediaStore.MediaColumns.IS_PENDING, 0);
                if (getContentResolver().update(createdDocument, published, null, null) != 1)
                    throw new ProbeFailure("mediastore_publish_failed");
                saveSession("ready");
                compareThreeRounds(createdDocument, 0);
                return;
            }
            saveSession("generated_pending_saf_grant");

            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) {
                    finishRun("activity_closed", false, "");
                    return;
                }
                resultView.setText("Synthetic files ready. Reopen ONLY the document "
                        + "you just created through the SAF picker.");
                Intent open = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                open.addCategory(Intent.CATEGORY_OPENABLE);
                open.setType("application/octet-stream");
                open.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                        | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
                try {
                    startActivityForResult(open, REQUEST_OPEN);
                } catch (RuntimeException failure) {
                    finishRun("OPEN_DOCUMENT_" + failureKind(failure), false, "");
                }
            });
        } catch (Exception failure) {
            finishRun("create_" + failureKind(failure), false, "");
        }
    }

    private Uri createMediaStoreDocument() throws IOException {
        ContentValues values = new ContentValues();
        values.put(MediaStore.MediaColumns.DISPLAY_NAME,
                "kotonoha-s0-" + System.currentTimeMillis() + ".bin");
        values.put(MediaStore.MediaColumns.MIME_TYPE, "application/octet-stream");
        values.put(MediaStore.MediaColumns.RELATIVE_PATH,
                Environment.DIRECTORY_DOWNLOADS + "/KotonohaS0/");
        values.put(MediaStore.MediaColumns.IS_PENDING, 1);
        Uri uri = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI,
                values);
        if (uri == null) throw new ProbeFailure("mediastore_insert_failed");
        return uri;
    }

    private boolean hasPersistedReadGrant(Uri uri) {
        if (uri == null) return false;
        return getContentResolver().getPersistedUriPermissions().stream()
                .anyMatch(grant -> uri.equals(grant.getUri()) && grant.isReadPermission());
    }

    private void retestSavedDocument() {
        if (busy || createdDocument == null) return;
        privateSample = new File(getFilesDir(), PRIVATE_NAME);
        if (!privateSample.isFile() || privateSample.length() != SIZE_BYTES) {
            resultView.setText("Private synthetic reference is incomplete or missing. "
                    + "Use Delete to clear this saved session; no personal file was opened.");
            return;
        }
        if (!"ready".equals(preferences().getString(PREF_STAGE, ""))
                && !"generated_pending_saf_grant".equals(
                        preferences().getString(PREF_STAGE, ""))) {
            resultView.setText("Synthetic generation was interrupted. "
                    + "Delete this session manually before creating a new one.");
            return;
        }
        busy = true;
        refreshButtons();
        resultView.setText("Retesting saved synthetic files: 3 full passes. "
                + "No regeneration, no copying.");
        if (mediaStoreMode) {
            launchWorker(() -> compareThreeRounds(createdDocument, 0));
        } else if (hasPersistedReadGrant(openedDocument)) {
            Log.i(TAG, "persisted_read_grant_on_retest=true");
            launchWorker(() -> compareThreeRounds(openedDocument, 0));
        } else {
            Log.i(TAG, "persisted_read_grant_on_retest=false");
            resultView.setText("Persisted SAF read grant is absent. Select ONLY "
                    + "the original S0 synthetic document to renew access.");
            Intent open = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            open.addCategory(Intent.CATEGORY_OPENABLE);
            open.setType("application/octet-stream");
            open.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            try {
                startActivityForResult(open, REQUEST_OPEN);
            } catch (RuntimeException failure) {
                finishRun("reopen_" + failureKind(failure), false, "");
            }
        }
    }

    private void compareThreeRounds(Uri selected, int grantFlags) {
        StringBuilder measurements = new StringBuilder();
        try {
            if (!mediaStoreMode) {
                if (grantFlags != 0) {
                    if ((grantFlags & Intent.FLAG_GRANT_READ_URI_PERMISSION) == 0)
                        throw new ProbeFailure("read_grant_missing");
                    getContentResolver().takePersistableUriPermission(selected,
                            Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    openedDocument = selected;
                    saveSession("ready");
                }
                if (!hasPersistedReadGrant(selected))
                    throw new ProbeFailure("persisted_grant_not_visible");
                Log.i(TAG, "persisted_read_grant=true");
            }

            try (ParcelFileDescriptor remote =
                         getContentResolver().openFileDescriptor(selected, "r");
                 ParcelFileDescriptor local =
                         ParcelFileDescriptor.open(privateSample,
                                 ParcelFileDescriptor.MODE_READ_ONLY)) {
                if (remote == null) throw new ProbeFailure("provider_fd_missing");
                FileDescriptor remoteFd = remote.getFileDescriptor();
                FileDescriptor localFd = local.getFileDescriptor();
                String procFdResult = probeProcFd(remote.getFd(), localFd);
                Log.i(TAG, procFdResult);
                measurements.append(procFdResult).append('\n');
                checkSeekableAndLength(remoteFd, "provider");
                checkSeekableAndLength(localFd, "private");

                byte[] remoteBytes = new byte[BLOCK_BYTES];
                byte[] localBytes = new byte[BLOCK_BYTES];
                for (int round = 0; round < 3; ++round) {
                    long remoteNanos = 0;
                    long localNanos = 0;
                    long[] remoteLatencies = new long[BLOCKS];
                    long[] localLatencies = new long[BLOCKS];
                    for (int step = 0; step < BLOCKS; ++step) {
                        if (Thread.currentThread().isInterrupted())
                            throw new ProbeFailure("interrupted");
                        int block = orderFor(round, step);
                        long offset = (long) block * BLOCK_BYTES;

                        long started = System.nanoTime();
                        preadFully(remoteFd, remoteBytes, offset);
                        remoteLatencies[step] = System.nanoTime() - started;
                        remoteNanos += remoteLatencies[step];

                        started = System.nanoTime();
                        preadFully(localFd, localBytes, offset);
                        localLatencies[step] = System.nanoTime() - started;
                        localNanos += localLatencies[step];

                        if (!Arrays.equals(remoteBytes, localBytes))
                            throw new ProbeFailure("content_mismatch_round_" + (round + 1));
                    }

                    String access = round == 0 ? "sequential" : "random";
                    String pattern = round == 0 ? "forward" :
                            round == 1 ? "reverse" : "strided";
                    String provider = telemetryLine(mediaStoreMode ? "mediastore" : "saf",
                            access, pattern, round + 1, remoteNanos, remoteLatencies);
                    String baseline = telemetryLine("private", access, pattern,
                            round + 1, localNanos, localLatencies);
                    persistAndLogMetric(provider);
                    persistAndLogMetric(baseline);
                    measurements.append(provider).append('\n').append(baseline).append('\n');
                    showStatus(measurements.toString());
                }
            }
            finishRun("pass", true, measurements.toString());
        } catch (Exception failure) {
            finishRun("compare_" + failureKind(failure), false,
                    measurements.toString());
        }
    }

    private static void checkSeekableAndLength(FileDescriptor fd, String label)
            throws ErrnoException, IOException {
        long size = Os.lseek(fd, 0, OsConstants.SEEK_END);
        if (size != SIZE_BYTES) throw new ProbeFailure(label + "_size_mismatch");
        if (Os.lseek(fd, SIZE_BYTES - 1, OsConstants.SEEK_SET) != SIZE_BYTES - 1)
            throw new ProbeFailure(label + "_lseek_failed");
        Os.lseek(fd, 0, OsConstants.SEEK_SET);
    }

    private static void preadFully(FileDescriptor fd, byte[] buffer, long offset)
            throws ErrnoException, IOException {
        int filled = 0;
        while (filled < buffer.length) {
            int count = Os.pread(fd, buffer, filled, buffer.length - filled,
                    offset + filled);
            if (count <= 0) throw new ProbeFailure("pread_short_read");
            filled += count;
        }
    }

    private static String probeProcFd(int descriptorNumber, FileDescriptor privateFd) {
        FileDescriptor duplicated = null;
        try {
            // Never resolve/readlink/log the symlink target (could reveal a user path).
            duplicated = Os.open("/proc/self/fd/" + descriptorNumber,
                    OsConstants.O_RDONLY | OsConstants.O_CLOEXEC, 0);
            try {
                byte[] viaProc = new byte[8192];
                byte[] expected = new byte[8192];
                preadFully(duplicated, viaProc, 0);
                preadFully(privateFd, expected, 0);
                boolean matched = Arrays.equals(viaProc, expected);
                return "proc_fd_reopen=true proc_fd_first8k_match=" + matched;
            } catch (ErrnoException failure) {
                return "proc_fd_reopen=true proc_fd_read_errno=" + failure.errno;
            } catch (IOException failure) {
                return "proc_fd_reopen=true proc_fd_first8k_match=false";
            }
        } catch (ErrnoException failure) {
            return "proc_fd_reopen=false proc_fd_errno=" + failure.errno;
        } finally {
            if (duplicated != null) {
                try { Os.close(duplicated); } catch (ErrnoException ignored) { }
            }
        }
    }

    private static int orderFor(int round, int step) {
        if (round == 0) return step;
        if (round == 1) return BLOCKS - 1 - step;
        return (step * 791 + 127) % BLOCKS; // 791 is coprime with 1024.
    }

    private static double throughput(long nanos) {
        return nanos <= 0 ? 0 : 1024.0e9 / nanos;
    }

    private static double latencyPercentileMillis(long[] samplesNanos, double quantile) {
        long[] sorted = Arrays.copyOf(samplesNanos, samplesNanos.length);
        Arrays.sort(sorted);
        // Nearest-rank percentile; zero-based index, clamped to available samples.
        int index = Math.max(0,
                Math.min(sorted.length - 1, (int) Math.ceil(quantile * sorted.length) - 1));
        return sorted[index] / 1e6;
    }

    private static String telemetryLine(String mode, String access, String pattern,
                                        int repetition, long nanos, long[] latencies) {
        return String.format(Locale.US,
                "test=%s mode=%s access=%s pattern=%s repetition=%d "
                        + "block_bytes=%d blocks=%d bytes=%d equal=true "
                        + "read_ms=%.3f throughput_mib_s=%.3f lat_p50_ms=%.3f lat_p95_ms=%.3f",
                TEST, mode, access, pattern, repetition, BLOCK_BYTES, BLOCKS,
                SIZE_BYTES, nanos / 1e6, throughput(nanos),
                latencyPercentileMillis(latencies, 0.50),
                latencyPercentileMillis(latencies, 0.95));
    }

    private boolean sameDocument(Uri expected, Uri selected) {
        if (expected == null || selected == null) return false;
        if (expected.equals(selected)) return true;
        try {
            return expected.getAuthority() != null
                    && expected.getAuthority().equals(selected.getAuthority())
                    && DocumentsContract.isDocumentUri(this, expected)
                    && DocumentsContract.isDocumentUri(this, selected)
                    && DocumentsContract.getDocumentId(expected)
                            .equals(DocumentsContract.getDocumentId(selected));
        } catch (IllegalArgumentException failure) {
            return false;
        }
    }

    private void finishRun(String outcome, boolean success, String metrics) {
        Log.i(TAG, "backend=" + (mediaStoreMode ? "mediastore" : "saf")
                + " result=" + (success ? "pass" : "fail") + " reason=" + outcome
                + " synthetic_data_retained=" + (createdDocument != null));
        String summary = metrics + "Result: " + outcome + ".\n"
                + "Synthetic data and persisted SAF grant (when obtained) are retained.\n"
                + "Use Retest after app/reboot; Delete removes only this S0 session.";
        runOnUiThread(() -> completeUi(summary));
    }

    private void deleteSavedSyntheticFiles() {
        if (busy || createdDocument == null) return;
        busy = true;
        refreshButtons();
        resultView.setText("Deleting only the synthetic files created by S0...");
        launchWorker(() -> {
            // Explicit Delete always purges metric history, even when the
            // document provider refuses deletion; SAF grants are untouched then.
            boolean metricsCleared = metricPreferences().edit().clear().commit();
            Log.i(TAG, "manual_cleanup_metric_history_cleared=" + metricsCleared);
            boolean providerDeleted;
            try {
                if (mediaStoreMode) {
                    providerDeleted = getContentResolver().delete(
                            createdDocument, null, null) == 1;
                } else {
                    providerDeleted = DocumentsContract.deleteDocument(
                            getContentResolver(), createdDocument);
                }
            } catch (Exception ignored) {
                providerDeleted = false;
            }
            if (!providerDeleted) {
                Log.i(TAG, "manual_cleanup_provider_deleted=false");
                runOnUiThread(() -> completeUi("Provider refused removal of its own "
                        + "synthetic document. Remove it manually in Files; "
                        + "S0 reference and grant remain saved for another attempt."));
                return;
            }
            privateSample = new File(getFilesDir(), PRIVATE_NAME);
            boolean privateDeleted = !privateSample.exists() || privateSample.delete();
            if (openedDocument != null && hasPersistedReadGrant(openedDocument)) {
                try {
                    getContentResolver().releasePersistableUriPermission(openedDocument,
                            Intent.FLAG_GRANT_READ_URI_PERMISSION);
                } catch (SecurityException ignored) {
                    // Provider or OS already revoked the grant after deletion.
                }
            }
            boolean cleared = preferences().edit().clear().commit();
            Log.i(TAG, "manual_cleanup_provider_deleted=true private_deleted="
                    + privateDeleted + " metadata_cleared=" + cleared);
            createdDocument = null;
            openedDocument = null;
            privateSample = null;
            final String summary = "Manual S0 cleanup: provider=true, private="
                    + privateDeleted + ", metadata=" + cleared + ". "
                    + (privateDeleted ? "A new synthetic session can be created."
                    : "Private S0 reference remains; remove app data before starting again.");
            runOnUiThread(() -> completeUi(summary));
        });
    }

    private void launchWorker(Runnable action) {
        worker = new Thread(action, "Kotonoha-S0-synthetic-probe");
        worker.start();
    }

    private void showStatus(String text) {
        runOnUiThread(() -> {
            if (!isFinishing() && !isDestroyed()) resultView.setText(text);
        });
    }

    private void completeUi(String text) {
        if (!isFinishing() && !isDestroyed()) {
            busy = false;
            refreshButtons();
            resultView.setText(text);
        }
    }

    private static String failureKind(Throwable failure) {
        if (failure instanceof ProbeFailure) return failure.getMessage();
        if (failure instanceof ErrnoException)
            return "errno_" + ((ErrnoException) failure).errno;
        if (failure instanceof SecurityException) return "security_denied";
        if (failure instanceof IOException) return "io_error";
        return "unsupported_runtime";
    }

    private static final class ProbeFailure extends IOException {
        ProbeFailure(String code) { super(code); }
    }

    @Override
    protected void onDestroy() {
        if (worker != null && worker.isAlive()) worker.interrupt();
        super.onDestroy();
    }
}
