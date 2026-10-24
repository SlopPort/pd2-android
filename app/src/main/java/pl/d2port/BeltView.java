package pl.d2port;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.MotionEvent;
import android.view.View;
import pl.d2port.d2.D2Native;

/**
 * The belt's first row (the four quick slots of D2's control panel) at the bottom centre of the
 * screen. A tap drinks/uses the item in that slot with packet 0x26, as the 1..4 keys do in D2.
 * Belt items have mode IMODE_INBELT (2); their x is the slot index (row * 4 + column).
 */
final class BeltView extends View {
    private static final int SLOTS = 4, SLOT = 34; // slot size in 800x600 pixels, about D2's
    private final Paint paint = new Paint(), frame = new Paint();
    private final java.util.Map<String, Bitmap> graphics = new java.util.HashMap<>();
    /** Per slot: {guid or -1}, and the bitmap; set on the native thread. */
    private volatile int[] guids = new int[SLOTS];
    private volatile Bitmap[] icons = new Bitmap[SLOTS];
    private volatile int pendingSlot = -1;

    BeltView(Context c) {
        super(c);
        frame.setStyle(Paint.Style.STROKE); frame.setStrokeWidth(1.5f); frame.setColor(0xa0c8a868);
        java.util.Arrays.fill(guids, -1);
    }

    /** Native thread: reads the belt from the inventory list and sends a pending use. */
    void update() {
        int slot = pendingSlot;
        if (slot >= 0) { pendingSlot = -1; if (guids[slot] >= 0) D2Native.itemCommand(0x26, guids[slot], 0, 0, 0); }
        Object[] inv = D2Native.inventory();
        if (inv == null) return;
        String[] files = (String[]) inv[1];
        int[] d = (int[]) inv[2];
        int[] g = new int[SLOTS];
        Bitmap[] b = new Bitmap[SLOTS];
        java.util.Arrays.fill(g, -1);
        for (int i = 0; i < files.length; i++) {
            int x = d[i * 8 + 2], mode = d[i * 8 + 7];
            if (mode != 2 || x < 0 || x >= SLOTS) continue;
            g[x] = d[i * 8 + 6];
            Bitmap bm = graphics.get(files[i]);
            if (bm == null && !graphics.containsKey(files[i])) {
                Bitmap[] f = Dc6.frames(files[i]);
                bm = f != null && f.length > 0 ? f[0] : null;
                graphics.put(files[i], bm);
            }
            b[x] = bm;
        }
        guids = g; icons = b;
        postInvalidate();
    }

    private float scale() { return getHeight() / 600f; }
    private float left() { return getWidth() / 2f - SLOTS * SLOT * scale() / 2f; }

    @Override protected void onDraw(Canvas c) {
        float s = scale(), x0 = left(), y0 = getHeight() - (SLOT + 6) * s;
        Bitmap[] b = icons;
        for (int i = 0; i < SLOTS; i++) {
            float x = x0 + i * SLOT * s;
            c.drawRect(x, y0, x + SLOT * s, y0 + SLOT * s, frame);
            if (b[i] != null) {
                float w = b[i].getWidth() * s, h = b[i].getHeight() * s;
                c.drawBitmap(b[i], null, new android.graphics.RectF(x + (SLOT * s - w) / 2, y0 + (SLOT * s - h) / 2, x + (SLOT * s + w) / 2, y0 + (SLOT * s + h) / 2), paint);
            }
        }
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        float s = scale(), x0 = left(), y0 = getHeight() - (SLOT + 6) * s;
        if (e.getY() < y0 || e.getX() < x0 || e.getX() >= x0 + SLOTS * SLOT * s) return false;
        if (e.getActionMasked() == MotionEvent.ACTION_UP) pendingSlot = (int) ((e.getX() - x0) / (SLOT * s));
        return true;
    }
}
