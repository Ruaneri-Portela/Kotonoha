package org.kotonoha.android;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.ImageDecoder;
import android.net.Uri;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/** Checks display names and stages small SAF resources. Native route/GPK validation runs at launch. */
final class SchoolDaysFolder {
    static final String[] SUPPORTED_LOCALES = {"en", "pt-BR", "es", "ru", "zh-CN"};
    private static final long MAX_STAGED_OVERLAY_BYTES = 64L * 1024L * 1024L;
    private static final long MAX_STAGED_SUBTITLE_BYTES = 16L * 1024L * 1024L;

    final List<String> missing = new ArrayList<>();
    /** Base HQ JSON dictionaries present (usable when LxH is OFF or unavailable). */
    final List<String> baseJsonLanguages = new ArrayList<>();
    /** Complete JSON pairs (HQ + LxH). */
    final List<String> languages = new ArrayList<>();
    /** Incomplete JSON entries (missing HQ or LxH pair). */
    final List<String> incompleteJsonLanguages = new ArrayList<>();
    /** Base HQ native GPK packs present (English always uses Packs/Script.GPK). */
    final List<String> baseGpkLanguages = new ArrayList<>();
    /** Complete native GPK translation packs (Script + LxH). */
    final List<String> gpkLanguages = new ArrayList<>();
    final boolean selectedPacksDirectly;
    final boolean hasStylesAss;
    final boolean hasForcedSubtitles;
    final boolean hasOverlay;
    final int packCount;
    final int retailPackCount;
    final boolean hasLxhPack;

    private final Uri treeUri;
    private final Map<String, DocumentTree.Entry> archives;
    private final Map<String, DocumentTree.Entry> jsons;
    private final Map<String, DocumentTree.Entry> scripts;
    private final DocumentTree.Entry route;
    private final DocumentTree.Entry stylesAss;
    private final DocumentTree.Entry forcedSubtitlesDir;
    private final DocumentTree.Entry overlayDir;
    Bitmap artwork;

    static String gpkScriptFilename(String locale) {
        if ("en".equals(locale)) return null; // English HQ always uses Packs/Script.GPK
        if ("pt-BR".equals(locale)) return "Script - Brazilian.GPK";
        if ("es".equals(locale)) return "Script - Spanish.GPK";
        if ("ru".equals(locale)) return "Script - Russian.GPK";
        if ("zh-CN".equals(locale)) return "Script - chinese-compatible.GPK";
        return null;
    }

    static String gpkLxhFilename(String locale) {
        return locale + "-lxh.GPK";
    }

    List<String> missingGpkFiles(String locale, boolean requireLxh) {
        List<String> absent = new ArrayList<>();
        String scriptName = gpkScriptFilename(locale);
        if (scriptName == null) {
            if (!archives.containsKey("script.gpk")) absent.add("Packs/Script.GPK");
        } else if (!file(scripts, scriptName)) {
            absent.add(scriptName);
        }
        if (requireLxh) {
            String lxhName = gpkLxhFilename(locale);
            if (!file(scripts, lxhName)) {
                absent.add(lxhName);
            }
        }
        return absent;
    }

    List<String> missingGpkFiles(String locale) {
        return missingGpkFiles(locale, false);
    }

    boolean hasLegacyIncompatibleChineseGpk() {
        return file(scripts, "Script - chinese.GPK")
                && !file(scripts, "Script - chinese-compatible.GPK");
    }

    static String formatLanguageSelection(String backend, String locale) {
        return backend + ":" + locale;
    }

    static boolean isGpkSelection(String selection) {
        return selection != null && selection.startsWith("gpk:");
    }

    static String selectionLocale(String selection) {
        if (selection == null) return "en";
        int sep = selection.indexOf(':');
        return sep >= 0 ? selection.substring(sep + 1) : selection;
    }

    boolean supportsSelection(String selection) {
        if (selection == null || selection.isEmpty()) return false;
        String loc = selectionLocale(selection);
        return isGpkSelection(selection)
                ? baseGpkLanguages.contains(loc)
                : baseJsonLanguages.contains(loc);
    }

    boolean supportsLxhForSelection(String selection) {
        if (!hasLxhPack || selection == null || selection.isEmpty()) return false;
        String loc = selectionLocale(selection);
        return isGpkSelection(selection)
                ? gpkLanguages.contains(loc)
                : (languages.contains(loc) || ("es".equals(loc) && baseJsonLanguages.contains("es")));
    }

    String defaultSelection() {
        if (baseGpkLanguages.contains("pt-BR")) return formatLanguageSelection("gpk", "pt-BR");
        if (baseGpkLanguages.contains("en")) return formatLanguageSelection("gpk", "en");
        if (!baseGpkLanguages.isEmpty()) return formatLanguageSelection("gpk", baseGpkLanguages.get(0));
        if (baseJsonLanguages.contains("pt-BR")) return formatLanguageSelection("json", "pt-BR");
        if (!baseJsonLanguages.isEmpty()) return formatLanguageSelection("json", baseJsonLanguages.get(0));
        return null;
    }

    DocumentTree.Entry archiveEntry(String canonicalName) {
        return archives.get(canonicalName.toLowerCase(Locale.ROOT));
    }

    private static Map<String, DocumentTree.Entry> directory(DocumentTree source, Uri tree,
            Map<String, DocumentTree.Entry> entries, String name) throws IOException {
        DocumentTree.Entry entry = entries.get(name);
        return entry != null && entry.directory ? source.children(tree, entry.uri)
                : Collections.emptyMap();
    }

    private static DocumentTree.Entry directoryEntry(Map<String, DocumentTree.Entry> entries, String name) {
        DocumentTree.Entry entry = entries.get(name);
        return entry != null && entry.directory ? entry : null;
    }

    private static boolean file(Map<String, DocumentTree.Entry> entries, String name) {
        DocumentTree.Entry entry = entries.get(name);
        return entry != null && !entry.directory && !entry.empty;
    }

    private static boolean safeSegment(String name) {
        return name != null && !name.isEmpty() && !".".equals(name) && !"..".equals(name)
                && name.indexOf('/') < 0 && name.indexOf('\\') < 0 && name.indexOf('\0') < 0;
    }

    private static List<String> retailPackNames() {
        List<String> names = new ArrayList<>();
        Collections.addAll(names, "BGM.GPK", "Ini.GPK", "Script.GPK", "SysSe.GPK", "System.GPK");
        for (String prefix : new String[]{"Event", "Movie", "Se", "Voice"}) {
            for (int i = 0; i < 6; i++) names.add(prefix + "0" + i + ".GPK");
        }
        return names;
    }

    static List<String> missingPacks(Map<String, DocumentTree.Entry> entries) {
        Map<String, DocumentTree.Entry> files = indexPacks(entries);
        List<String> names = retailPackNames();
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

    private static void copyTree(DocumentTree source, Uri tree, DocumentTree.Entry dir,
            File destination, long maxFileBytes, int depth) throws IOException {
        if (dir == null || depth > 8) return;
        Map<String, DocumentTree.Entry> children = source.children(tree, dir.uri);
        for (Map.Entry<String, DocumentTree.Entry> item : children.entrySet()) {
            String name = item.getKey();
            DocumentTree.Entry entry = item.getValue();
            if (!safeSegment(name)) continue;
            File target = new File(destination, name);
            if (entry.directory) {
                copyTree(source, tree, entry, target, maxFileBytes, depth + 1);
            } else if (!entry.empty && (entry.size < 0 || entry.size <= maxFileBytes)) {
                Files.createDirectories(destination.toPath());
                source.copy(entry, target);
            }
        }
    }

    private static void copyForcedSubtitlesForLocale(DocumentTree source, Uri tree,
            DocumentTree.Entry rootDir, File destination, String locale) throws IOException {
        if (rootDir == null) return;
        Map<String, DocumentTree.Entry> children = source.children(tree, rootDir.uri);
        String wantedLower = locale.toLowerCase(Locale.ROOT);
        for (Map.Entry<String, DocumentTree.Entry> item : children.entrySet()) {
            String name = item.getKey();
            DocumentTree.Entry entry = item.getValue();
            if (!safeSegment(name)) continue;
            if (entry.directory) {
                if (name.toLowerCase(Locale.ROOT).equals(wantedLower)) {
                    copyTree(source, tree, entry, new File(destination, name),
                            MAX_STAGED_SUBTITLE_BYTES, 1);
                }
            } else if ("en".equals(locale) && !entry.empty
                    && name.toLowerCase(Locale.ROOT).endsWith(".ass")
                    && (entry.size < 0 || entry.size <= MAX_STAGED_SUBTITLE_BYTES)) {
                Files.createDirectories(destination.toPath());
                source.copy(entry, new File(destination, name));
            }
        }
    }

    // Only the small resources whose consumers require physical files go in cache.
    // The native mount opens each retail/optional-LxH archive on demand via provider-issued URIs.
    File prepare(Context context, DocumentTree source, String selection, boolean effectiveLxh) throws IOException {
        if (!missing.isEmpty() || !supportsSelection(selection)) throw new IOException();
        boolean useGpk = isGpkSelection(selection);
        String locale = selectionLocale(selection);
        boolean stageLxh = effectiveLxh && supportsLxhForSelection(selection);
        File session = Files.createTempDirectory(context.getCacheDir().toPath(), "sd-saf-").toFile();
        try {
            JSONObject catalog = new JSONObject();
            for (String name : retailPackNames()) {
                DocumentTree.Entry entry = archives.get(name.toLowerCase(Locale.ROOT));
                catalog.put(name, new JSONObject().put("uri", entry.uri.toString()).put("size", entry.size));
            }
            if (stageLxh && hasLxhPack) {
                DocumentTree.Entry lxhEntry = archives.get("lxh.gpk");
                if (lxhEntry != null) {
                    catalog.put("LxH.GPK", new JSONObject().put("uri", lxhEntry.uri.toString()).put("size", lxhEntry.size));
                }
            }
            Files.write(new File(session, "documents.json").toPath(),
                    catalog.toString().getBytes(StandardCharsets.UTF_8));
            source.copy(route, new File(session, "school-days-hq.ktnroute"));

            if (stylesAss != null) {
                File gameAssetsDir = new File(session, "game-assets");
                Files.createDirectories(gameAssetsDir.toPath());
                source.copy(stylesAss, new File(gameAssetsDir, "styles.ass"));
            }

            if (useGpk) {
                File scriptsDir = new File(session, "translations/scripts");
                Files.createDirectories(scriptsDir.toPath());
                String scriptName = gpkScriptFilename(locale);
                if (scriptName != null) {
                    source.copy(scripts.get(scriptName), new File(scriptsDir, scriptName));
                }
                if (stageLxh) {
                    String lxhName = gpkLxhFilename(locale);
                    if (file(scripts, lxhName)) {
                        source.copy(scripts.get(lxhName), new File(scriptsDir, lxhName));
                    }
                }
            } else {
                File dictionaries = new File(session, "translations/jsons");
                Files.createDirectories(dictionaries.toPath());
                source.copy(jsons.get(locale + ".json"), new File(dictionaries, locale + ".json"));
                if (stageLxh && file(jsons, locale + "-lxh.json")) {
                    source.copy(jsons.get(locale + "-lxh.json"), new File(dictionaries, locale + "-lxh.json"));
                }
                for (String sidecar : new String[]{
                        locale + "-reviewed-identities.json",
                        locale + "-lxh-reviewed-identities.json"}) {
                    if (file(jsons, sidecar)) {
                        source.copy(jsons.get(sidecar), new File(dictionaries, sidecar));
                    }
                }
            }

            if (forcedSubtitlesDir != null) {
                File stagedForced = new File(session, "game-assets/forced_subtitles");
                copyForcedSubtitlesForLocale(source, treeUri, forcedSubtitlesDir, stagedForced, locale);
            }

            if (overlayDir != null) {
                File stagedOverlay = new File(session, "overlay");
                copyTree(source, treeUri, overlayDir, stagedOverlay, MAX_STAGED_OVERLAY_BYTES, 0);
            }

            pruneStaleSessions(context.getCacheDir(), session);
            return session;
        } catch (IOException | RuntimeException | JSONException failure) {
            discard(session);
            throw new IOException("Cannot prepare selected resources", failure);
        }
    }

    static void pruneStaleSessions(File cacheDir, File keep) {
        if (cacheDir == null || !cacheDir.isDirectory()) return;
        File[] entries = cacheDir.listFiles();
        if (entries == null) return;
        for (File entry : entries) {
            if (entry == null || entry.equals(keep)) continue;
            if (entry.getName().startsWith("sd-saf-") && entry.isDirectory()) {
                discard(entry);
            }
        }
    }

    static void discard(File session) {
        if (session == null) return;
        File[] children = Files.isSymbolicLink(session.toPath()) ? null : session.listFiles();
        if (children != null) for (File child : children) discard(child);
        session.delete();
    }

    static String[] launchArguments(File session, String selection, boolean effectiveLxh) {
        List<String> args = new ArrayList<>();
        String locale = selectionLocale(selection);
        Collections.addAll(args, "--school-days", session.getPath(), "--document-catalog",
                new File(session, "documents.json").getPath());
        args.add(effectiveLxh ? "--lxh" : "--no-lxh");
        if (isGpkSelection(selection)) {
            Collections.addAll(args, "--gpk-locale", locale);
        } else {
            Collections.addAll(args, "--translation",
                    new File(session, "translations/jsons/" + locale + ".json").getPath());
        }
        File styles = new File(session, "game-assets/styles.ass");
        if (styles.isFile()) {
            Collections.addAll(args, "--subtitle-style", styles.getPath());
        }
        File forced = new File(session, "game-assets/forced_subtitles");
        if (forced.isDirectory()) {
            Collections.addAll(args, "--forced-subtitles", forced.getPath());
        }
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
            if (retailPackNames().stream().allMatch(name -> existingFile(directory, name))) return true;
        }
        return false;
    }

    private static boolean existingFile(File directory, String name) {
        File candidate = new File(directory, name);
        return candidate.isFile() && candidate.length() > 0;
    }

    SchoolDaysFolder(Context context, DocumentTree source, Uri tree) throws IOException {
        this.treeUri = tree;
        Map<String, DocumentTree.Entry> root = source.children(tree, null);
        Map<String, DocumentTree.Entry> packs = directory(source, tree, root, "Packs");
        Map<String, DocumentTree.Entry> assets = directory(source, tree, root, "game-assets");

        Map<String, DocumentTree.Entry> rootPacks = indexPacks(root);
        selectedPacksDirectly = packs.isEmpty()
                && (rootPacks.containsKey("script.gpk") || rootPacks.containsKey("system.gpk")
                || rootPacks.containsKey("lxh.gpk"));

        archives = indexPacks(packs);
        int foundRetail = 0;
        for (String name : retailPackNames()) {
            if (archives.containsKey(name.toLowerCase(Locale.ROOT))) foundRetail++;
        }
        hasLxhPack = archives.containsKey("lxh.gpk");
        retailPackCount = foundRetail;
        packCount = foundRetail + (hasLxhPack ? 1 : 0);

        route = assets.get("school-days-hq.ktnroute");
        if (!file(assets, "school-days-hq.ktnroute")) missing.add("game-assets/school-days-hq.ktnroute");
        if (packs.isEmpty()) {
            missing.add("Packs/");
        } else {
            missing.addAll(missingPacks(packs));
        }

        stylesAss = file(assets, "styles.ass") ? assets.get("styles.ass") : null;
        hasStylesAss = stylesAss != null;

        DocumentTree.Entry gameAssetsForced = directoryEntry(assets, "forced_subtitles");
        DocumentTree.Entry legacyRootForced = directoryEntry(root, "forced_subtitles");
        forcedSubtitlesDir = gameAssetsForced != null ? gameAssetsForced : legacyRootForced;
        hasForcedSubtitles = forcedSubtitlesDir != null;

        overlayDir = directoryEntry(root, "overlay");
        hasOverlay = overlayDir != null;

        Map<String, DocumentTree.Entry> translations = directory(source, tree, root, "translations");
        Map<String, DocumentTree.Entry> organizedJsons = directory(source, tree, translations, "jsons");
        Map<String, DocumentTree.Entry> mergedJsons = new HashMap<>(translations);
        mergedJsons.putAll(organizedJsons);
        jsons = mergedJsons;

        Map<String, DocumentTree.Entry> organizedScripts = directory(source, tree, translations, "scripts");
        Map<String, DocumentTree.Entry> mergedScripts = new HashMap<>(translations);
        mergedScripts.putAll(organizedScripts);
        scripts = mergedScripts;

        for (String locale : SUPPORTED_LOCALES) {
            boolean hasBaseJson = file(jsons, locale + ".json");
            boolean hasLxhJson = file(jsons, locale + "-lxh.json");
            if (hasBaseJson) {
                baseJsonLanguages.add(locale);
            }
            if (hasBaseJson && hasLxhJson) {
                languages.add(locale);
            } else if (hasBaseJson || hasLxhJson) {
                incompleteJsonLanguages.add(locale);
            }

            String scriptName = gpkScriptFilename(locale);
            boolean hasBaseGpk = (scriptName == null)
                    ? archives.containsKey("script.gpk")
                    : file(scripts, scriptName);
            boolean hasLxhGpk = file(scripts, gpkLxhFilename(locale));
            if (hasBaseGpk) {
                baseGpkLanguages.add(locale);
            }
            if (hasBaseGpk && hasLxhGpk) {
                gpkLanguages.add(locale);
            }
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
                    decoder.setTargetSampleSize((int) Math.max(1L, (longest + (long) limit - 1) / limit));
                });
            }
        } catch (IOException | RuntimeException ignored) {
            // Optional artwork. Keep the neutral header and the folder report.
        }
    }
}
