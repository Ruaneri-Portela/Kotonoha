package org.kotonoha.android;

import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.util.Log;

import androidx.documentfile.provider.DocumentFile;

import java.io.File;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

/**
 * Storage Access Framework (SAF) bridge for persisting folder/disc URIs and
 * exposing read-only descriptor/listing helpers to the native launcher.
 */
public final class SafBridge {
    private static final String TAG = "KotonohaSAF";
    private static final String PREFS_NAME = "kotonoha_launcher_prefs";
    private static final String KEY_URIS = "persisted_library_uris_v1";
    private static final String KEY_SPLIT_SAVES = "split_saves_by_game_v1";
    private static final String KEY_RENDERER_BACKEND = "renderer_backend_v1";
    private static final String KEY_UPSCALER_BACKEND = "upscaler_backend_v1";
    private static final String KEY_UPSCALER_PRESET = "upscaler_preset_v1";
    private static final String KEY_UPSCALER_FACTOR = "upscaler_factor_v1";

    private SafBridge() {}

    public static void persistUriPermission(Context context, Uri uri) {
        if (context == null || uri == null) return;
        try {
            final int flags = Intent.FLAG_GRANT_READ_URI_PERMISSION;
            context.getContentResolver().takePersistableUriPermission(uri, flags);
        } catch (SecurityException error) {
            Log.w(TAG, "Persistable permission not granted for " + uri + ": " + error.getMessage());
        }
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        Set<String> current = new LinkedHashSet<>(prefs.getStringSet(KEY_URIS, new LinkedHashSet<>()));
        current.add(uri.toString());
        prefs.edit().putStringSet(KEY_URIS, current).apply();
    }

    public static void removePersistedUri(Context context, String uriString) {
        if (context == null || uriString == null) return;
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        Set<String> current = new LinkedHashSet<>(prefs.getStringSet(KEY_URIS, new LinkedHashSet<>()));
        current.remove(uriString);
        prefs.edit().putStringSet(KEY_URIS, current).apply();
        try {
            context.getContentResolver().releasePersistableUriPermission(
                    Uri.parse(uriString), Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception ignored) {
        }
    }

    public static List<String> getScanRoots(Context context) {
        List<String> roots = new ArrayList<>();
        File external = context.getExternalFilesDir(null);
        if (external != null) {
            File gamesDir = new File(external, "games");
            if (!gamesDir.exists()) {
                gamesDir.mkdirs();
            }
            roots.add(gamesDir.getAbsolutePath());
        }
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        Set<String> persisted = prefs.getStringSet(KEY_URIS, new LinkedHashSet<>());
        if (persisted != null) {
            for (String uriStr : persisted) {
                if (uriStr != null && !uriStr.isEmpty() && !roots.contains(uriStr)) {
                    roots.add(uriStr);
                }
            }
        }
        return roots;
    }

    public static boolean isSplitSavesEnabled(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        return prefs.getBoolean(KEY_SPLIT_SAVES, true);
    }

    public static void setSplitSavesEnabled(Context context, boolean enabled) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        prefs.edit().putBoolean(KEY_SPLIT_SAVES, enabled).apply();
    }

    public static String getRendererBackend(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        String value = prefs.getString(KEY_RENDERER_BACKEND, "");
        return value != null ? value : "";
    }

    public static void setRendererBackend(Context context, String backend) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        prefs.edit().putString(KEY_RENDERER_BACKEND, backend != null ? backend : "").apply();
    }

    public static String getUpscalerBackend(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        String value = prefs.getString(KEY_UPSCALER_BACKEND, "none");
        return (value != null && !value.isEmpty()) ? value : "none";
    }

    public static void setUpscalerBackend(Context context, String backend) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        prefs.edit().putString(KEY_UPSCALER_BACKEND, backend != null ? backend : "none").apply();
    }

    public static String getUpscalerPreset(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        String value = prefs.getString(KEY_UPSCALER_PRESET, "0");
        return (value != null && !value.isEmpty()) ? value : "0";
    }

    public static void setUpscalerPreset(Context context, String preset) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        prefs.edit().putString(KEY_UPSCALER_PRESET, preset != null ? preset : "0").apply();
    }

    public static String getUpscalerFactor(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        String value = prefs.getString(KEY_UPSCALER_FACTOR, "2.0");
        return (value != null && !value.isEmpty()) ? value : "2.0";
    }

    public static void setUpscalerFactor(Context context, String factor) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        prefs.edit().putString(KEY_UPSCALER_FACTOR, factor != null ? factor : "2.0").apply();
    }

    /**
     * Resolves a human-readable display name for a SAF URI or local file path.
     */
    public static String displayName(Context context, String pathOrUri) {
        if (pathOrUri == null || pathOrUri.isEmpty()) return "";
        if (!pathOrUri.startsWith("content://")) {
            return new File(pathOrUri).getName();
        }
        try {
            Uri uri = Uri.parse(pathOrUri);
            DocumentFile doc = DocumentsContract.isTreeUri(uri)
                    ? DocumentFile.fromTreeUri(context, uri)
                    : DocumentFile.fromSingleUri(context, uri);
            if (doc != null && doc.getName() != null) {
                return doc.getName();
            }
        } catch (Exception ignored) {
        }
        return pathOrUri;
    }

    /**
     * Opens a detached POSIX file descriptor for native 64-bit random access.
     * Caller owns the returned fd and must close() it. Returns -1 on error.
     */
    public static int openReadOnlyFd(Context context, String uriString) {
        if (context == null || uriString == null) return -1;
        try {
            ParcelFileDescriptor pfd = context.getContentResolver()
                    .openFileDescriptor(Uri.parse(uriString), "r");
            if (pfd != null) {
                return pfd.detachFd();
            }
        } catch (Exception error) {
            Log.e(TAG, "openReadOnlyFd failed for " + uriString, error);
        }
        return -1;
    }

    /**
     * Lists direct children of a SAF tree document as "docId|displayName|mimeType|size".
     */
    public static String[] listTreeChildren(Context context, String treeUriString, String parentDocId) {
        List<String> out = new ArrayList<>();
        if (context == null || treeUriString == null) return new String[0];
        ContentResolver resolver = context.getContentResolver();
        Uri treeUri = Uri.parse(treeUriString);
        String docId = (parentDocId == null || parentDocId.isEmpty())
                ? DocumentsContract.getTreeDocumentId(treeUri)
                : parentDocId;
        Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, docId);
        String[] projection = new String[]{
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
                DocumentsContract.Document.COLUMN_SIZE
        };
        try (Cursor cursor = resolver.query(childrenUri, projection, null, null, null)) {
            if (cursor != null) {
                while (cursor.moveToNext()) {
                    String id = cursor.getString(0);
                    String name = cursor.getString(1);
                    String mime = cursor.getString(2);
                    long size = cursor.getLong(3);
                    out.add((id != null ? id : "") + "|" +
                            (name != null ? name : "") + "|" +
                            (mime != null ? mime : "") + "|" + size);
                }
            }
        } catch (Exception error) {
            Log.e(TAG, "listTreeChildren failed for " + treeUriString, error);
        }
        return out.toArray(new String[0]);
    }
}
