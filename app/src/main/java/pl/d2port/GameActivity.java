package pl.d2port;

import android.app.Activity;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.File;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import pl.d2port.d2.D2Native;
import pl.d2port.data.InstallationManifest;
import pl.d2port.data.InstallationStore;

/**
 * The game: one native thread runs D2's 25 frames per second; the screen draws the latest state.
 * Movement uses PD2's gamepad scheme (see Stick): the stick picks a point in its direction and a
 * walk/run command to it is repeated every 5 frames; walk or run comes from the run toggle only.
 */
public final class GameActivity extends Activity {
    private static final int FRAME_MS = 40; // 25 frames per second
    private final ScheduledExecutorService nativeThread = Executors.newSingleThreadScheduledExecutor();
    private InstallationManifest.Session session;
    private long act;
    private int levelShown, frame;
    private boolean run;
    /** Developer helper: walk towards this level (0 = off). */
    private volatile int devWalkLevel;
    private LevelView view;
    private TextView status;
    private Stick stick;
    private final Looks looks = new Looks();
    // Shared by the game thread (fills) and the UI thread (draws).
    private final java.util.Map<Integer, android.graphics.Bitmap> tileImages = java.util.Collections.synchronizedMap(new java.util.HashMap<>());
    private final java.util.Map<Integer, int[]> tileOffsets = java.util.Collections.synchronizedMap(new java.util.HashMap<>());
    private int worldSignature;
    private final Automap automap = new Automap();

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        view = new LevelView(this);
        root.addView(view);
        stick = new Stick(this);
        root.addView(stick);
        LinearLayout bar = new LinearLayout(this);
        status = new TextView(this); status.setTextColor(Color.WHITE); status.setPadding(16, 8, 16, 8);
        Button runButton = new Button(this); runButton.setAllCaps(false); runButton.setText("Bieg: wył.");
        runButton.setOnClickListener(v -> { run = !run; runButton.setText(run ? "Bieg: wł." : "Bieg: wył."); });
        Button walk = new Button(this); walk.setAllCaps(false); walk.setText("→ poziom 2 (dev)");
        walk.setOnClickListener(v -> devWalkLevel = devWalkLevel == 0 ? 2 : 0);
        bar.addView(runButton); bar.addView(walk); bar.addView(status);
        root.addView(bar);
        root.setFitsSystemWindows(true);
        setContentView(root);
        status.setText("Wczytywanie…");
        nativeThread.execute(this::start);
    }

    private void start() {
        try {
            InstallationStore store = new InstallationStore(new File(getFilesDir(), "installation"));
            DataImport.importPending(this, store);
            if (store.current() == null) throw new java.io.IOException("Brak plików gry (MPQ)");
            session = store.open();
            if (!D2Native.init(session.data) || !D2Native.loadPalette(0)) throw new java.io.IOException("Nie udało się wczytać tabel");
            // A new single-player game run by D2Game, as the original client created one.
            act = D2Native.startGame(4, 0);
            if (act == 0) throw new java.io.IOException("D2Game nie utworzył gry (logcat D2Native)");
            automap.load();
            view.automap = automap;
            nativeThread.scheduleAtFixedRate(this::gameFrame, FRAME_MS, FRAME_MS, TimeUnit.MILLISECONDS);
        } catch (Exception ex) {
            runOnUiThread(() -> status.setText("Błąd: " + ex.getMessage()));
        }
    }

    /** Rebuilds the drawn world when the set of active rooms changed (rooms come and go as the hero walks). */
    private void refreshWorld() {
        int[] t = D2Native.activeTiles(act);
        if (t == null) return;
        int sig = t.length * 31 + (t.length > 6 ? t[1] * 7 + t[2] * 13 + t[t.length - 5] * 17 + t[t.length - 4] : 0);
        if (sig == worldSignature) return;
        worldSignature = sig;
        LevelView.Level lv = LevelView.fromActiveRooms(act, tileImages, tileOffsets);
        int[] s = D2Native.serverPlayer();
        if (lv != null) runOnUiThread(() -> { view.setLevel(lv); status.setText("Poziom " + s[6]); });
    }

    private void showLevelOfPlayer() {
        int[] s = D2Native.serverPlayer();
        if (s[6] == levelShown || s[6] == 0) return;
        levelShown = s[6];
        long t = System.nanoTime();
        LevelView.Level lv = LevelView.load(act, s[6]);
        long ms = (System.nanoTime() - t) / 1000000;
        runOnUiThread(() -> { if (lv != null) view.setLevel(lv); status.setText("Poziom " + s[6] + " (" + ms + " ms)"); });
    }

    /** One D2 frame on the native thread: commands in, server frame, state out. */
    private void gameFrame() {
        try {
            frame++;
            float[] dir = stick.direction();
            if (dir != null && frame % 5 == 0 && D2Native.serverReady()) {
                int[] s = D2Native.serverPlayer();
                // Screen direction -> subtile offset (isometric: x right-down, y left-down).
                float dist = 3 + 5 * dir[2];
                float sx = dir[0] * dist, sy = dir[1] * dist;
                int tx = Math.round(s[0] + sx + sy * 2), ty = Math.round(s[1] + sy * 2 - sx);
                D2Native.serverMove(tx, ty, run);
            }
            if (devWalkLevel > 0 && frame % 10 == 0 && dir == null && !D2Native.devWalkToLevel(devWalkLevel, run)) devWalkLevel = 0;
            D2Native.serverTick();
            int[] s = D2Native.serverPlayer();
            java.util.List<LevelView.Sprite> list = new java.util.ArrayList<>();
            float hx = s[2] / 65536f, hy = s[3] / 65536f;
            LevelView.Sprite hero = sprite(0, 0, hx, hy, s[5], s[7]);
            if (hero != null) list.add(hero);
            int[] u = D2Native.serverUnits();
            for (int i = 0; i + 7 < u.length; i += 8) {
                if (u[i] == 4) continue; // items lie in inventories here; ground items come later
                // Only units near the hero (a screen and a half).
                if (Math.abs(u[i + 3] - s[0]) + Math.abs(u[i + 4] - s[1]) > 60) continue;
                LevelView.Sprite sp = sprite(u[i], u[i + 2], u[i + 3] + 0.5f, u[i + 4] + 0.5f, u[i + 6], u[i + 7]);
                if (sp != null) list.add(sp);
            }
            view.sprites = list.toArray(new LevelView.Sprite[0]);
            view.heroFrame = hero != null ? hero.frame : null;
            view.heroX = hx; view.heroY = hy;
            view.postInvalidateOnAnimation();
            if (frame % 10 == 0) refreshWorld();
            if (frame % 25 == 5) automap.update(act);
        } catch (RuntimeException ex) {
            android.util.Log.e("D2Native", "frame", ex);
        }
    }

    private LevelView.Sprite sprite(int type, int guid, float x, float y, int dir64, int frame) {
        Looks.Anim a = looks.get(D2Native.unitLook(type, guid), dir64);
        if (a == null || a.frames == null || a.frames.length == 0) return null;
        return new LevelView.Sprite(x, y, a.frames[Math.max(0, Math.min(a.frames.length - 1, frame))], a.offX, a.offY);
    }

    @Override protected void onDestroy() {
        super.onDestroy();
        nativeThread.execute(() -> {
            if (act != 0) D2Native.freeAct(act);
            act = 0;
            try { if (session != null) session.close(); } catch (java.io.IOException ignored) {}
        });
        nativeThread.shutdown();
    }

    /**
     * Virtual stick, PD2 style: square dead zone per axis (0.3 of the radius, PD2's value for
     * controller_deadzone 20), outside it only the direction and tilt are passed on.
     */
    static final class Stick extends View {
        private float cx, cy, x, y;
        private volatile float dx, dy, tilt;
        private volatile boolean active;
        private final Paint ring = new Paint(), knob = new Paint();
        Stick(Context c) {
            super(c);
            ring.setColor(0x60ffffff); ring.setStyle(Paint.Style.STROKE); ring.setStrokeWidth(4);
            knob.setColor(0x90c8a868);
        }
        private float radius() { return Math.min(getWidth(), getHeight()) * 0.16f; }
        @Override public boolean onTouchEvent(MotionEvent e) {
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    if (e.getX() > getWidth() * 0.5f) return false;
                    cx = e.getX(); cy = e.getY(); x = cx; y = cy; active = true; update(); break;
                case MotionEvent.ACTION_MOVE: x = e.getX(); y = e.getY(); update(); break;
                case MotionEvent.ACTION_UP: case MotionEvent.ACTION_CANCEL: active = false; dx = dy = 0; break;
            }
            invalidate();
            return true;
        }
        private void update() {
            float r = radius(), ax = (x - cx) / r, ay = (y - cy) / r;
            float len = (float) Math.hypot(ax, ay);
            if (len > 1) { ax /= len; ay /= len; len = 1; }
            if (Math.abs(ax) < 0.3f && Math.abs(ay) < 0.3f) { dx = dy = 0; tilt = 0; return; }
            dx = ax / len; dy = ay / len; tilt = len;
        }
        /** {dirX, dirY, tilt} in screen space, or null when idle. */
        float[] direction() { return active && (dx != 0 || dy != 0) ? new float[]{dx, dy, tilt} : null; }
        @Override protected void onDraw(Canvas c) {
            if (!active) return;
            float r = radius();
            c.drawCircle(cx, cy, r, ring);
            float kx = x - cx, ky = y - cy, l = (float) Math.hypot(kx, ky);
            if (l > r) { kx = kx / l * r; ky = ky / l * r; }
            c.drawCircle(cx + kx, cy + ky, r * 0.35f, knob);
        }
    }
}
