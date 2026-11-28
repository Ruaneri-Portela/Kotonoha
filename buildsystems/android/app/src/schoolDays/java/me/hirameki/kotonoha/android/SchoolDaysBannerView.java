package me.hirameki.kotonoha.android;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Shader;
import android.util.AttributeSet;
import android.view.View;

/**
 * Renders the top character-focused crop of SchoolDaysHQ-Launcher.png matching
 * the Windows SchoolDaysLauncher setup header, with a soft translucent sky fade
 * on the right side for the "School Days HQ" title overlay.
 */
public final class SchoolDaysBannerView extends View {
    private static final int FALLBACK_BG = 0xFFE5F3F7;

    private final Paint bitmapPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint gradientPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Rect srcRect = new Rect();
    private final Rect dstRect = new Rect();

    private Bitmap artwork;
    private int lastGradientWidth = -1;

    public SchoolDaysBannerView(Context context) {
        super(context);
    }

    public SchoolDaysBannerView(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public SchoolDaysBannerView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public void setArtwork(Bitmap bitmap) {
        if (this.artwork == bitmap) return;
        this.artwork = bitmap;
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int w = getWidth();
        int h = getHeight();
        if (w <= 0 || h <= 0) return;

        canvas.drawColor(FALLBACK_BG);

        if (artwork == null || artwork.isRecycled()) return;

        int bw = artwork.getWidth();
        int bh = artwork.getHeight();
        if (bw <= 0 || bh <= 0) return;

        // Mirror Windows SchoolDaysLauncher::drawOptionsScreen() upper crop:
        // span the full horizontal artwork so Kotonoha and Sekai sit on the
        // left ~55% and the clear sky occupies the right ~45%, while cropping
        // vertically around their faces and excluding the bottom retail logo.
        float viewAspect = (float) w / (float) h;
        float maxCropH = bh * 0.54f;
        float cropH = Math.min(maxCropH, bw / Math.max(1.0f, viewAspect));
        float focusCenterY = bh * 0.24f;
        float top = Math.max(0.0f, Math.min(maxCropH - cropH, focusCenterY - cropH * 0.5f));
        int srcTop = Math.round(top);
        int srcBottom = Math.min(bh, Math.max(srcTop + 1, Math.round(top + cropH)));

        srcRect.set(0, srcTop, bw, srcBottom);
        dstRect.set(0, 0, w, h);
        canvas.drawBitmap(artwork, srcRect, dstRect, bitmapPaint);

        if (lastGradientWidth != w) {
            lastGradientWidth = w;
            gradientPaint.setShader(new LinearGradient(
                    w * 0.46f, 0.0f, w, 0.0f,
                    new int[]{0x00F7FAFD, 0xD8F7FAFD, 0xF5F7FAFD},
                    new float[]{0.0f, 0.55f, 1.0f},
                    Shader.TileMode.CLAMP));
        }
        canvas.drawRect(w * 0.46f, 0.0f, w, h, gradientPaint);
    }
}
