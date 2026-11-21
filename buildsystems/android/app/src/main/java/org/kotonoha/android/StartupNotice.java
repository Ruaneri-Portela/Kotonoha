package org.kotonoha.android;

import android.view.View;
import android.widget.TextView;

/** Accessible inline startup feedback. The launcher stays available for retry. */
public final class StartupNotice {
    private final TextView message;

    public StartupNotice(TextView message) {
        this.message = message;
        message.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
    }

    public void show(CharSequence text) {
        message.setText(text);
        message.setVisibility(View.VISIBLE);
    }
}
