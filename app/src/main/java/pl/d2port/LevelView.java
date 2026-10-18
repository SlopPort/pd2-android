package pl.d2port;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.MotionEvent;
import android.view.View;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Draws one level as D2 laid it out: isometric tiles (160x80), optional collision overlay. */
final class LevelView extends View {
    static final class Level {
        int[] tiles;                       // see D2Native.levelTiles
        Map<Integer, Bitmap> images = new HashMap<>();
        Map<Integer, int[]> offsets = new HashMap<>();
        short[] collision; int posX, posY, width, height;
        Integer[] order;                   // walls/roofs sorted by depth
    }
    private Level level;
    private float camX, camY, lastX, lastY, scale = 0.5f;
    boolean showCollision;
    private final Paint paint = new Paint(), red = new Paint();

    LevelView(Context c) {
        super(c);
        paint.setFilterBitmap(false);
        red.setColor(0x80ff2020);
    }

    void setLevel(Level l) {
        level = l;
        // Start centred on the level.
        float cx = l.posX + l.width / 2f, cy = l.posY + l.height / 2f;
        camX = (cx - cy) * 80; camY = (cx + cy) * 40;
        invalidate();
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        if (e.getAction() == MotionEvent.ACTION_DOWN) { lastX = e.getX(); lastY = e.getY(); return true; }
        if (e.getAction() == MotionEvent.ACTION_MOVE) {
            camX -= (e.getX() - lastX) / scale; camY -= (e.getY() - lastY) / scale;
            lastX = e.getX(); lastY = e.getY(); invalidate(); return true;
        }
        return true;
    }

    @Override protected void onDraw(Canvas c) {
        c.drawColor(Color.BLACK);
        Level l = level;
        if (l == null || l.tiles == null) return;
        c.save();
        c.translate(getWidth() / 2f, getHeight() / 2f);
        c.scale(scale, scale);
        c.translate(-camX, -camY);
        float viewW = getWidth() / scale, viewH = getHeight() / scale;
        int[] t = l.tiles;
        for (int i = 0; i + 5 < t.length; i += 6) if (t[i] == 0) drawTile(c, l, i, viewW, viewH);
        for (Integer i : l.order) drawTile(c, l, i, viewW, viewH);
        if (showCollision && l.collision != null) {
            int w = l.width * 5;
            for (int y = 0; y < l.height * 5; y++)
                for (int x = 0; x < w; x++) {
                    int m = l.collision[y * w + x] & 0xFFFF;
                    if ((m & 1) == 0 || m == 0xFFFF) continue;
                    // Subtile diamond centre: 32x16 pixels per subtile.
                    float sx = (l.posX * 5 + x - (l.posY * 5 + y)) * 16f, sy = (l.posX * 5 + x + l.posY * 5 + y) * 8f + 8;
                    if (Math.abs(sx - camX) > viewW || Math.abs(sy - camY) > viewH) continue;
                    c.drawRect(sx - 8, sy - 4, sx + 8, sy + 4, red);
                }
        }
        c.restore();
    }

    private void drawTile(Canvas c, Level l, int i, float viewW, float viewH) {
        int[] t = l.tiles;
        Bitmap b = l.images.get(t[i + 4]);
        if (b == null) return;
        int[] o = l.offsets.get(t[i + 4]);
        // Block x runs over the 160-pixel tile box whose top corner is at x = 80.
        float x = (t[i + 1] - t[i + 2]) * 80f - 80 + o[0], y = (t[i + 1] + t[i + 2]) * 40f + o[1];
        if (x > camX + viewW || x + b.getWidth() < camX - viewW || y > camY + viewH || y + b.getHeight() < camY - viewH) return;
        c.drawBitmap(b, x, y, paint);
    }

    static void sortDepth(Level l) {
        List<Integer> idx = new ArrayList<>();
        int[] t = l.tiles;
        for (int i = 0; i + 5 < t.length; i += 6) if (t[i] != 0) idx.add(i);
        Integer[] a = idx.toArray(new Integer[0]);
        Arrays.sort(a, (p, q) -> {
            int dp = t[p + 1] + t[p + 2], dq = t[q + 1] + t[q + 2];
            if (dp != dq) return Integer.compare(dp, dq);
            return Integer.compare(t[p], t[q]); // roofs after walls
        });
        l.order = a;
    }
}
