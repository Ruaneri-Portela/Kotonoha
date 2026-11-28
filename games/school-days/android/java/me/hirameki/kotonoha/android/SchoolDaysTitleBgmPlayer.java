package me.hirameki.kotonoha.android;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.net.Uri;
import android.os.ParcelFileDescriptor;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Locale;
import java.util.zip.DataFormatException;
import java.util.zip.Inflater;

/**
 * Plays the retail School Days TitleBGM directly inside the portrait Android
 * launcher (read-only from SAF Ini.GPK + BGM.GPK), isolated from Config.DAT.
 */
final class SchoolDaysTitleBgmPlayer {
    private static final float LAUNCHER_MUSIC_GAIN = 0.60f;
    private static final byte[] PIDX_XOR_KEY = {
            (byte) 0x82, (byte) 0xEE, (byte) 0x1D, (byte) 0xB3,
            (byte) 0x57, (byte) 0xE9, (byte) 0x2C, (byte) 0xC2,
            (byte) 0x2F, (byte) 0x54, (byte) 0x7B, (byte) 0x10,
            (byte) 0x4C, (byte) 0x9A, (byte) 0x75, (byte) 0x49
    };

    private MediaPlayer player;
    private File cachedOgg;
    private Uri loadedBgmUri;

    synchronized boolean prepareFromSaf(Context context, Uri iniUri, Uri bgmUri) {
        if (iniUri == null || bgmUri == null) return false;
        if (cachedOgg != null && cachedOgg.isFile() && bgmUri.equals(loadedBgmUri)) {
            return true;
        }
        try {
            byte[] startIniBytes = extractEntry(context, iniUri, "STARTSCRIPT.INI");
            if (startIniBytes == null) return false;
            String startIni = new String(startIniBytes, StandardCharsets.UTF_8);
            String titleStem = extractIniValue(startIni, "TitleBGM");
            if (titleStem == null || titleStem.isEmpty()) return false;

            String normalized = titleStem.replace('\\', '/');
            if (normalized.toLowerCase(Locale.ROOT).startsWith("bgm/")) {
                normalized = normalized.substring(4);
            }
            byte[] oggBytes = extractEntry(context, bgmUri, normalized + "_loop.ogg");
            if (oggBytes == null) {
                oggBytes = extractEntry(context, bgmUri, normalized + ".ogg");
            }
            if (oggBytes == null || oggBytes.length < 64) return false;

            File out = new File(context.getCacheDir(), "sd-launcher-title-bgm.ogg");
            Files.write(out.toPath(), oggBytes);
            cachedOgg = out;
            loadedBgmUri = bgmUri;
            return true;
        } catch (IOException | RuntimeException ignored) {
            return false;
        }
    }

    synchronized void start() {
        if (cachedOgg == null || !cachedOgg.isFile()) return;
        if (player != null) {
            try {
                if (!player.isPlaying()) player.start();
                return;
            } catch (IllegalStateException ignored) {
                stop();
            }
        }
        try {
            MediaPlayer mp = new MediaPlayer();
            mp.setAudioAttributes(new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build());
            mp.setDataSource(cachedOgg.getAbsolutePath());
            mp.setLooping(true);
            mp.setVolume(LAUNCHER_MUSIC_GAIN, LAUNCHER_MUSIC_GAIN);
            mp.prepare();
            mp.start();
            player = mp;
        } catch (IOException | RuntimeException ignored) {
            stop();
        }
    }

    synchronized void pause() {
        if (player != null) {
            try {
                if (player.isPlaying()) player.pause();
            } catch (IllegalStateException ignored) {
            }
        }
    }

    synchronized void stop() {
        if (player != null) {
            try {
                if (player.isPlaying()) player.stop();
            } catch (IllegalStateException ignored) {
            }
            try {
                player.release();
            } catch (RuntimeException ignored) {
            }
            player = null;
        }
    }

    synchronized void release() {
        stop();
        loadedBgmUri = null;
        if (cachedOgg != null) {
            cachedOgg.delete();
            cachedOgg = null;
        }
    }

    private static String extractIniValue(String text, String key) {
        String prefix = "[" + key + "]=\"";
        int idx = text.indexOf(prefix);
        if (idx < 0) return null;
        int start = idx + prefix.length();
        int end = text.indexOf('"', start);
        if (end < 0) return null;
        return text.substring(start, end);
    }

    private static byte[] extractEntry(Context context, Uri gpkUri, String targetRelative) throws IOException {
        String wanted = targetRelative.replace('\\', '/').toLowerCase(Locale.ROOT);
        try (ParcelFileDescriptor pfd = context.getContentResolver().openFileDescriptor(gpkUri, "r")) {
            if (pfd == null) return null;
            try (FileInputStream fis = new FileInputStream(pfd.getFileDescriptor());
                 FileChannel ch = fis.getChannel()) {
                long fileSize = ch.size();
                if (fileSize < 32) return null;
                byte[] trailer = readRange(ch, fileSize - 32, 32);
                String sig1 = new String(trailer, 0, 12, StandardCharsets.US_ASCII);
                String sig2 = new String(trailer, 16, 16, StandardCharsets.US_ASCII);
                if (!"STKFile0PIDX".equals(sig1) || !"STKFile0PACKFILE".equals(sig2)) return null;
                long pidxLen = ByteBuffer.wrap(trailer, 12, 4).order(ByteOrder.LITTLE_ENDIAN).getInt() & 0xFFFFFFFFL;
                if (pidxLen <= 4 || pidxLen > fileSize - 32 || pidxLen > 32 * 1024 * 1024) return null;
                long pidxOffset = fileSize - 32 - pidxLen;
                byte[] encoded = readRange(ch, pidxOffset, (int) pidxLen);
                for (int i = 0; i < encoded.length; i++) {
                    encoded[i] ^= PIDX_XOR_KEY[i & 15];
                }
                int decodedSize = ByteBuffer.wrap(encoded, 0, 4).order(ByteOrder.LITTLE_ENDIAN).getInt();
                if (decodedSize <= 0 || decodedSize > 64 * 1024 * 1024) return null;
                byte[] pidx = inflateExact(encoded, 4, encoded.length - 4, decodedSize);
                if (pidx == null) return null;

                ByteBuffer buf = ByteBuffer.wrap(pidx).order(ByteOrder.LITTLE_ENDIAN);
                while (buf.remaining() >= 2) {
                    int nameChars = buf.getShort() & 0xFFFF;
                    if (nameChars == 0) break;
                    if (buf.remaining() < nameChars * 2 + 23) return null;
                    char[] chars = new char[nameChars];
                    for (int i = 0; i < nameChars; i++) chars[i] = buf.getChar();
                    String entryName = new String(chars).replace('\\', '/').toLowerCase(Locale.ROOT);
                    buf.getShort(); // subVersion
                    buf.getShort(); // version
                    buf.getShort(); // reserved
                    long offset = buf.getInt() & 0xFFFFFFFFL;
                    int storedLen = buf.getInt();
                    byte[] magic = new byte[4];
                    buf.get(magic);
                    int uncompressedLen = buf.getInt();
                    int prefixLen = buf.get() & 0xFF;
                    if (buf.remaining() < prefixLen) return null;
                    byte[] prefix = new byte[prefixLen];
                    buf.get(prefix);

                    if (entryName.equals(wanted)) {
                        int bodyLen = storedLen - prefixLen;
                        if (bodyLen < 0 || offset + bodyLen > pidxOffset || storedLen > 32 * 1024 * 1024) {
                            return null;
                        }
                        byte[] stored = new byte[storedLen];
                        System.arraycopy(prefix, 0, stored, 0, prefixLen);
                        if (bodyLen > 0) {
                            byte[] body = readRange(ch, offset, bodyLen);
                            System.arraycopy(body, 0, stored, prefixLen, bodyLen);
                        }
                        String magicStr = new String(magic, StandardCharsets.US_ASCII);
                        if ("    ".equals(magicStr)) return stored;
                        if ("DFLT".equals(magicStr) && uncompressedLen >= 0 && uncompressedLen <= 32 * 1024 * 1024) {
                            return inflateExact(stored, 0, stored.length, uncompressedLen);
                        }
                        return null;
                    }
                }
            }
        }
        return null;
    }

    private static byte[] readRange(FileChannel ch, long offset, int length) throws IOException {
        ByteBuffer dst = ByteBuffer.allocate(length);
        long pos = offset;
        while (dst.hasRemaining()) {
            int n = ch.read(dst, pos);
            if (n < 0) throw new IOException("Unexpected EOF reading GPK");
            pos += n;
        }
        return dst.array();
    }

    private static byte[] inflateExact(byte[] input, int off, int len, int expectedSize) {
        Inflater inflater = new Inflater();
        try {
            inflater.setInput(input, off, len);
            ByteArrayOutputStream out = new ByteArrayOutputStream(expectedSize);
            byte[] buf = new byte[8192];
            while (!inflater.finished()) {
                int n = inflater.inflate(buf);
                if (n == 0) {
                    if (inflater.needsInput() || inflater.needsDictionary()) break;
                } else {
                    out.write(buf, 0, n);
                    if (out.size() > expectedSize) return null;
                }
            }
            byte[] result = out.toByteArray();
            return result.length == expectedSize ? result : null;
        } catch (DataFormatException e) {
            return null;
        } finally {
            inflater.end();
        }
    }
}
