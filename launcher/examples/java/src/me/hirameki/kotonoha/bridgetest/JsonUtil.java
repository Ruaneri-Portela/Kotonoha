package me.hirameki.kotonoha.bridgetest;

import java.util.ArrayList;
import java.util.List;
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

    /** Naive object-level scrape: enough for title / engine columns in a table. */
    static List<String[]> games(String json) {
        List<String[]> rows = new ArrayList<>();
        if (json == null) return rows;
        for (String object : splitObjects(json)) {
            String title = "";
            String engine = "";
            String path = "";
            Matcher m = FIELD.matcher(object);
            while (m.find()) {
                String key = m.group(1);
                String value = m.group(2);
                if (key.equalsIgnoreCase("title")
                        || key.equalsIgnoreCase("name")
                        || key.equalsIgnoreCase("gameTitle")) {
                    if (title.isEmpty()) title = value;
                } else if (key.toLowerCase().contains("engine")) {
                    if (engine.isEmpty()) engine = value;
                } else if (key.equalsIgnoreCase("path")
                        || key.equalsIgnoreCase("gamePath")
                        || key.equalsIgnoreCase("root")) {
                    if (path.isEmpty()) path = value;
                }
            }
            if (!title.isEmpty() || !engine.isEmpty() || !path.isEmpty()) {
                rows.add(new String[] {
                    title.isEmpty() ? "(sem título)" : title,
                    engine.isEmpty() ? "(desconhecido)" : engine,
                    path
                });
            }
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
