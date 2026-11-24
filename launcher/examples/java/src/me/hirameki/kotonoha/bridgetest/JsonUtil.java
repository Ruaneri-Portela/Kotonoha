package me.hirameki.kotonoha.bridgetest;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Tiny dependency-free JSON helpers: pretty printing and game-row scraping. */
final class JsonUtil {
    private JsonUtil() {}

    static String pretty(String json) {
        if (json == null) return "(null)";
        StringBuilder out = new StringBuilder();
        int indent = 0;
        boolean inString = false;
        boolean escaped = false;
        for (int i = 0; i < json.length(); ++i) {
            char c = json.charAt(i);
            if (inString) {
                out.append(c);
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == '"') {
                    inString = false;
                }
                continue;
            }
            switch (c) {
                case '"':
                    inString = true;
                    out.append(c);
                    break;
                case '{':
                case '[':
                    out.append(c).append('\n');
                    ++indent;
                    appendIndent(out, indent);
                    break;
                case '}':
                case ']':
                    --indent;
                    out.append('\n');
                    appendIndent(out, Math.max(indent, 0));
                    out.append(c);
                    break;
                case ',':
                    out.append(',').append('\n');
                    appendIndent(out, indent);
                    break;
                case ':':
                    out.append(": ");
                    break;
                default:
                    if (!Character.isWhitespace(c)) out.append(c);
                    break;
            }
        }
        return out.toString();
    }

    private static void appendIndent(StringBuilder out, int indent) {
        for (int i = 0; i < indent; ++i) out.append("  ");
    }

    private static final Pattern FIELD =
            Pattern.compile("\"([A-Za-z_][A-Za-z0-9_]*)\"\\s*:\\s*\"([^\"]*)\"");

    /** Reads a top-level JSON boolean (e.g. "playable":true) without a parser. */
    static boolean boolField(String json, String name) {
        if (json == null) return false;
        Matcher m = Pattern.compile("\"" + Pattern.quote(name)
                + "\"\\s*:\\s*true").matcher(json);
        return m.find();
    }

    /**
     * Full row from KotonohaLauncherScanRootsJson. The nested "params" object is
     * flattened by the same string-field regex, so every optional launch
     * parameter survives into the request built by LaunchRequest.
     */
    static final class GameInfo {
        String title = "";
        String saveProfileId = "";
        String path = "";
        String overlayPath = "";
        String engineSlug = "";
        final Map<String, String> params = new LinkedHashMap<>();
    }

    static List<GameInfo> gameInfos(String json) {
        List<GameInfo> rows = new ArrayList<>();
        if (json == null) return rows;
        for (String object : splitObjects(json)) {
            GameInfo game = new GameInfo();
            boolean engineSlugSeen = false;
            Matcher m = FIELD.matcher(object);
            while (m.find()) {
                String key = m.group(1);
                String value = m.group(2);
                if (key.equalsIgnoreCase("title") || key.equalsIgnoreCase("name")
                        || key.equalsIgnoreCase("gameTitle")) {
                    if (game.title.isEmpty()) game.title = value;
                } else if (key.equalsIgnoreCase("saveProfileId")) {
                    if (game.saveProfileId.isEmpty()) game.saveProfileId = value;
                } else if (key.equalsIgnoreCase("path")
                        || key.equalsIgnoreCase("gamePath")
                        || key.equalsIgnoreCase("root")) {
                    if (game.path.isEmpty()) game.path = value;
                } else if (key.equalsIgnoreCase("overlayPath")) {
                    if (game.overlayPath.isEmpty()) game.overlayPath = value;
                } else if (key.equalsIgnoreCase("engineSlug")) {
                    engineSlugSeen = true;
                    game.engineSlug = value;
                } else if (key.toLowerCase().contains("engine")
                        && !engineSlugSeen && game.engineSlug.isEmpty()) {
                    game.engineSlug = value;
                } else {
                    game.params.putIfAbsent(key, value);
                }
            }
            if (!game.title.isEmpty() || !game.engineSlug.isEmpty()
                    || !game.path.isEmpty()) {
                rows.add(game);
            }
        }
        return rows;
    }

    /** Legacy 3-column view kept for simple consumers. */
    static List<String[]> games(String json) {
        List<String[]> rows = new ArrayList<>();
        for (GameInfo game : gameInfos(json)) {
            rows.add(new String[] {
                game.title.isEmpty() ? "(sem título)" : game.title,
                game.engineSlug.isEmpty() ? "(desconhecido)" : game.engineSlug,
                game.path
            });
        }
        return rows;
    }

    private static List<String> splitObjects(String json) {
        List<String> objects = new ArrayList<>();
        int depth = 0;
        int start = -1;
        boolean inString = false;
        boolean escaped = false;
        for (int i = 0; i < json.length(); ++i) {
            char c = json.charAt(i);
            if (inString) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') {
                inString = true;
            } else if (c == '{') {
                if (depth == 0) start = i;
                ++depth;
            } else if (c == '}' && start >= 0) {
                --depth;
                if (depth == 0) {
                    objects.add(json.substring(start, i + 1));
                    start = -1;
                }
            }
        }
        return objects;
    }
}
