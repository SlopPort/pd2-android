package pl.d2port;

import android.graphics.Bitmap;
import pl.d2port.d2.D2Native;

/** DC6 files as bitmaps (native thread). */
final class Dc6 {
    private Dc6() {}

    /** Offsets {x, y} per frame of the last {@link #frames} call (DC6 frame header values). */
    static int[] lastOffsets;

    /** All frames of a DC6 file in the current palette, or null when missing. */
    static Bitmap[] frames(String path) {
        int[] d = D2Native.dc6Frames(path);
        if (d == null) return null;
        int n = d[0], p = 1;
        Bitmap[] f = new Bitmap[n];
        int[] off = new int[n * 2];
        for (int i = 0; i < n; i++) {
            int w = d[p], h = d[p + 1];
            off[i * 2] = d[p + 2]; off[i * 2 + 1] = d[p + 3];
            f[i] = w > 0 && h > 0 ? Bitmap.createBitmap(d, p + 4, w, w, h, Bitmap.Config.ARGB_8888) : null;
            p += 4 + w * h;
        }
        lastOffsets = off;
        return f;
    }
}
