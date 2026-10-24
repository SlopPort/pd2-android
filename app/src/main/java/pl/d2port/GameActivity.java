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
 * Movement uses a gamepad-style scheme (see Stick): the stick picks a point in its direction and a
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
    /** Developer helper: walk to the nearest hostile monster. */
    private volatile boolean devWalkMonster;
    private LevelView view;
    private TextView status;
    private Stick stick;
    private volatile boolean attackHeld, pickRequested, inventoryOpen;
    private InventoryView inventory;
    private BeltView belt;
    private boolean deadShown;
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
        belt = new BeltView(this);
        root.addView(belt);
        inventory = new InventoryView(this);
        inventory.setVisibility(View.GONE);
        root.addView(inventory);
        LinearLayout bar = new LinearLayout(this);
        status = new TextView(this); status.setTextColor(Color.WHITE); status.setPadding(16, 8, 16, 8);
        Button runButton = new Button(this); runButton.setAllCaps(false); runButton.setText("Bieg: wył.");
        runButton.setOnClickListener(v -> { run = !run; runButton.setText(run ? "Bieg: wł." : "Bieg: wył."); });
        Button walk = new Button(this); walk.setAllCaps(false); walk.setText("→ poziom 2 (dev)");
        walk.setOnClickListener(v -> devWalkLevel = devWalkLevel == 0 ? 2 : 0);
        Button hunt = new Button(this); hunt.setAllCaps(false); hunt.setText("→ potwór (dev)");
        hunt.setOnClickListener(v -> devWalkMonster = !devWalkMonster);
        Button invButton = new Button(this); invButton.setAllCaps(false); invButton.setText("Ekwipunek");
        invButton.setOnClickListener(v -> { inventoryOpen = !inventoryOpen; inventory.setVisibility(inventoryOpen ? View.VISIBLE : View.GONE); });
        bar.addView(runButton); bar.addView(walk); bar.addView(hunt); bar.addView(invButton); bar.addView(status);
        root.addView(bar);
        // Attack, gamepad style: held, it keeps hitting the nearest monster in reach.
        Button attack = new Button(this); attack.setAllCaps(false); attack.setText("Atak");
        attack.setOnTouchListener((v, e) -> {
            int a = e.getActionMasked();
            if (a == MotionEvent.ACTION_DOWN) attackHeld = true;
            else if (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) attackHeld = false;
            return false;
        });
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(260, 260, android.view.Gravity.BOTTOM | android.view.Gravity.END);
        lp.setMargins(0, 0, 60, 60);
        root.addView(attack, lp);
        Button pick = new Button(this); pick.setAllCaps(false); pick.setText("Podnieś");
        pick.setOnClickListener(v -> pickRequested = true);
        FrameLayout.LayoutParams lp2 = new FrameLayout.LayoutParams(260, 140, android.view.Gravity.BOTTOM | android.view.Gravity.END);
        lp2.setMargins(0, 0, 60, 340);
        root.addView(pick, lp2);
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
            // Developer runs can fix the seed: adb shell am start -n pl.d2port/.GameActivity --ei seed 777
            D2Native.setGameSeed(getIntent().getIntExtra("seed", -1));
            act = D2Native.startGame(4, 0);
            if (act == 0) throw new java.io.IOException("D2Game nie utworzył gry (logcat D2Native)");
            automap.load();
            view.automap = automap;
            int[] g = D2Native.dc6Frames("DATA\\GLOBAL\\UI\\PANEL\\HLTHMANA.DC6");
            if (g != null && g[0] >= 2) {
                android.graphics.Bitmap[] orbs = new android.graphics.Bitmap[2];
                for (int i = 0, p = 1; i < 2; i++) {
                    int w = g[p], h = g[p + 1];
                    orbs[i] = android.graphics.Bitmap.createBitmap(g, p + 4, w, w, h, android.graphics.Bitmap.Config.ARGB_8888);
                    p += 4 + w * h;
                }
                view.orbs = orbs;
            }
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
            if (devWalkMonster && frame % 10 == 0 && dir == null) devWalkMonster = walkToNearestMonster();
            int[] me = D2Native.serverPlayer();
            if (me[4] == 17) { // PLRMODE_DEAD: D2 waits for Esc; here the attack button continues
                if (!deadShown) { deadShown = true; runOnUiThread(() -> status.setText("Zginąłeś — naciśnij Atak")); }
                devWalkMonster = false; devWalkLevel = 0;
                if (attackHeld) { attackHeld = false; deadShown = false; D2Native.serverResurrect(); }
            } else
            if (attackHeld && frame % 5 == 0) attackNearest(5);
            if (pickRequested) { pickRequested = false; pickNearest(20); }
            D2Native.serverTick();
            int[] s = D2Native.serverPlayer();
            java.util.List<LevelView.Sprite> list = new java.util.ArrayList<>();
            float hx = s[2] / 65536f, hy = s[3] / 65536f;
            LevelView.Sprite hero = sprite(0, 0, hx, hy, s[5], s[7]);
            if (hero != null) list.add(hero);
            int[] u = D2Native.serverUnits();
            for (int i = 0; i + 12 < u.length; i += 13) {
                if (u[i] == 4) { // items: only those lying on the ground, drawn with the last flippy frame
                    if (Math.abs(u[i + 3] - s[0]) + Math.abs(u[i + 4] - s[1]) > 60) continue;
                    LevelView.Sprite g = groundItem(u[i + 2], u[i + 3] + 0.5f, u[i + 4] + 0.5f);
                    if (g != null) list.add(g);
                    continue;
                }
                // Only units near the hero (a screen and a half).
                if (Math.abs(u[i + 3] - s[0]) + Math.abs(u[i + 4] - s[1]) > 60) continue;
                boolean monster = u[i] == 1, animated = monster || u[i] == 2;
                LevelView.Sprite sp = sprite(u[i], u[i + 2], u[i + 8] / 65536f, u[i + 9] / 65536f,
                        monster ? u[i + 10] : u[i + 6],
                        animated ? clientFrame(u[i] << 24 | u[i + 2], u[i + 5], u[i + 11], u[i + 12]) : u[i + 7]);
                if (sp != null) list.add(sp);
            }
            view.sprites = list.toArray(new LevelView.Sprite[0]);
            view.heroFrame = hero != null ? hero.frame : null;
            view.heroX = hx; view.heroY = hy;
            view.postInvalidateOnAnimation();
            if (frame % 10 == 0) refreshWorld();
            if (frame % 25 == 5) automap.update(act);
            if (frame % 5 == 0) view.vitals = D2Native.serverVitals();
            if (inventoryOpen) { inventory.handleTap(); if (frame % 5 == 0) inventory.update(); }
            if (frame % 5 == 2) belt.update();
        } catch (RuntimeException ex) {
            android.util.Log.e("D2Native", "frame", ex);
        }
    }

    /** Index in serverUnits() of the nearest living hostile monster (not town NPCs 146-179), or -1. */
    private int nearestMonster(int[] u, int[] s, int[] distOut) {
        int best = -1, bestD = Integer.MAX_VALUE;
        for (int i = 0; i + 12 < u.length; i += 13) {
            // Monster modes 0 (death) and 12 (dead).
            if (u[i] != 1 || (u[i + 1] >= 146 && u[i + 1] <= 179) || u[i + 5] == 0 || u[i + 5] == 12) continue;
            if (unreachable.contains(u[i + 2])) continue;
            int d = Math.abs(u[i + 3] - s[0]) + Math.abs(u[i + 4] - s[1]);
            if (d < bestD) { bestD = d; best = i; }
        }
        distOut[0] = bestD;
        return best;
    }

    /** Interacts with (picks up) the nearest item on the ground within reach subtiles. */
    private void pickNearest(int reach) {
        int[] s = D2Native.serverPlayer(), u = D2Native.serverUnits();
        int best = -1, bestD = reach + 1;
        for (int i = 0; i + 12 < u.length; i += 13) {
            if (u[i] != 4) continue;
            int d = Math.abs(u[i + 3] - s[0]) + Math.abs(u[i + 4] - s[1]);
            if (d < bestD && D2Native.groundItemFile(u[i + 2]) != null) { bestD = d; best = i; }
        }
        if (best >= 0) D2Native.serverInteract(4, u[best + 2]);
    }

    /** Left skill (attack) on the nearest monster within reach subtiles. */
    private boolean attackNearest(int reach) {
        int[] s = D2Native.serverPlayer(), u = D2Native.serverUnits(), d = new int[1];
        int i = nearestMonster(u, s, d);
        if (i < 0 || d[0] > reach) return false;
        D2Native.serverAttack(u[i], u[i + 2]);
        return true;
    }

    /** Developer helper: walks to the nearest monster and fights it. */
    private boolean walkToNearestMonster() {
        int[] s = D2Native.serverPlayer(), u = D2Native.serverUnits();
        int[] dist = new int[1];
        int best = nearestMonster(u, s, dist), bestD = dist[0];
        if (best >= 0 && bestD <= 4) { stuckCalls = 0; return attackNearest(4); }
        // Not moving for a few calls (wall in the way of a straight-ish route): try the next point.
        stuckCalls = s[0] == lastX && s[1] == lastY ? stuckCalls + 1 : 0;
        lastX = s[0]; lastY = s[1];
        if (stuckCalls >= 4) {
            stuckCalls = 0;
            if (best >= 0) unreachable.add(u[best + 2]); else exploreIndex++;
            return true;
        }
        if (best >= 0 && D2Native.devWalkToPoint(u[best + 3], u[best + 4], run)) return true;
        // None loaded, or none reachable from here (e.g. across the river): explore the hero's level, visiting points spread over it in turn; a point
        // that cannot be got closer to (river, cliff) is skipped.
        int[] li = D2Native.levelInfo(act, s[6]);
        if (li == null) return false;
        for (int tries = 0; tries < EXPLORE.length; ++tries) {
            float[] f = EXPLORE[exploreIndex % EXPLORE.length];
            int tx = (int) ((li[2] + li[4] * f[0]) * 5), ty = (int) ((li[3] + li[5] * f[1]) * 5);
            if (D2Native.devWalkToPoint(tx, ty, run)) return true;
            exploreIndex++;
        }
        return false;
    }

    private static final float[][] EXPLORE = {{0.5f, 0.5f}, {0.2f, 0.2f}, {0.8f, 0.2f}, {0.8f, 0.8f}, {0.2f, 0.8f}};
    private int exploreIndex, stuckCalls, lastX, lastY;
    /** Monsters the dev helper got stuck walking to (walls between); skipped from then on. */
    private final java.util.Set<Integer> unreachable = new java.util.HashSet<>();

    private final java.util.Map<String, Object[]> flippies = new java.util.HashMap<>();

    /** Native thread: an item on the ground as D2 shows it once it has landed (last flippy frame). */
    private LevelView.Sprite groundItem(int guid, float x, float y) {
        String file = D2Native.groundItemFile(guid);
        if (file == null) return null;
        Object[] f = flippies.get(file);
        if (f == null) {
            int[] d = D2Native.dc6Frames(file);
            f = new Object[]{null, 0, 0};
            if (d != null && d[0] > 0) {
                int n = d[0], p = 1;
                for (int i = 0; i < n - 1; i++) p += 4 + d[p] * d[p + 1];
                int w = d[p], h = d[p + 1];
                if (w > 0 && h > 0) f = new Object[]{android.graphics.Bitmap.createBitmap(d, p + 4, w, w, h, android.graphics.Bitmap.Config.ARGB_8888), d[p + 2] - w / 2, d[p + 3] - h};
            }
            flippies.put(file, f);
        }
        return f[0] == null ? null : new LevelView.Sprite(x, y, (android.graphics.Bitmap) f[0], (int) f[1], (int) f[2]);
    }

    /** Client-side animation per (type << 24 | guid) of monsters and objects: {mode, frame (8.8)}. */
    private final java.util.Map<Integer, int[]> monsterAnims = new java.util.HashMap<>();

    /**
     * Animation frame of a monster or object (torches, fires) as D2Client keeps it: the server sets the mode, rate and length
     * (wAnimSpeed, dwFrameCountPrecise, 8.8 fixed) but does not step walking/idle frames itself;
     * like D2Common's UNITS_StopSequence (#10373) the frame grows by the rate each game frame and wraps.
     */
    private int clientFrame(int key, int mode, int speed, int count) {
        int[] a = monsterAnims.get(key);
        if (a == null || a[0] != mode) { a = new int[]{mode, 0}; monsterAnims.put(key, a); }
        else if (count > 0) {
            a[1] += speed;
            if (mode == 0 && key >>> 24 == 1) a[1] = Math.min(a[1], count - 1); // monster death (DT) plays once, then DD
            else while (a[1] >= count) a[1] -= count;
        }
        return a[1] >> 8;
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
     * Virtual stick: square dead zone per axis (0.3 of the radius, as a gamepad dead zone of
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
