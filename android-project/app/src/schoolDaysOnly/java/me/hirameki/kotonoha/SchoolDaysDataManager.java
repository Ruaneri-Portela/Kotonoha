package me.hirameki.kotonoha;

import android.content.ContentResolver;
import android.content.Context;
import android.content.res.AssetManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.StatFs;
import android.provider.DocumentsContract;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

final class SchoolDaysDataManager {
    interface Progress {
        void update(String message, int completed, int total);
    }

    static final class ImportResult {
        final boolean success;
        final String message;

        ImportResult(boolean success, String message) {
            this.success = success;
            this.message = message;
        }
    }

    private static final String ROUTE = "school-days-hq.ktnroute";
    private static final String STYLES = "styles.skot";
    private static final String READY_MARKER = ".import-ready-v1";
    private static final String[] REQUIRED_PACKS = {
            "BGM.GPK", "Event00.GPK", "Event01.GPK", "Event02.GPK",
            "Event03.GPK", "Event04.GPK", "Event05.GPK", "Ini.GPK",
            "Movie00.GPK", "Movie01.GPK", "Movie02.GPK", "Movie03.GPK",
            "Movie04.GPK", "Movie05.GPK", "Script.GPK", "Se00.GPK",
            "Se01.GPK", "Se02.GPK", "Se03.GPK", "Se04.GPK", "Se05.GPK",
            "SysSe.GPK", "System.gpk", "Voice00.GPK", "Voice01.GPK",
            "Voice02.GPK", "Voice03.GPK", "Voice04.GPK", "Voice05.GPK"
    };

    private SchoolDaysDataManager() {}

    static File gameRoot(Context context) {
        File base = context.getExternalFilesDir(null);
        if (base == null) base = context.getFilesDir();
        return new File(base, "SchoolDaysHQ");
    }

    static boolean ensureBootstrap(Context context) {
        File root = gameRoot(context);
        if (!root.mkdirs() && !root.isDirectory()) return false;
        try {
            copyAssetAtomic(context.getAssets(), ROUTE, new File(root, ROUTE));
            copyAssetAtomic(context.getAssets(), STYLES, new File(root, STYLES));
            return true;
        } catch (IOException error) {
            return false;
        }
    }

    static boolean isReady(Context context) {
        File root = gameRoot(context);
        if (!new File(root, READY_MARKER).isFile()
                || !new File(root, ROUTE).isFile()
                || !new File(root, STYLES).isFile()) {
            return false;
        }
        File packs = new File(root, "Packs");
        if (!packs.isDirectory()) return false;
        Map<String, File> present = new HashMap<>();
        File[] files = packs.listFiles();
        if (files == null) return false;
        for (File file : files) {
            if (file.isFile()) present.put(file.getName().toLowerCase(Locale.ROOT), file);
        }
        for (String required : REQUIRED_PACKS) {
            File file = present.get(required.toLowerCase(Locale.ROOT));
            if (file == null || file.length() <= 0) return false;
        }
        return true;
    }

    static ImportResult importTree(Context context, Uri treeUri, Progress progress) {
        try {
            if (!ensureBootstrap(context)) {
                return new ImportResult(false,
                        "Não foi possível preparar a rota e os estilos incluídos no aplicativo.");
            }

            ContentResolver resolver = context.getContentResolver();
            Uri selectedRoot = DocumentsContract.buildDocumentUriUsingTree(
                    treeUri, DocumentsContract.getTreeDocumentId(treeUri));
            Map<String, DocumentEntry> selectedEntries =
                    listChildren(resolver, treeUri, selectedRoot);

            // Accept either the retail Packs directory itself or its parent.
            // This keeps the user's distributable data folder simple while the
            // native engine still receives ordinary real filesystem paths.
            Map<String, DocumentEntry> packEntries = selectedEntries;
            if (!containsAllRequiredPacks(packEntries)) {
                DocumentEntry packs = selectedEntries.get("packs");
                if (packs == null || !packs.directory) {
                    return new ImportResult(false,
                            "Pasta inválida. Selecione Packs ou a pasta que contém Packs.");
                }
                packEntries = listChildren(
                        resolver, treeUri,
                        DocumentsContract.buildDocumentUriUsingTree(treeUri, packs.documentId));
            }

            List<DocumentEntry> orderedPacks = new ArrayList<>();
            for (String required : REQUIRED_PACKS) {
                DocumentEntry entry = packEntries.get(required.toLowerCase(Locale.ROOT));
                if (entry == null || entry.directory || entry.size == 0) {
                    return new ImportResult(false, "GPK obrigatório ausente ou vazio: " + required);
                }
                orderedPacks.add(entry);
            }

            File rootOut = gameRoot(context);
            File packsOut = new File(rootOut, "Packs");
            if (!packsOut.mkdirs() && !packsOut.isDirectory()) {
                return new ImportResult(false, "Não foi possível criar a pasta de dados do jogo.");
            }

            // Imports are one-file-at-a-time. Missing files remain on disk while
            // subsequent files are copied; existing files only need enough free
            // room for the largest temporary replacement.
            long missingBytes = 0;
            long replacementPeak = 0;
            for (int i = 0; i < orderedPacks.size(); ++i) {
                long size = Math.max(0, orderedPacks.get(i).size);
                File destination = new File(packsOut, REQUIRED_PACKS[i]);
                if (destination.isFile()) {
                    replacementPeak = Math.max(replacementPeak, size);
                } else {
                    missingBytes += size;
                }
            }
            long bytesNeeded = missingBytes + replacementPeak;
            long available = new StatFs(rootOut.getAbsolutePath()).getAvailableBytes();
            if (bytesNeeded > available) {
                return new ImportResult(false,
                        "Espaço insuficiente. Necessário aproximadamente "
                                + humanBytes(bytesNeeded) + ", disponível " + humanBytes(available) + ".");
            }

            // From this point an interrupted import must never be considered
            // playable. The marker is recreated only after all 29 replacements
            // have completed and been size-checked.
            File marker = new File(rootOut, READY_MARKER);
            if (marker.exists() && !marker.delete()) {
                return new ImportResult(false, "Não foi possível iniciar uma atualização segura dos dados.");
            }

            final int total = REQUIRED_PACKS.length;
            int done = 0;
            for (int i = 0; i < orderedPacks.size(); ++i) {
                copyDocumentAtomic(resolver, treeUri, orderedPacks.get(i),
                        new File(packsOut, REQUIRED_PACKS[i]));
                progress.update("Importando " + REQUIRED_PACKS[i] + "...", ++done, total);
            }

            writeReadyMarker(marker);
            if (!isReady(context)) {
                marker.delete();
                return new ImportResult(false,
                        "A importação terminou, mas a validação final dos dados falhou.");
            }
            return new ImportResult(true, "Dados do School Days HQ importados com sucesso.");
        } catch (Exception error) {
            new File(gameRoot(context), READY_MARKER).delete();
            return new ImportResult(false, "Falha ao importar dados: " + error.getMessage());
        }
    }

    private static boolean containsAllRequiredPacks(Map<String, DocumentEntry> entries) {
        for (String required : REQUIRED_PACKS) {
            DocumentEntry entry = entries.get(required.toLowerCase(Locale.ROOT));
            if (entry == null || entry.directory) return false;
        }
        return true;
    }

    private static void copyAssetAtomic(AssetManager assets, String name,
                                        File destination) throws IOException {
        File parent = destination.getParentFile();
        if (parent != null && !parent.mkdirs() && !parent.isDirectory()) {
            throw new IOException("Não foi possível criar " + parent.getAbsolutePath());
        }
        File temp = new File(destination.getAbsolutePath() + ".bootstrap-part");
        if (temp.exists() && !temp.delete()) {
            throw new IOException("Falha ao limpar bootstrap temporário.");
        }
        try (InputStream in = assets.open(name);
             FileOutputStream out = new FileOutputStream(temp)) {
            copyStream(in, out);
            out.getFD().sync();
        } catch (IOException error) {
            temp.delete();
            throw error;
        }
        replaceAtomic(temp, destination);
    }

    private static void copyDocumentAtomic(ContentResolver resolver, Uri treeUri,
                                           DocumentEntry source, File destination)
            throws IOException {
        File parent = destination.getParentFile();
        if (parent != null && !parent.mkdirs() && !parent.isDirectory()) {
            throw new IOException("Não foi possível criar " + parent.getAbsolutePath());
        }
        File temp = new File(destination.getAbsolutePath() + ".part");
        if (temp.exists() && !temp.delete()) {
            throw new IOException("Falha ao limpar arquivo temporário.");
        }
        Uri sourceUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, source.documentId);
        try (InputStream in = resolver.openInputStream(sourceUri);
             FileOutputStream out = new FileOutputStream(temp)) {
            if (in == null) throw new IOException("Documento não pôde ser aberto: " + source.name);
            copyStream(in, out);
            out.getFD().sync();
        } catch (IOException error) {
            temp.delete();
            throw error;
        }
        if (source.size >= 0 && temp.length() != source.size) {
            temp.delete();
            throw new IOException("Tamanho inválido após copiar " + source.name);
        }
        replaceAtomic(temp, destination);
    }

    private static void copyStream(InputStream in, FileOutputStream out) throws IOException {
        byte[] buffer = new byte[1024 * 1024];
        int read;
        while ((read = in.read(buffer)) >= 0) {
            if (read > 0) out.write(buffer, 0, read);
        }
    }

    private static void replaceAtomic(File temp, File destination) throws IOException {
        try {
            Files.move(temp.toPath(), destination.toPath(),
                    StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
        } catch (AtomicMoveNotSupportedException error) {
            Files.move(temp.toPath(), destination.toPath(), StandardCopyOption.REPLACE_EXISTING);
        } finally {
            if (temp.exists()) temp.delete();
        }
    }

    private static void writeReadyMarker(File marker) throws IOException {
        try (FileOutputStream out = new FileOutputStream(marker)) {
            out.write(("School Days HQ GPK import v1\ncount=" + REQUIRED_PACKS.length + "\n")
                    .getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        }
    }

    private static Map<String, DocumentEntry> listChildren(ContentResolver resolver,
                                                            Uri treeUri, Uri parentUri) {
        String parentId = DocumentsContract.getDocumentId(parentUri);
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parentId);
        String[] projection = {
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
                DocumentsContract.Document.COLUMN_SIZE
        };
        Map<String, DocumentEntry> result = new HashMap<>();
        try (Cursor cursor = resolver.query(children, projection, null, null, null)) {
            if (cursor == null) return result;
            int idColumn = cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DOCUMENT_ID);
            int nameColumn = cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DISPLAY_NAME);
            int mimeColumn = cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_MIME_TYPE);
            int sizeColumn = cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_SIZE);
            while (cursor.moveToNext()) {
                String id = cursor.getString(idColumn);
                String name = cursor.getString(nameColumn);
                String mime = cursor.getString(mimeColumn);
                long size = cursor.isNull(sizeColumn) ? -1 : cursor.getLong(sizeColumn);
                if (name == null || id == null) continue;
                result.put(name.toLowerCase(Locale.ROOT),
                        new DocumentEntry(id, name,
                                DocumentsContract.Document.MIME_TYPE_DIR.equals(mime), size));
            }
        }
        return result;
    }

    private static String humanBytes(long value) {
        double gib = value / (1024.0 * 1024.0 * 1024.0);
        return String.format(Locale.ROOT, "%.2f GB", gib);
    }

    private static final class DocumentEntry {
        final String documentId;
        final String name;
        final boolean directory;
        final long size;

        DocumentEntry(String documentId, String name, boolean directory, long size) {
            this.documentId = documentId;
            this.name = name;
            this.directory = directory;
            this.size = size;
        }
    }
}
