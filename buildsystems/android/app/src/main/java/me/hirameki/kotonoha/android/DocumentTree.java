package org.kotonoha.android;

import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;

import java.io.IOException;
import java.io.File;
import java.io.InputStream;
import java.nio.file.Files;
import java.util.HashMap;
import java.util.Map;

/** A persisted, read-only SAF tree. Directory listings never open file data. */
public final class DocumentTree {
    private final ContentResolver resolver;
    private final SharedPreferences selection;
    private static final int READ = Intent.FLAG_GRANT_READ_URI_PERMISSION;

    public DocumentTree(Context context) {
        resolver = context.getContentResolver();
        selection = context.getSharedPreferences("document-tree", Context.MODE_PRIVATE);
    }

    public static Intent picker() {
        return new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).addFlags(READ
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
    }

    public Uri selected() {
        String uri = selection.getString("root", null);
        return uri == null ? null : Uri.parse(uri);
    }

    public boolean canRead(Uri uri) {
        for (UriPermission grant : resolver.getPersistedUriPermissions()) {
            if (grant.isReadPermission() && grant.getUri().equals(uri)) return true;
        }
        return false;
    }

    public void remember(Intent result) throws IOException {
        Uri uri = result.getData();
        if (uri == null || !DocumentsContract.isTreeUri(uri)
                || (result.getFlags() & READ) == 0) throw new IOException();
        Uri old = selected();
        resolver.takePersistableUriPermission(uri, READ);
        if (!canRead(uri)) throw new IOException();
        if (!selection.edit().putString("root", uri.toString()).commit()) {
            if (!uri.equals(old)) resolver.releasePersistableUriPermission(uri, READ);
            throw new IOException();
        }
        if (old != null && !old.equals(uri) && canRead(old)) {
            try {
                resolver.releasePersistableUriPermission(old, READ);
            } catch (SecurityException ignored) { /* Already revoked. */ }
        }
    }

    public void forget() {
        Uri old = selected();
        if (old != null && canRead(old)) {
            resolver.releasePersistableUriPermission(old, READ);
        }
        selection.edit().remove("root").apply();
    }

    public static final class Entry {
        public final Uri uri;
        public final boolean directory;
        public final boolean empty;
        public final long size;

        Entry(Uri uri, boolean directory, long size) {
            this.uri = uri;
            this.directory = directory;
            this.empty = size == 0;
            this.size = size;
        }
    }

    public void copy(Entry entry, File destination) throws IOException {
        try (InputStream input = resolver.openInputStream(entry.uri)) {
            if (input == null) throw new IOException();
            long copied = Files.copy(input, destination.toPath());
            if (entry.size >= 0 && copied != entry.size) throw new IOException();
        }
    }

    public Map<String, Entry> children(Uri tree, Uri parent) throws IOException {
        String id = parent == null ? DocumentsContract.getTreeDocumentId(tree)
                : DocumentsContract.getDocumentId(parent);
        Uri listing = DocumentsContract.buildChildDocumentsUriUsingTree(tree, id);
        String[] fields = { Document.COLUMN_DISPLAY_NAME, Document.COLUMN_DOCUMENT_ID,
                Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE };
        Map<String, Entry> entries = new HashMap<>();
        try (Cursor rows = resolver.query(listing, fields, null, null, null)) {
            if (rows == null || rows.getExtras().getBoolean(DocumentsContract.EXTRA_LOADING)
                    || rows.getExtras().containsKey(DocumentsContract.EXTRA_ERROR)) throw new IOException();
            while (rows.moveToNext()) {
                if (Thread.currentThread().isInterrupted()) throw new IOException();
                Uri child = DocumentsContract.buildDocumentUriUsingTree(tree, rows.getString(1));
                String name = rows.getString(0);
                if (name == null || entries.put(name, new Entry(child,
                        Document.MIME_TYPE_DIR.equals(rows.getString(2)),
                        rows.isNull(3) ? -1 : rows.getLong(3))) != null) throw new IOException();
            }
        }
        return entries;
    }
}
