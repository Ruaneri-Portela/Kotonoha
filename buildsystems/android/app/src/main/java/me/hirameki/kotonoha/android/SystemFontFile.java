package me.hirameki.kotonoha.android;

import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.fonts.Font;
import android.graphics.fonts.FontStyle;
import android.graphics.fonts.SystemFonts;
import android.graphics.text.PositionedGlyphs;
import android.graphics.text.TextRunShaper;
import android.os.Build;

import java.io.File;
import java.util.Locale;

/** Find a system-owned file for the default Japanese UI font, without copying it. */
final class SystemFontFile {
    private SystemFontFile() {}

    static String japaneseSans() {
        if (Build.VERSION.SDK_INT >= 31) {
            Paint paint = new Paint();
            paint.setTypeface(Typeface.create("sans-serif", Typeface.NORMAL));
            paint.setTextLocale(Locale.JAPAN);
            PositionedGlyphs run = TextRunShaper.shapeTextRun("あ", 0, 1, 0, 1, 0, 0, false, paint);
            if (run.glyphCount() > 0) {
                File file = run.getFont(0).getFile();
                if (file != null && file.canRead()) return file.getAbsolutePath();
            }
        }
        if (Build.VERSION.SDK_INT >= 29) {
            for (Font font : SystemFonts.getAvailableFonts()) {
                if (font.getStyle().getWeight() != FontStyle.FONT_WEIGHT_NORMAL
                        || font.getStyle().getSlant() != FontStyle.FONT_SLANT_UPRIGHT) continue;
                for (int i = 0; i < font.getLocaleList().size(); i++) {
                    if ("ja".equals(font.getLocaleList().get(i).getLanguage())) {
                        File file = font.getFile();
                        if (file != null && file.canRead()) return file.getAbsolutePath();
                    }
                }
            }
        }
        return "";
    }
}
