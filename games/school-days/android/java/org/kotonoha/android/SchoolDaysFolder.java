package org.kotonoha.android;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.ImageDecoder;
import android.net.Uri;

import java.io.IOException;
import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;

/** Checks display names only. Native route and archive validation happens at launch. */
final class SchoolDaysFolder {
    final List<String> missing = new ArrayList<>();
    final List<String> languages = new ArrayList<>();
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
        if (!file(assets, "school-days-hq.ktnroute")) missing.add("game-assets/school-days-hq.ktnroute");
        if (packs.isEmpty()) {
            missing.add("Packs/");
        } else {
            for (String name : packNames()) {
                if (!file(packs, name)) missing.add(name);
            }
        }
        Map<String, DocumentTree.Entry> translations = directory(source, tree, root, "translations");
        Map<String, DocumentTree.Entry> jsons = directory(source, tree, translations, "jsons");
        for (String locale : new String[]{"en", "es", "pt-BR", "ru", "zh-CN"}) {
            if (file(jsons, locale + ".json") && file(jsons, locale + "-lxh.json")) languages.add(locale);
        }
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
