package pl.d2port;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import pl.d2port.d2.D2Native;

/**
 * The automap: cells chosen by D2Common (automap.txt) for the tiles of revealed rooms, drawn with
 * the frames of data/global/ui/automap/maximap.dc6, like D2Client's automap. Shown as a minimap.
 */
final class Automap {
    private static final String CELLS_FILE = "DATA\\GLOBAL\\UI\\AUTOMAP\\MAXIMAP.DC6";
    /** One tile (160x80 in the world) is 1/5 of that on the automap. */
    static final float TILE_W = 32f, TILE_H = 16f;

    private Bitmap[] frames;
    private int[] offX, offY;
    private volatile int[] cells = new int[0];
    private final Paint paint = new Paint(), heroDot = new Paint(), frame = new Paint();

    Automap() {
        paint.setAlpha(255);
        heroDot.setColor(0xffffffff);
        frame.setColor(0xb0000000);
    }

    /** Native thread: load the cell graphics once. */
    void load() {
        int[] d = D2Native.dc6Frames(CELLS_FILE);
        if (d == null) { android.util.Log.w("D2Native", "automap graphics missing"); return; }
        int n = d[0], p = 1;
        Bitmap[] f = new Bitmap[n]; int[] ox = new int[n], oy = new int[n];
        for (int i = 0; i < n; i++) {
            int w = d[p], h = d[p + 1];
            ox[i] = d[p + 2]; oy[i] = d[p + 3];
            f[i] = w > 0 && h > 0 ? Bitmap.createBitmap(d, p + 4, w, w, h, Bitmap.Config.ARGB_8888) : null;
            p += 4 + w * h;
        }
        offX = ox; offY = oy; frames = f;
        if (n > 0) android.util.Log.i("D2Native", "automap: " + n + " cell frames, first " + d[1] + "x" + d[2] + " off " + d[3] + "," + d[4]);
    }

    /** Native thread: refresh the revealed cells. */
    void update(long act) {
        int[] c = D2Native.automapCells(act);
        if (c != null) { if (c.length != cells.length) android.util.Log.i("D2Native", "automap cells: " + c.length / 3); cells = c; }
    }

    /** UI thread: draw centred on the hero (subtiles) inside the given screen rectangle. */
    void draw(Canvas c, float heroX, float heroY, RectF area) {
        Bitmap[] f = frames;
        int[] cl = cells;
        if (f == null || Float.isNaN(heroX)) return;
        c.save();
        c.clipRect(area);
        c.drawRect(area, frame);
        float cx = area.centerX(), cy = area.centerY();
        float htx = heroX / 5f, hty = heroY / 5f;
        for (int i = 0; i + 2 < cl.length; i += 3) {
            int cell = cl[i + 2];
            if (cell < 0 || cell >= f.length || f[cell] == null) continue;
            float tx = cl[i] - htx, ty = cl[i + 1] - hty;
            float x = cx + (tx - ty) * TILE_W / 2f, y = cy + (tx + ty) * TILE_H / 2f;
            // DC6 offsets place the frame's bottom-left corner relative to the cell anchor.
            float left = x + offX[cell] - TILE_W / 2f, top = y + offY[cell] - f[cell].getHeight();
            if (left > area.right || left + f[cell].getWidth() < area.left || top > area.bottom || top + f[cell].getHeight() < area.top) continue;
            c.drawBitmap(f[cell], left, top, paint);
        }
        c.drawCircle(cx, cy, 4, heroDot);
        c.restore();
    }
}
