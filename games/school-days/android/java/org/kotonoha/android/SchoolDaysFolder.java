package org.kotonoha.android;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.ImageDecoder;
import android.net.Uri;

import java.io.IOException;
import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.nio.file.Files;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import org.json.JSONObject;
import org.json.JSONException;
import java.util.List;
import java.util.Locale;
import java.util.Map;


/** Checks display names only. Native route and archive validation happens at launch. */
final class SchoolDaysFolder {
    final List<String> missing = new ArrayList<>();
    final List<String> languages = new ArrayList<>();
    private final Map<String, DocumentTree.Entry> archives;
    private final Map<String, DocumentTree.Entry> jsons;
    private final DocumentTree.Entry route;
    private final Map<String, DocumentTree.Entry> displayOverlay;
    Bitmap artwork;

    private static Map<String, DocumentTree.Entry> directory(DocumentTree source, Uri tree,
            Map<String, DocumentTree.Entry> entries, String name) throws IOException {
        DocumentTree.Entry entry = entries.get(name);
        return entry != null && entry.directory ? source.children(tree, entry.uri)
                : Collections.emptyMap();
    }

    private static boolean file(Map<String, DocumentTree.Entry> entries, String name) {
        DocumentTree.Entry entry = entries.get(name);
        return entry != null && !entry.directory && !entry.empty;
    }

    private static List<String> packNames() {
        List<String> names = new ArrayList<>();
        Collections.addAll(names, "BGM.GPK", "Ini.GPK", "Script.GPK", "SysSe.GPK", "System.GPK", "LxH.GPK");
        for (String prefix : new String[]{"Event", "Movie", "Se", "Voice"}) {
            for (int i = 0; i < 6; i++) names.add(prefix + "0" + i + ".GPK");
        }
        return names;
    }

    static List<String> missingPacks(Map<String, DocumentTree.Entry> entries) {
        Map<String, DocumentTree.Entry> files = indexPacks(entries);
        List<String> names = packNames();
        names.removeIf(name -> files.containsKey(name.toLowerCase(Locale.ROOT)));
        return names;
    }

    private static Map<String, DocumentTree.Entry> indexPacks(Map<String, DocumentTree.Entry> entries) {
        Map<String, DocumentTree.Entry> files = new HashMap<>();
        entries.forEach((name, entry) -> {
            String key = name.toLowerCase(Locale.ROOT);
            if (files.containsKey(key) || entry.directory || entry.empty) files.put(key, null);
            else files.put(key, entry);
        });
        files.values().removeIf(value -> value == null);
        return files;
    }

    // Only the small resources whose consumers require physical files go in cache.
    // The native mount opens each archive on demand using its provider-issued URI.
    File prepare(Context context, DocumentTree source, String locale) throws IOException {
        if (!missing.isEmpty() || !languages.contains(locale)) throw new IOException();
        File session = Files.createTempDirectory(context.getCacheDir().toPath(), "sd-saf-").toFile();
        try {
            JSONObject catalog = new JSONObject();
            for (String name : packNames()) {
                DocumentTree.Entry entry = archives.get(name.toLowerCase(Locale.ROOT));
                catalog.put(name, new JSONObject().put("uri", entry.uri.toString()).put("size", entry.size));
            }
            Files.write(new File(session, "documents.json").toPath(),
                    catalog.toString().getBytes(StandardCharsets.UTF_8));
            source.copy(route, new File(session, "school-days-hq.ktnroute"));
            File dictionaries = new File(session, "translations/jsons");
            Files.createDirectories(dictionaries.toPath());
            for (String name : new String[]{locale + ".json", locale + "-lxh.json"})
                source.copy(jsons.get(name), new File(dictionaries, name));
            for (String name : new String[]{"Option_Display.png", "Option_Display.cmap",
                    "Option_Display_Chip.png", "Option_Display_Wide.cmap",
                    "Option_Display_Wide_Full.cmap", "Option_Display_Wide_Note.cmap"}) {
                if (!file(displayOverlay, name)) continue;
                File folder = new File(session, "overlay/System/Option");
                Files.createDirectories(folder.toPath());
                source.copy(displayOverlay.get(name), new File(folder, name));
            }
            return session;
        } catch (IOException | RuntimeException | JSONException failure) {
            discard(session);
            throw new IOException("Cannot prepare selected resources", failure);
        }
    }

    static void discard(File session) {
        if (session == null) return;
        File[] children = Files.isSymbolicLink(session.toPath()) ? null : session.listFiles();
        if (children != null) for (File child : children) discard(child);
        session.delete();
    }

    static String[] launchArguments(File session, String locale) {
        List<String> args = new ArrayList<>();
        Collections.addAll(args, "--school-days", session.getPath(), "--document-catalog",
                new File(session, "documents.json").getPath(), "--translation",
                new File(session, "translations/jsons/" + locale + ".json").getPath());
        File overlay = new File(session, "overlay");
        if (overlay.isDirectory()) Collections.addAll(args, "--asset-overlay", overlay.getPath());
        else args.add("--no-asset-overlay");
        return args.toArray(new String[0]);
    }

    // Compatibility with earlier app-private installations. Never import from SAF.
    static boolean hasPrivateInstallation(File dataDir) {
        File assets = new File(dataDir, "game_assets");
        if (!existingFile(assets, "school-days-hq.ktnroute")) return false;
        for (File directory : new File[]{new File(assets, "Packs"), assets}) {
            if (packNames().stream().allMatch(name -> existingFile(directory, name))) return true;
        }
        return false;
    }

    private static boolean existingFile(File directory, String name) {
        File candidate = new File(directory, name);
        return candidate.isFile() && candidate.length() > 0;
    }

    SchoolDaysFolder(Context context, DocumentTree source, Uri tree) throws IOException {
        Map<String, DocumentTree.Entry> root = source.children(tree, null);
        Map<String, DocumentTree.Entry> packs = directory(source, tree, root, "Packs");
        Map<String, DocumentTree.Entry> assets = directory(source, tree, root, "game-assets");
        archives = indexPacks(packs);
        route = assets.get("school-days-hq.ktnroute");
        if (!file(assets, "school-days-hq.ktnroute")) missing.add("game-assets/school-days-hq.ktnroute");
        if (packs.isEmpty()) {
            missing.add("Packs/");
        } else {
            missing.addAll(missingPacks(packs));
        }
        Map<String, DocumentTree.Entry> translations = directory(source, tree, root, "translations");
        jsons = directory(source, tree, translations, "jsons");
        for (String locale : new String[]{"en", "es", "pt-BR", "ru", "zh-CN"}) {
            if (file(jsons, locale + ".json") && file(jsons, locale + "-lxh.json")) languages.add(locale);
        }
        Map<String, DocumentTree.Entry> overlay = directory(source, tree, root, "overlay");
        Map<String, DocumentTree.Entry> system = directory(source, tree, overlay, "System");
        displayOverlay = directory(source, tree, system, "Option");
        try {
            Map<String, DocumentTree.Entry> art = directory(source, tree, assets, "launcher-art");
            String name = "SchoolDaysHQ-Launcher.png";
            if (!file(art, name)) art = directory(source, tree, root, "assets");
            if (!file(art, name)) art = directory(source, tree, root, "launcher-art");
            if (file(art, name)) {
                ImageDecoder.Source image = ImageDecoder.createSource(context.getContentResolver(), art.get(name).uri);
                artwork = ImageDecoder.decodeBitmap(image, (decoder, info, src) -> {
                    int longest = Math.max(info.getSize().getWidth(), info.getSize().getHeight());
                    int limit = Math.max(1, context.getResources().getDisplayMetrics().widthPixels);
                    decoder.setTargetSampleSize((int)Math.max(1L, (longest + (long)limit - 1) / limit));
                });
            }
        } catch (IOException | RuntimeException ignored) {
            // Optional artwork. Keep the neutral header and the folder report.
        }
    }
}
