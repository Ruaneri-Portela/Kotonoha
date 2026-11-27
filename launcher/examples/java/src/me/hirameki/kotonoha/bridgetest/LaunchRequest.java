package me.hirameki.kotonoha.bridgetest;

import java.util.LinkedHashMap;
import java.util.Map;
import me.hirameki.kotonoha.bridgetest.JsonUtil.GameInfo;
import me.hirameki.kotonoha.launcher.KotonohaLauncherBridge;

/**
 * One KotonohaGameLaunchRequest, assembled from a scanned game and passed to
 * the C ABI in a single native call. Keeping this in one place guarantees the
 * GUI harness and the headless harness build identical requests.
 */
final class LaunchRequest {
    final String gamePath;
    final String engineSlug;
    final String saveProfileId;
    final boolean splitSavesByGame;
    final String customSaveRoot;
    final String assetOverlayPath;
    final String translationPath;
    final String subtitleStylePath;
    final String customDocumentsDir;
    final String ktrfPath;
    final String scriptsDir;
    final String startScenario;
    final String rendererBackend;
    final String upscalerBackend;
    final String upscalerPreset;
    final String upscalerFactor;
    final boolean dryRun;

    private LaunchRequest(Builder builder) {
        gamePath = value(builder.gamePath);
        engineSlug = value(builder.engineSlug);
        saveProfileId = value(builder.saveProfileId);
        splitSavesByGame = builder.splitSavesByGame;
        customSaveRoot = value(builder.customSaveRoot);
        assetOverlayPath = value(builder.assetOverlayPath);
        translationPath = value(builder.translationPath);
        subtitleStylePath = value(builder.subtitleStylePath);
        customDocumentsDir = value(builder.customDocumentsDir);
        ktrfPath = value(builder.ktrfPath);
        scriptsDir = value(builder.scriptsDir);
        startScenario = value(builder.startScenario);
        rendererBackend = value(builder.rendererBackend);
        upscalerBackend = value(builder.upscalerBackend);
        upscalerPreset = value(builder.upscalerPreset);
        upscalerFactor = value(builder.upscalerFactor);
        dryRun = builder.dryRun;
    }

    private static String value(String text) {
        return text == null ? "" : text;
    }

    static Builder from(GameInfo game) {
        Builder builder = new Builder();
        builder.gamePath = game.path;
        builder.engineSlug = game.engineSlug;
        builder.saveProfileId = game.saveProfileId;
        builder.assetOverlayPath = game.overlayPath;
        builder.translationPath = game.params.get("translationPath");
        builder.subtitleStylePath = game.params.get("subtitleStylePath");
        builder.customDocumentsDir = game.params.get("customDocumentsDir");
        builder.ktrfPath = game.params.get("ktrfPath");
        builder.scriptsDir = game.params.get("scriptsDir");
        builder.startScenario = game.params.get("startScenario");
        builder.rendererBackend = game.params.get("rendererBackend");
        builder.upscalerBackend = game.params.get("upscalerBackend");
        builder.upscalerPreset = game.params.get("upscalerPreset");
        builder.upscalerFactor = game.params.get("upscalerFactor");
        String parsedOverlay = game.params.get("assetOverlayPath");
        if (parsedOverlay != null && !parsedOverlay.isEmpty()) {
            builder.assetOverlayPath = parsedOverlay;
        }
        return builder;
    }

    /** Human-readable, JSON-shaped confirmation of the assembled request. */
    String describe() {
        Map<String, Object> fields = new LinkedHashMap<>();
        fields.put("gamePath", gamePath);
        fields.put("engineSlug", engineSlug);
        fields.put("saveProfileId", saveProfileId);
        fields.put("splitSavesByGame", splitSavesByGame);
        fields.put("customSaveRoot", customSaveRoot);
        fields.put("assetOverlayPath", assetOverlayPath);
        fields.put("translationPath", translationPath);
        fields.put("subtitleStylePath", subtitleStylePath);
        fields.put("customDocumentsDir", customDocumentsDir);
        fields.put("ktrfPath", ktrfPath);
        fields.put("scriptsDir", scriptsDir);
        fields.put("startScenario", startScenario);
        fields.put("rendererBackend", rendererBackend);
        fields.put("upscalerBackend", upscalerBackend);
        fields.put("upscalerPreset", upscalerPreset);
        fields.put("upscalerFactor", upscalerFactor);
        fields.put("dryRun", dryRun);
        StringBuilder out = new StringBuilder("{\n");
        boolean first = true;
        for (Map.Entry<String, Object> entry : fields.entrySet()) {
            if (!first) out.append(",\n");
            first = false;
            out.append("  \"").append(entry.getKey()).append("\": ");
            Object v = entry.getValue();
            if (v instanceof Boolean) {
                out.append(v);
            } else {
                out.append('"').append(escape(String.valueOf(v))).append('"');
            }
        }
        return out.append("\n}").toString();
    }

    private static String escape(String text) {
        return text.replace("\\", "\\\\").replace("\"", "\\\"")
                   .replace("\n", "\\n").replace("\r", "\\r");
    }

    /** One in-process launch. The core configures SDL dummy drivers itself. */
    String launch() {
        return KotonohaLauncherBridge.nativeLaunchJson(
                gamePath, engineSlug, saveProfileId, splitSavesByGame,
                customSaveRoot, assetOverlayPath, translationPath,
                subtitleStylePath, customDocumentsDir, ktrfPath, scriptsDir,
                startScenario, rendererBackend, upscalerBackend,
                upscalerPreset, upscalerFactor, dryRun);
    }

    String launchDetached() {
        return KotonohaLauncherBridge.nativeLaunchDetachedJson(
                gamePath, engineSlug, saveProfileId, splitSavesByGame,
                customSaveRoot, assetOverlayPath, translationPath,
                subtitleStylePath, customDocumentsDir, ktrfPath, scriptsDir,
                startScenario, rendererBackend, upscalerBackend,
                upscalerPreset, upscalerFactor, dryRun);
    }

    static final class Builder {
        String gamePath = "";
        String engineSlug = "";
        String saveProfileId = "";
        boolean splitSavesByGame = true;
        String customSaveRoot = "";
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
        boolean dryRun = false;

        Builder split(boolean split) {
            splitSavesByGame = split;
            return this;
        }

        Builder saveRoot(String text) {
            customSaveRoot = value(text);
            return this;
        }

        Builder profile(String text) {
            if (text != null && !text.trim().isEmpty()) saveProfileId = text.trim();
            return this;
        }

        Builder overlay(String text) {
            if (text != null && !text.isEmpty()) assetOverlayPath = text;
            return this;
        }

        Builder translation(String text) {
            if (text != null && !text.isEmpty()) translationPath = text;
            return this;
        }

        Builder subtitle(String text) {
            if (text != null && !text.isEmpty()) subtitleStylePath = text;
            return this;
        }

        Builder documents(String text) {
            if (text != null && !text.isEmpty()) customDocumentsDir = text;
            return this;
        }

        Builder ktrf(String text) {
            if (text != null && !text.isEmpty()) ktrfPath = text;
            return this;
        }

        Builder scripts(String text) {
            if (text != null && !text.isEmpty()) scriptsDir = text;
            return this;
        }

        Builder start(String text) {
            if (text != null && !text.isEmpty()) startScenario = text;
            return this;
        }

        Builder renderer(String text) {
            if (text != null && !text.isEmpty()) rendererBackend = text;
            return this;
        }

        Builder upscaler(String text) {
            if (text != null && !text.isEmpty()) upscalerBackend = text;
            return this;
        }

        Builder preset(String text) {
            if (text != null && !text.isEmpty()) upscalerPreset = text;
            return this;
        }

        Builder factor(String text) {
            if (text != null && !text.isEmpty()) upscalerFactor = text;
            return this;
        }

        /** "Test launcher only": validate the request without opening SDL. */
        Builder dryRun(boolean value) {
            dryRun = value;
            return this;
        }

        LaunchRequest build() {
            return new LaunchRequest(this);
        }
    }
}
