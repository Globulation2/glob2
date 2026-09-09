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

    // UI state belongs to the main looper. The reservation also prevents a second
    // native request from racing the first runOnUiThread callback.
    private final java.util.concurrent.atomic.AtomicBoolean documentBusy = new java.util.concurrent.atomic.AtomicBoolean();
    private static final int DOCUMENT_REQUEST = 4127;
    private static final int MAX_DOCUMENT_BYTES = 64 * 1024 * 1024;
    private long documentRequest;
    private byte[] documentExport;
    private String documentError;
    private static native void documentResult(long request, int state, byte[] name, byte[] bytes);

    public boolean openDocument(long request) {
        if (!documentBusy.compareAndSet(false, true)) return false;
        runOnUiThread(() -> {
            documentRequest = request;
            try {
                android.content.Intent intent = new android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(android.content.Intent.CATEGORY_OPENABLE);
                intent.setType("*/*"); // Glob2 validates the extension and complete format after selection.
                startActivityForResult(intent, DOCUMENT_REQUEST);
            } catch (RuntimeException failure) {
                documentResult(request, 3, null, null);
                clearDocument();
            }
        });
        return true;
    }
    public boolean exportDocument(byte[] name, byte[] bytes, byte[] error) {
        if (bytes.length > MAX_DOCUMENT_BYTES || !documentBusy.compareAndSet(false, true)) return false;
        runOnUiThread(() -> {
            documentExport = bytes;
            documentError = new String(error, java.nio.charset.StandardCharsets.UTF_8);
            try {
                android.content.Intent intent = new android.content.Intent(android.content.Intent.ACTION_CREATE_DOCUMENT);
                intent.addCategory(android.content.Intent.CATEGORY_OPENABLE);
                intent.setType("application/octet-stream");
                intent.putExtra(android.content.Intent.EXTRA_TITLE, new String(name, java.nio.charset.StandardCharsets.UTF_8));
                startActivityForResult(intent, DOCUMENT_REQUEST);
            } catch (RuntimeException failure) { exportError(); clearDocument(); }
        });
        return true;
    }
    public void cancelDocument(long request) {
        runOnUiThread(() -> {
            if (request != 0 && request == documentRequest) {
                // Clear ownership first: the platform can deliver a late result.
                documentRequest = 0;
                finishActivity(DOCUMENT_REQUEST);
            }
        });
    }
    private void clearDocument() {
        documentRequest = 0;
        documentExport = null;
        documentBusy.set(false);
    }
    private void exportError() {
        if (!isFinishing() && !isDestroyed()) new android.app.AlertDialog.Builder(this)
            .setMessage(documentError)
            .setPositiveButton(android.R.string.ok, null).show();
    }
    @Override
    protected void onActivityResult(int requestCode, int resultCode, android.content.Intent data) {
        if (requestCode != DOCUMENT_REQUEST) { super.onActivityResult(requestCode, resultCode, data); return; }
        final long request = documentRequest;
        final byte[] outgoing = documentExport;
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            if (request != 0) documentResult(request, 2, null, null);
            clearDocument();
            return;
        }
        final android.net.Uri uri = data.getData();
        if (outgoing == null && request == 0) { clearDocument(); return; }
        // Keep the reservation while provider I/O runs. Never block the game/UI
        // thread on a cloud provider, and bound data even if its size is unknown.
        new Thread(() -> {
            try {
                if (outgoing != null) {
                    try (java.io.OutputStream stream = getContentResolver().openOutputStream(uri, "wt")) {
                        if (stream == null) throw new java.io.IOException("No output stream");
                        stream.write(outgoing);
                        stream.flush();
                    }
                } else {
                    String name = "";
                    try (android.database.Cursor cursor = getContentResolver().query(uri,
                            new String[] {android.provider.OpenableColumns.DISPLAY_NAME, android.provider.OpenableColumns.SIZE}, null, null, null)) {
                        if (cursor != null && cursor.moveToFirst()) {
                            name = cursor.getString(0);
                            if (!cursor.isNull(1) && cursor.getLong(1) > MAX_DOCUMENT_BYTES) throw new java.io.IOException("File too large");
                        }
                    }
                    if (name == null || name.length() > 1024) throw new java.io.IOException("Invalid filename");
                    try (java.io.InputStream stream = getContentResolver().openInputStream(uri);
                         java.io.ByteArrayOutputStream output = new java.io.ByteArrayOutputStream()) {
                        if (stream == null) throw new java.io.IOException("No input stream");
                        byte[] buffer = new byte[65536];
                        int count;
                        while ((count = stream.read(buffer)) != -1) {
                            if (count > MAX_DOCUMENT_BYTES - output.size()) throw new java.io.IOException("File too large");
                            output.write(buffer, 0, count);
                        }
                        documentResult(request, 1, name.getBytes(java.nio.charset.StandardCharsets.UTF_8), output.toByteArray());
                    }
                }
            } catch (Exception | OutOfMemoryError failure) {
                if (outgoing == null) documentResult(request, 3, null, null);
                else runOnUiThread(this::exportError);
            } finally { runOnUiThread(this::clearDocument); }
        }, "Glob2-document").start();
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "c++_shared", "SDL2", "main" };
    }
}
