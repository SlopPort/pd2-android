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
class LevelView extends View {
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
    /** Hero position in subtiles (with fraction) or NaN; when set the camera follows it. */
    volatile float heroX = Float.NaN, heroY = Float.NaN;
    /** Current hero animation frame and its offset from the foot point. */
    volatile Bitmap heroFrame; volatile int heroOffX, heroOffY;
    private final Paint hero = new Paint(), heroRing = new Paint();

    /** A unit to draw: foot point in subtiles (with fraction), current frame and its offset. */
    static final class Sprite {
        final float x, y; final Bitmap frame; final int offX, offY;
        Sprite(float x, float y, Bitmap frame, int offX, int offY) { this.x = x; this.y = y; this.frame = frame; this.offX = offX; this.offY = offY; }
    }
    /** Units of the current frame, hero included; replaced whole by the game thread. */
    volatile Sprite[] sprites = new Sprite[0];
    /** Minimap in the top-right corner (null: none). */
    volatile Automap automap;
    private final android.graphics.RectF mapArea = new android.graphics.RectF();
    private final Paint paint = new Paint(), red = new Paint();

    LevelView(Context c) {
        super(c);
        paint.setFilterBitmap(false);
        red.setColor(0x80ff2020);
        hero.setColor(0xffe0c070);
        heroRing.setColor(0xff000000); heroRing.setStyle(Paint.Style.STROKE); heroRing.setStrokeWidth(3);
    }

    void setLevel(Level l) {
        level = l;
        if (!Float.isNaN(heroX)) { invalidate(); return; }
        // Start centred on the level.
        float cx = l.posX + l.width / 2f, cy = l.posY + l.height / 2f;
        camX = (cx - cy) * 80; camY = (cx + cy) * 40;
        invalidate();
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        if (!Float.isNaN(heroX)) return false; // in game the stick view handles touches
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
        float hx = heroX, hy = heroY;
        if (!Float.isNaN(hx)) {
            camX = (hx - hy) * 16f; camY = (hx + hy) * 8f;
            scale = getHeight() / 600f; // the game shows what D2 shows at 800x600
        }
        c.save();
        c.translate(getWidth() / 2f, getHeight() / 2f);
        c.scale(scale, scale);
        c.translate(-camX, -camY);
        float viewW = getWidth() / scale, viewH = getHeight() / scale;
        int[] t = l.tiles;
        for (int i = 0; i + 5 < t.length; i += 6) if (t[i] == 0) drawTile(c, l, i, viewW, viewH);
        // Walls/roofs and units in depth order (further up the screen first). A wall tile's depth is
        // the subtile sum at its bottom row, a unit's that of its foot point.
        Sprite[] sp = sprites.clone();
        java.util.Arrays.sort(sp, (p, q) -> Float.compare(p.x + p.y, q.x + q.y));
        int si = 0;
        for (Integer i : l.order) {
            float wallDepth = (t[i + 1] + t[i + 2]) * 5 + 8;
            while (si < sp.length && sp[si].x + sp[si].y < wallDepth) drawSprite(c, sp[si++]);
            drawTile(c, l, i, viewW, viewH);
        }
        while (si < sp.length) drawSprite(c, sp[si++]);
        if (!Float.isNaN(hx) && heroFrame == null) {
            float sx = (hx - hy) * 16f, sy = (hx + hy) * 8f;
            c.drawRect(sx - 8, sy - 70, sx + 8, sy, hero); c.drawRect(sx - 8, sy - 70, sx + 8, sy, heroRing);
        }
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
        Automap am = automap;
        if (am != null && !Float.isNaN(hx)) {
            float w = getWidth() * 0.28f, h = getHeight() * 0.38f;
            mapArea.set(getWidth() - w - 16, 16, getWidth() - 16, 16 + h);
            am.draw(c, hx, hy, mapArea);
        }
        // Life and mana globes (hlthmana.dc6: frame 0 life, 1 mana), filled from the bottom.
        android.graphics.Bitmap[] o = orbs;
        int[] v = vitals;
        if (o != null && v != null) {
            float size = o[0].getHeight() * scale;
            float y = getHeight() - size - 16;
            drawOrb(c, o[0], 16, y, size, v[1] > 0 ? (float) v[0] / v[1] : 0);
            drawOrb(c, o[1], getWidth() - size - 360, y, size, v[3] > 0 ? (float) v[2] / v[3] : 0);
        }
    }

    /** Globe graphics and the hero's {life, max, mana, max}; set from the game thread. */
    volatile android.graphics.Bitmap[] orbs;
    volatile int[] vitals;

    private void drawOrb(Canvas c, android.graphics.Bitmap b, float x, float y, float size, float fill) {
        fill = Math.max(0, Math.min(1, fill));
        float w = b.getWidth() * scale;
        c.save();
        c.clipRect(x, y + size * (1 - fill), x + w, y + size);
        c.drawBitmap(b, null, new android.graphics.RectF(x, y, x + w, y + size), paint);
        c.restore();
    }

    private void drawSprite(Canvas c, Sprite s) {
        if (s.frame == null) return;
        float sx = (s.x - s.y) * 16f, sy = (s.x + s.y) * 8f;
        c.drawBitmap(s.frame, sx + s.offX, sy + s.offY, paint);
    }

    private void drawTile(Canvas c, Level l, int i, float viewW, float viewH) {
        int[] t = l.tiles;
        if ((t[i + 5] & 0x8) != 0) return; // MAPTILE_HIDDEN: warps and helpers the game does not draw
        Bitmap b = l.images.get(t[i + 4]);
        if (b == null) return;
        int[] o = l.offsets.get(t[i + 4]);
        // Block x runs over the 160-pixel tile box whose top corner is at x = 80.
        float x = (t[i + 1] - t[i + 2]) * 80f - 80 + o[0], y = (t[i + 1] + t[i + 2]) * 40f + o[1];
        if (x > camX + viewW || x + b.getWidth() < camX - viewW || y > camY + viewH || y + b.getHeight() < camY - viewH) return;
        c.drawBitmap(b, x, y, paint);
    }

    /** Builds the drawable level from the native core (call on the native thread). */
    static Level load(long act, int levelId) {
        int[] i = pl.d2port.d2.D2Native.levelInfo(act, levelId);
        if (i == null) return null;
        Level lv = new Level();
        lv.posX = i[2]; lv.posY = i[3]; lv.width = i[4]; lv.height = i[5];
        lv.tiles = pl.d2port.d2.D2Native.levelTiles(act, levelId);
        lv.collision = pl.d2port.d2.D2Native.levelCollision(act, levelId);
        if (lv.tiles == null) lv.tiles = new int[0];
        for (int k = 0; k + 5 < lv.tiles.length; k += 6) {
            int key = lv.tiles[k + 4];
            if (lv.images.containsKey(key)) continue;
            int[] img = pl.d2port.d2.D2Native.tileImage(key);
            if (img == null) { lv.images.put(key, null); continue; }
            lv.images.put(key, Bitmap.createBitmap(img, 4, img[0], img[0], img[1], Bitmap.Config.ARGB_8888));
            lv.offsets.put(key, new int[]{img[2], img[3]});
        }
        sortDepth(lv);
        return lv;
    }

    /**
     * Drawable world from the rooms the game keeps active (any level); tile graphics come from and go
     * into the shared cache, so only new tiles are decoded. Native thread only.
     */
    static Level fromActiveRooms(long act, java.util.Map<Integer, Bitmap> images, java.util.Map<Integer, int[]> offsets) {
        int[] t = pl.d2port.d2.D2Native.activeTiles(act);
        if (t == null) return null;
        Level lv = new Level();
        lv.tiles = t;
        lv.images = images; lv.offsets = offsets;
        for (int k = 0; k + 5 < t.length; k += 6) {
            int key = t[k + 4];
            if (images.containsKey(key)) continue;
            int[] img = pl.d2port.d2.D2Native.tileImage(key);
            if (img == null) { images.put(key, null); continue; }
            images.put(key, Bitmap.createBitmap(img, 4, img[0], img[0], img[1], Bitmap.Config.ARGB_8888));
            offsets.put(key, new int[]{img[2], img[3]});
        }
        sortDepth(lv);
        return lv;
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
