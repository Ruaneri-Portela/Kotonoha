package me.hirameki.kotonoha.android;

import android.content.Context;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.zip.DataFormatException;
import java.util.zip.Deflater;
import java.util.zip.Inflater;

/**
 * Reads and writes the exact shared configuration files consumed by the C++
 * School Days HQ runtime on Android:
 *   - settings.json  (EngineSettings::defaultPath("school-days"))
 *   - save/Config.DAT (SchoolDaysConfigStorage, DFLT + zlib)
 *   - save/Kotonoha.ini (ContentAspectPreference, AndroidFrameLimit)
 */
final class SchoolDaysConfigStore {
    // Matches Kotonoha::UpscaleBackend enum values in Upscaler.hpp.
    static final int UPSCALE_NONE = 0;
    static final int UPSCALE_LINEAR = 1;
    static final int UPSCALE_MPV = 2;
    static final int UPSCALE_AUTO = 4;

    static final class PresetOption {
        final int id;
        final String tier;
        final String label;

        PresetOption(int id, String tier, String label) {
            this.id = id;
            this.tier = tier;
            this.label = label;
        }
    }

    // Matches Kotonoha::UpscalePreset & SchoolDaysShaderPresetCatalog.hpp.
    static final PresetOption[] PRESET_OPTIONS = {
            new PresetOption(9, "LOW", "LOW · Anime4K Mode A (Fast)"),
            new PresetOption(10, "LOW", "LOW · Anime4K Mode B (Fast)"),
            new PresetOption(11, "LOW", "LOW · Anime4K Mode C (Fast)"),
            new PresetOption(12, "MEDIUM", "MEDIUM · Anime4K Mode A+A (Fast)"),
            new PresetOption(13, "MEDIUM", "MEDIUM · Anime4K Mode B+B (Fast)"),
            new PresetOption(14, "MEDIUM", "MEDIUM · Anime4K Mode C+A (Fast)"),
            new PresetOption(0, "MEDIUM", "MEDIUM · Anime4K Mode A (HQ)"),
            new PresetOption(4, "MEDIUM", "MEDIUM · Anime4K Mode B (HQ)"),
            new PresetOption(5, "MEDIUM", "MEDIUM · Anime4K Mode C (HQ)"),
            new PresetOption(1, "MEDIUM", "MEDIUM · ArtCNN"),
            new PresetOption(2, "MEDIUM", "MEDIUM · ArtCNN Denoise"),
            new PresetOption(6, "HIGH", "HIGH · Anime4K Mode A+A (HQ)"),
            new PresetOption(7, "HIGH", "HIGH · Anime4K Mode B+B (HQ)"),
            new PresetOption(8, "HIGH", "HIGH · Anime4K Mode C+A (HQ)"),
            new PresetOption(3, "HIGH", "HIGH · ArtCNN Large"),
    };

    static final class Snapshot {
        // Display (settings.json + Kotonoha.ini)
        int fullscreenWidth = 0;
        int fullscreenHeight = 0;
        float refreshRate = 0.0f;
        boolean vsync = true;
        int contentAspectPreference = 0; // 0=Auto, 1=4:3, 2=16:9
        int androidFrameLimit = 0;       // 0=Unlimited, 30, 60

        // Graphics (settings.json)
        float renderScale = 0.75f;
        int upscaleBackend = UPSCALE_LINEAR;
        float upscaleFactor = 1.0f;
        int upscalePreset = 0;

        // Audio (Config.DAT + settings.json)
        int bgmVolume = 5;    // 0..10
        int seVolume = 5;     // 0..10
        int voiceVolume = 5;  // 0..10
        boolean menVoice = true;
        boolean mute = false;
        float masterVolume = 1.0f;
        float movieVolume = 1.0f;

        // General (Config.DAT + settings.json)
        boolean textView = true;
        boolean skip = false;
        boolean superSkip = false;
        boolean autoDraw = true;
        boolean autoMode = false;
        boolean swapControllerAB = false;
        boolean enableForcedSubtitles = true;
        // Tri-state EnableLxh preference stored in save/Kotonoha.ini:
        // null = never explicitly chosen (default follows LxH.GPK availability).
        Boolean enableLxh = null;
        boolean discordPresence = false;

        // Launcher-only (SharedPreferences, never written to Config.DAT/settings.json)
        boolean launcherMusic = true;

        Snapshot copy() {
            Snapshot c = new Snapshot();
            c.fullscreenWidth = fullscreenWidth;
            c.fullscreenHeight = fullscreenHeight;
            c.refreshRate = refreshRate;
            c.vsync = vsync;
            c.contentAspectPreference = contentAspectPreference;
            c.androidFrameLimit = androidFrameLimit;
            c.renderScale = renderScale;
            c.upscaleBackend = upscaleBackend;
            c.upscaleFactor = upscaleFactor;
            c.upscalePreset = upscalePreset;
            c.bgmVolume = bgmVolume;
            c.seVolume = seVolume;
            c.voiceVolume = voiceVolume;
            c.menVoice = menVoice;
            c.mute = mute;
            c.masterVolume = masterVolume;
            c.movieVolume = movieVolume;
            c.textView = textView;
            c.skip = skip;
            c.superSkip = superSkip;
            c.autoDraw = autoDraw;
            c.autoMode = autoMode;
            c.swapControllerAB = swapControllerAB;
            c.enableForcedSubtitles = enableForcedSubtitles;
            c.enableLxh = enableLxh;
            c.discordPresence = discordPresence;
            c.launcherMusic = launcherMusic;
            return c;
        }

        boolean retailEquals(Snapshot other) {
            return bgmVolume == other.bgmVolume
                    && seVolume == other.seVolume
                    && voiceVolume == other.voiceVolume
                    && menVoice == other.menVoice
                    && mute == other.mute
                    && textView == other.textView
                    && skip == other.skip
                    && superSkip == other.superSkip
                    && autoDraw == other.autoDraw
                    && autoMode == other.autoMode;
        }

        static Snapshot defaults() {
            return new Snapshot();
        }
    }

    private final File documentsRoot;
    private final File saveRoot;

    SchoolDaysConfigStore(Context context) {
        this(resolveDocumentsRoot(context));
    }

    SchoolDaysConfigStore(File documentsRoot) {
        this.documentsRoot = documentsRoot;
        this.saveRoot = new File(documentsRoot, "save");
    }

    private static File resolveDocumentsRoot(Context context) {
        File ext = context.getExternalFilesDir(null);
        return ext != null ? ext : context.getFilesDir();
    }

    File settingsFile() {
        return new File(documentsRoot, "settings.json");
    }

    private File presenceFile() {
        return new File(documentsRoot, "discord-presence.txt");
    }

    File configDatFile() {
        return new File(saveRoot, "Config.DAT");
    }

    File kotonohaIniFile() {
        return new File(saveRoot, "Kotonoha.ini");
    }

    static double retailLinearGain(int level) {
        int clamped = Math.max(0, Math.min(10, level));
        double nativeMasterValue = (11 - clamped) * (-1.0);
        if (nativeMasterValue <= -11.0) return 0.0;
        if (nativeMasterValue >= -1.0) return 1.0;
        int hundredthDb = (int) Math.max(-10000L, Math.min(0L, Math.round(nativeMasterValue * 175.0)));
        if (hundredthDb <= -10000) return 0.0;
        if (hundredthDb >= 0) return 1.0;
        return Math.pow(10.0, hundredthDb / 2000.0);
    }

    Snapshot load(boolean launcherMusicDefault) {
        Snapshot s = Snapshot.defaults();
        s.launcherMusic = launcherMusicDefault;
        loadSettingsJson(s);
        loadConfigDat(s);
        loadKotonohaIni(s);
        if (BuildConfig.HAS_DISCORD_RPC) {
            File presence = presenceFile();
            try {
                s.discordPresence = presence.isFile() && presence.length() == 3
                        && Arrays.equals(Files.readAllBytes(presence.toPath()),
                        "on\n".getBytes(StandardCharsets.UTF_8));
            } catch (IOException ignored) {
                s.discordPresence = false;
            }
        }
        return s;
    }

    void save(Snapshot next, Snapshot previous, boolean clearTranslationPath) throws IOException {
        if (!documentsRoot.isDirectory() && !documentsRoot.mkdirs()) {
            throw new IOException("Cannot create documents directory");
        }
        if (!saveRoot.isDirectory() && !saveRoot.mkdirs()) {
            throw new IOException("Cannot create save directory");
        }
        saveSettingsJson(next, clearTranslationPath);
        if (configDatFile().isFile() || !next.retailEquals(previous)) {
            saveConfigDat(next);
        }
        saveKotonohaIni(next);
        if (BuildConfig.HAS_DISCORD_RPC) {
            writeAtomic(presenceFile(), (next.discordPresence ? "on\n" : "off\n")
                    .getBytes(StandardCharsets.UTF_8));
        }
    }

    void clearStoredTranslationPath() {
        File file = settingsFile();
        if (!file.isFile()) return;
        try {
            String raw = new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
            JSONObject obj = new JSONObject(raw);
            if (obj.optString("translationPath", "").isEmpty()) return;
            obj.put("translationPath", "");
            writeAtomic(file, obj.toString(2).getBytes(StandardCharsets.UTF_8));
        } catch (IOException | JSONException ignored) {
        }
    }

    private void loadSettingsJson(Snapshot s) {
        File file = settingsFile();
        if (!file.isFile()) return;
        try {
            String raw = new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
            JSONObject obj = new JSONObject(raw);
            s.fullscreenWidth = obj.optInt("fullscreenWidth", s.fullscreenWidth);
            s.fullscreenHeight = obj.optInt("fullscreenHeight", s.fullscreenHeight);
            s.refreshRate = (float) obj.optDouble("refreshRate", s.refreshRate);
            s.vsync = obj.optBoolean("vsync", s.vsync);
            s.renderScale = clampFloat((float) obj.optDouble("renderScale", s.renderScale), 0.25f, 1.0f);
            s.upscaleBackend = obj.optInt("upscaler", s.upscaleBackend);
            s.upscalePreset = obj.optInt("preset", s.upscalePreset);
            s.upscaleFactor = clampFloat((float) obj.optDouble("factor", s.upscaleFactor), 1.0f, 4.0f);
            s.masterVolume = clampFloat((float) obj.optDouble("masterVolume", s.masterVolume), 0.0f, 1.0f);
            s.mute = obj.optBoolean("masterMuted", s.mute);
            s.swapControllerAB = obj.optBoolean("swapControllerAB", s.swapControllerAB);
            s.enableForcedSubtitles = obj.optBoolean("enableForcedSubtitles", s.enableForcedSubtitles);
            JSONObject channels = obj.optJSONObject("channelVolumes");
            if (channels != null && channels.has("Movie")) {
                s.movieVolume = clampFloat((float) channels.optDouble("Movie", s.movieVolume), 0.0f, 1.0f);
            }
        } catch (IOException | JSONException ignored) {
        }
    }

    private void saveSettingsJson(Snapshot s, boolean clearTranslationPath) throws IOException {
        File file = settingsFile();
        JSONObject obj = null;
        if (file.isFile()) {
            try {
                String raw = new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
                obj = new JSONObject(raw);
            } catch (IOException | JSONException ignored) {
            }
        }
        try {
            if (obj == null) {
                obj = new JSONObject();
                obj.put("version", 1);
                obj.put("width", 1280);
                obj.put("height", 720);
                obj.put("display", 0);
                obj.put("resizable", true);
                obj.put("debugUIScale", 1.0);
                obj.put("logPriority", 3);
                obj.put("shaderFiles", "");
                obj.put("assetOverlays", new JSONObject());
                obj.put("assetOverlayName", "");
                obj.put("translationPath", "");
                obj.put("fontPath", "");
                obj.put("vnFontPath", "");
            }
            obj.put("version", 1);
            obj.put("mode", 1); // WindowMode::Borderless (Android fullscreen Surface)
            obj.put("display", 0);
            obj.put("fullscreenWidth", s.fullscreenWidth);
            obj.put("fullscreenHeight", s.fullscreenHeight);
            obj.put("refreshRate", (double) s.refreshRate);
            obj.put("vsync", s.vsync);
            obj.put("renderScale", (double) clampFloat(s.renderScale, 0.25f, 1.0f));
            obj.put("renderer", "opengles2");
            obj.put("upscaler", s.upscaleBackend);
            obj.put("preset", s.upscalePreset);
            obj.put("factor", (double) clampFloat(s.upscaleFactor, 1.0f, 4.0f));
            obj.put("masterVolume", (double) clampFloat(s.masterVolume, 0.0f, 1.0f));
            obj.put("masterMuted", s.mute);
            obj.put("swapControllerAB", s.swapControllerAB);
            obj.put("enableForcedSubtitles", s.enableForcedSubtitles);
            if (clearTranslationPath) {
                obj.put("translationPath", "");
            }
            JSONObject channels = obj.optJSONObject("channelVolumes");
            if (channels == null) channels = new JSONObject();
            channels.put("BGM", retailLinearGain(s.bgmVolume));
            channels.put("SE", retailLinearGain(s.seVolume));
            channels.put("Voice", retailLinearGain(s.voiceVolume));
            channels.put("Movie", (double) clampFloat(s.movieVolume, 0.0f, 1.0f));
            obj.put("channelVolumes", channels);
            writeAtomic(file, obj.toString(2).getBytes(StandardCharsets.UTF_8));
        } catch (JSONException e) {
            throw new IOException("Failed to serialize settings.json", e);
        }
    }

    private void loadConfigDat(Snapshot s) {
        File file = configDatFile();
        if (!file.isFile()) return;
        try {
            byte[] bytes = Files.readAllBytes(file.toPath());
            String text = decodeConfigDat(bytes);
            if (text == null) return;
            for (String line : text.split("\n", -1)) {
                String[] kv = extractConfigLine(line);
                if (kv == null) continue;
                String key = kv[0];
                int val;
                try {
                    val = Integer.parseInt(kv[1]);
                } catch (NumberFormatException ignored) {
                    continue;
                }
                switch (key) {
                    case "VoiceVolume":
                        if (val >= 0 && val <= 10) s.voiceVolume = val;
                        break;
                    case "BgmVolume":
                        if (val >= 0 && val <= 10) s.bgmVolume = val;
                        break;
                    case "SeVolume":
                        if (val >= 0 && val <= 10) s.seVolume = val;
                        break;
                    case "TextView":
                        s.textView = val != 0;
                        break;
                    case "MenVoice":
                        s.menVoice = val != 0;
                        break;
                    case "Mute":
                        s.mute = val != 0;
                        break;
                    case "Skip":
                        s.skip = val != 0;
                        break;
                    case "AutoDraw":
                        s.autoDraw = val != 0;
                        break;
                    case "AutoMode":
                        s.autoMode = val != 0;
                        break;
                    case "SuperSkip":
                        s.superSkip = val != 0;
                        break;
                    default:
                        break;
                }
            }
        } catch (IOException ignored) {
        }
    }

    private void saveConfigDat(Snapshot s) throws IOException {
        File file = configDatFile();
        List<String> lines = new ArrayList<>();
        if (file.isFile()) {
            try {
                String existing = decodeConfigDat(Files.readAllBytes(file.toPath()));
                if (existing != null && !existing.isEmpty()) {
                    String trimmed = existing.endsWith("\n")
                            ? existing.substring(0, existing.length() - 1) : existing;
                    if (!trimmed.isEmpty()) {
                        lines.addAll(Arrays.asList(trimmed.split("\n", -1)));
                    }
                }
            } catch (IOException ignored) {
            }
        }
        setConfigKey(lines, "VoiceVolume", String.valueOf(clampInt(s.voiceVolume, 0, 10)));
        setConfigKey(lines, "BgmVolume", String.valueOf(clampInt(s.bgmVolume, 0, 10)));
        setConfigKey(lines, "SeVolume", String.valueOf(clampInt(s.seVolume, 0, 10)));
        setConfigKey(lines, "TextView", s.textView ? "1" : "0");
        setConfigKey(lines, "MenVoice", s.menVoice ? "1" : "0");
        setConfigKey(lines, "Mute", s.mute ? "1" : "0");
        setConfigKey(lines, "Skip", s.skip ? "1" : "0");
        setConfigKey(lines, "AutoDraw", s.autoDraw ? "1" : "0");
        setConfigKey(lines, "AutoMode", s.autoMode ? "1" : "0");
        setConfigKey(lines, "SuperSkip", s.superSkip ? "1" : "0");

        StringBuilder sb = new StringBuilder();
        for (String line : lines) {
            sb.append(line).append('\n');
        }
        byte[] encoded = encodeConfigDat(sb.toString());
        writeAtomic(file, encoded);
    }

    void loadKotonohaIni(Snapshot s) {
        File file = kotonohaIniFile();
        if (!file.isFile()) return;
        try {
            List<String> lines = Files.readAllLines(file.toPath(), StandardCharsets.UTF_8);
            for (String raw : lines) {
                String line = raw.trim();
                if (line.startsWith("ContentAspectPreference=")) {
                    try {
                        int v = Integer.parseInt(line.substring("ContentAspectPreference=".length()).trim());
                        if (v >= 0 && v <= 2) s.contentAspectPreference = v;
                    } catch (NumberFormatException ignored) {
                    }
                } else if (line.startsWith("AndroidFrameLimit=")) {
                    try {
                        int v = Integer.parseInt(line.substring("AndroidFrameLimit=".length()).trim());
                        s.androidFrameLimit = (v == 30 || v == 60) ? v : 0;
                    } catch (NumberFormatException ignored) {
                    }
                } else if (line.startsWith("EnableLxh=")) {
                    String v = line.substring("EnableLxh=".length()).trim();
                    if ("1".equals(v) || "true".equalsIgnoreCase(v)) {
                        s.enableLxh = Boolean.TRUE;
                    } else if ("0".equals(v) || "false".equalsIgnoreCase(v)) {
                        s.enableLxh = Boolean.FALSE;
                    }
                }
            }
        } catch (IOException ignored) {
        }
    }

    void saveKotonohaIni(Snapshot s) throws IOException {
        if (!saveRoot.isDirectory() && !saveRoot.mkdirs()) {
            throw new IOException("Cannot create save directory");
        }
        File file = kotonohaIniFile();
        List<String> lines = new ArrayList<>();
        if (file.isFile()) {
            try {
                lines.addAll(Files.readAllLines(file.toPath(), StandardCharsets.UTF_8));
            } catch (IOException ignored) {
            }
        }
        setIniKey(lines, "ContentAspectPreference", String.valueOf(clampInt(s.contentAspectPreference, 0, 2)));
        setIniKey(lines, "AndroidFrameLimit",
                String.valueOf(s.androidFrameLimit == 30 || s.androidFrameLimit == 60 ? s.androidFrameLimit : 0));
        if (s.enableLxh != null) {
            setIniKey(lines, "EnableLxh", s.enableLxh ? "1" : "0");
        } else {
            removeIniKey(lines, "EnableLxh");
        }
        StringBuilder sb = new StringBuilder();
        for (String line : lines) {
            sb.append(line).append('\n');
        }
        writeAtomic(file, sb.toString().getBytes(StandardCharsets.UTF_8));
    }

    private static void setIniKey(List<String> lines, String key, String value) {
        String prefix = key + "=";
        for (int i = 0; i < lines.size(); i++) {
            if (lines.get(i).startsWith(prefix)) {
                lines.set(i, prefix + value);
                return;
            }
        }
        lines.add(prefix + value);
    }

    private static void removeIniKey(List<String> lines, String key) {
        String prefix = key + "=";
        lines.removeIf(line -> line != null && line.trim().startsWith(prefix));
    }

    private static String[] extractConfigLine(String line) {
        if (line == null || line.length() < 6 || line.charAt(0) != '[') return null;
        int close = line.indexOf(']');
        if (close <= 1 || close + 3 >= line.length()) return null;
        if (line.charAt(close + 1) != '=' || line.charAt(close + 2) != '"'
                || line.charAt(line.length() - 1) != '"') return null;
        if (line.indexOf('"', close + 3) != line.length() - 1) return null;
        String key = line.substring(1, close);
        String val = line.substring(close + 3, line.length() - 1);
        return key.isEmpty() ? null : new String[]{key, val};
    }

    private static void setConfigKey(List<String> lines, String key, String value) {
        String formatted = "[" + key + "]=\"" + value + "\"";
        for (int i = 0; i < lines.size(); i++) {
            String[] kv = extractConfigLine(lines.get(i));
            if (kv != null && key.equals(kv[0])) {
                lines.set(i, formatted);
                return;
            }
        }
        lines.add(formatted);
    }

    private static String decodeConfigDat(byte[] bytes) {
        if (bytes == null || bytes.length == 0) return null;
        if (bytes.length >= 4 && bytes[0] == 'D' && bytes[1] == 'F'
                && bytes[2] == 'L' && bytes[3] == 'T') {
            if (bytes.length == 4) return null;
            Inflater inflater = new Inflater();
            try {
                inflater.setInput(bytes, 4, bytes.length - 4);
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                byte[] buf = new byte[4096];
                while (!inflater.finished()) {
                    int n = inflater.inflate(buf);
                    if (n == 0) {
                        if (inflater.needsInput() || inflater.needsDictionary()) break;
                    } else {
                        out.write(buf, 0, n);
                        if (out.size() > 1024 * 1024) return null;
                    }
                }
                return new String(out.toByteArray(), StandardCharsets.UTF_8);
            } catch (DataFormatException e) {
                return null;
            } finally {
                inflater.end();
            }
        }
        return new String(bytes, StandardCharsets.UTF_8);
    }

    private static byte[] encodeConfigDat(String logical) throws IOException {
        byte[] raw = logical.getBytes(StandardCharsets.UTF_8);
        Deflater deflater = new Deflater();
        try {
            deflater.setInput(raw);
            deflater.finish();
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            out.write('D');
            out.write('F');
            out.write('L');
            out.write('T');
            byte[] buf = new byte[4096];
            while (!deflater.finished()) {
                int n = deflater.deflate(buf);
                if (n > 0) out.write(buf, 0, n);
            }
            return out.toByteArray();
        } finally {
            deflater.end();
        }
    }

    private static void writeAtomic(File target, byte[] data) throws IOException {
        File parent = target.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("Cannot create parent directory: " + parent);
        }
        File tmp = new File(target.getPath() + ".tmp");
        Files.write(tmp.toPath(), data);
        Files.move(tmp.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING);
    }

    private static float clampFloat(float v, float lo, float hi) {
        if (Float.isNaN(v) || Float.isInfinite(v)) return lo;
        return Math.max(lo, Math.min(hi, v));
    }

    private static int clampInt(int v, int lo, int hi) {
        return Math.max(lo, Math.min(hi, v));
    }
}
