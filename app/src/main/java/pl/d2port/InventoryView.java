package pl.d2port;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.View;
import pl.d2port.d2.D2Native;

/**
 * The inventory panel as D2Client draws it in 800x600: invchar6.dc6 (four pieces, 2x2) in the panel
 * rectangle from inventory.txt, items at their grid cells or centred in their body-location boxes.
 * The 800x600 screen is scaled to the view's height and centred.
 */
final class InventoryView extends View {
    private static final String PANEL = "DATA\\GLOBAL\\UI\\PANEL\\INVCHAR6.DC6";
    // inventory.txt body components, in the order of D2InventoryComponent, by item body location.
    // Body locations: 1 head, 2 neck, 3 torso, 4 right arm, 5 left arm, 6 right ring, 7 left ring,
    // 8 belt, 9 feet, 10 gloves. Components: 0 rarm, 1 torso, 2 larm, 3 head, 4 neck, 5 rhand,
    // 6 lhand, 7 belt, 8 feet, 9 gloves.
    private static final int[] COMPONENT_OF_BODYLOC = {-1, 3, 4, 1, 0, 2, 5, 6, 7, 8, 9};

    private final Paint paint = new Paint();
    private Bitmap[] panel;
    private int[] panelOff;
    /** Set on the native thread by {@link #update}. */
    private volatile Object[] state;
    private final java.util.Map<String, Bitmap> graphics = new java.util.HashMap<>();

    InventoryView(Context c) { super(c); paint.setFilterBitmap(false); }

    /** Native thread: reads the hero's inventory and loads graphics not seen yet. */
    void update() {
        if (panel == null) {
            Bitmap[] p = Dc6.frames(PANEL);
            if (p != null) { panelOff = Dc6.lastOffsets; panel = p; }
        }
        Object[] inv = D2Native.inventory();
        if (inv == null) return;
        String[] files = (String[]) inv[1];
        Bitmap[] bitmaps = new Bitmap[files.length];
        for (int i = 0; i < files.length; i++) {
            Bitmap b = graphics.get(files[i]);
            if (b == null && !graphics.containsKey(files[i])) {
                Bitmap[] f = Dc6.frames(files[i]);
                b = f != null && f.length > 0 ? f[0] : null;
                graphics.put(files[i], b);
            }
            bitmaps[i] = b;
        }
        state = new Object[]{inv[0], bitmaps, inv[2]};
        postInvalidate();
    }

    @Override protected void onDraw(Canvas c) {
        Object[] s = state;
        if (s == null) return;
        int[] l = (int[]) s[0];
        Bitmap[] items = (Bitmap[]) s[1];
        int[] d = (int[]) s[2];
        float scale = getHeight() / 600f;
        c.save();
        c.translate((getWidth() - 800 * scale) / 2f, 0);
        c.scale(scale, scale);
        int left = l[0], top = l[2];
        Bitmap[] p = panel;
        // Frames 0-3 are the character sheet; this file's frames 4-7 are the inventory (PD2 data).
        if (p != null && p.length >= 8) {
            c.drawBitmap(p[4], left, top, paint);
            c.drawBitmap(p[5], left + p[4].getWidth(), top, paint);
            c.drawBitmap(p[6], left, top + p[4].getHeight(), paint);
            c.drawBitmap(p[7], left + p[4].getWidth(), top + p[4].getHeight(), paint);
        }
        int gridLeft = l[6], gridTop = l[8], boxW = l[10], boxH = l[11];
        for (int i = 0; i < items.length; i++) {
            Bitmap b = items[i];
            if (b == null) continue;
            int page = d[i * 6], body = d[i * 6 + 1], x = d[i * 6 + 2], y = d[i * 6 + 3];
            if (page == 0) { // inventory grid
                c.drawBitmap(b, gridLeft + x * boxW, gridTop + y * boxH, paint);
            } else if (page == 255 && body > 0 && body < COMPONENT_OF_BODYLOC.length) {
                int k = 12 + COMPONENT_OF_BODYLOC[body] * 6;
                float cx = (l[k] + l[k + 1]) / 2f, cy = (l[k + 2] + l[k + 3]) / 2f;
                c.drawBitmap(b, cx - b.getWidth() / 2f, cy - b.getHeight() / 2f, paint);
            }
        }
        c.restore();
    }
}
