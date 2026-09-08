package org.globulation.glob2;

import org.libsdl.app.SDLActivity;
import android.os.Bundle;
import android.os.Build;
import android.view.WindowInsets;
import android.graphics.Insets;

public final class Glob2Activity extends SDLActivity {
    private static volatile int[] uiInsets = new int[] {0, 0, 0, 0};
    public static int[] getUiInsets() { return uiInsets; }
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (mLayout == null) return;
        mLayout.setOnApplyWindowInsetsListener((view, insets) -> {
            int left, top, right, bottom;
            if (Build.VERSION.SDK_INT >= 30) {
                Insets safe = insets.getInsets(WindowInsets.Type.systemBars()
                    | WindowInsets.Type.displayCutout() | WindowInsets.Type.ime());
                left = safe.left; top = safe.top; right = safe.right; bottom = safe.bottom;
            } else {
                left = insets.getSystemWindowInsetLeft(); top = insets.getSystemWindowInsetTop();
                right = insets.getSystemWindowInsetRight(); bottom = insets.getSystemWindowInsetBottom();
                if (Build.VERSION.SDK_INT >= 28 && insets.getDisplayCutout() != null) {
                    left = Math.max(left, insets.getDisplayCutout().getSafeInsetLeft());
                    top = Math.max(top, insets.getDisplayCutout().getSafeInsetTop());
                    right = Math.max(right, insets.getDisplayCutout().getSafeInsetRight());
                    bottom = Math.max(bottom, insets.getDisplayCutout().getSafeInsetBottom());
                }
            }
            // Publish immutable UI geometry; keep the game surface edge to edge.
            uiInsets = new int[] {left, top, right, bottom};
            return insets;
        });
        mLayout.requestApplyInsets();
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "c++_shared", "SDL2", "main" };
    }
}
