package pl.d2port.d2;

import java.io.FileNotFoundException;
import java.io.IOException;
import pl.d2port.data.GameData;

/**
 * Native D2 core: D2MOO's D2Common (original Diablo II 1.10 game code) compiled for Android,
 * with the game tables. Game files are read through {@link GameData} from the user's MPQs.
 * One act at a time; all calls must come from the same worker thread.
 */
public final class D2Native {
    static { System.loadLibrary("d2native"); }
    private D2Native() {}

    private static GameData data;
    private static final int FILE_LIMIT = 64 * 1024 * 1024;

    /** Called from native code for every game file; null when absent. */
    static byte[] readFile(String path) {
        GameData d = data;
        if (d == null) return null;
        try { return d.read(path, FILE_LIMIT).bytes; }
        catch (FileNotFoundException ex) { return null; }
        catch (IOException ex) { android.util.Log.w("D2Native", "read " + path + ": " + ex.getMessage()); return null; }
    }

    /** Loads all data tables (once per process). */
    public static synchronized boolean init(GameData source) {
        data = source;
        return loadTables();
    }

    private static native boolean loadTables();
    /** Builds the whole act with the original DRLG; returns a handle, 0 on failure. */
    public static native long allocAct(int act, int seed, int difficulty);
    public static native void freeAct(long act);
    /** {drlgType, levelType, posX, posY, width, height, rooms} in tiles, or null. */
    public static native int[] levelInfo(long act, int level);
    /** Rooms of a generated level as {x, y, width, height} tile rectangles, flattened. */
    public static native int[] levelRooms(long act, int level);
    /** Activates the level's rooms; per tile {layer (0 floor, 1 wall, 2 roof), tileX, tileY, orientation, imageKey, flags}. */
    public static native int[] levelTiles(long act, int level);
    /** Loads data/global/palette/act{act+1}/pal.dat for tile decoding. */
    public static native boolean loadPalette(int act);
    /** {width, height, left, top, argb...} relative to the tile anchor, or null. */
    public static native int[] tileImage(int key);
    /** D2 collision flags per subtile over the level rectangle (width*5 per row); 0xFFFF outside rooms. */
    public static native short[] levelCollision(long act, int level);

    /** Creates the player (charstats class 0-6) at the act's town spawn location. */
    public static native boolean spawnPlayer(long act, int charClass);
    public static native void setRun(boolean run);
    /** Walk/run command towards a subtile, as a click in D2; false when no path. */
    public static native boolean moveTo(int x, int y);
    /** One game frame (D2 runs 25 frames per second). */
    public static native void tick();
    /** {subtileX, subtileY, precisionX, precisionY (16.16), animMode, direction (0-63), levelId, frame, frameCount} */
    public static native int[] playerState();
    /**
     * Composite animation (COF + DCC layers) of one COF direction:
     * {frames, animRate, cofDirections, boxX, boxY, width, height, argb frames...}; box relative to the foot point.
     * armor holds the 16 component codes (HD TR LG RA LA RH LH SH S1-S8), e.g. "LIT", null for none.
     */
    public static native int[] unitAnimation(String token, String mode, String weaponClass, String[] armor, int direction);

    // ---- D2Game: the real game server (single player through an in-memory D2Net queue)
    /** New single-player game with a new character (charstats class); returns the act for map drawing, 0 on failure. */
    /** Fixed game seed for the next startGame (-1 = random, as in normal play). */
    public static native void setGameSeed(int seed);
    public static native long startGame(int charClass, int difficulty);
    /** Process client commands and run due server frames (call every 40 ms). */
    public static native void serverTick();
    /** The player has entered the game; commands are accepted. */
    public static native boolean serverReady();
    /** Walk/run command to a subtile (client packet 0x01/0x03). */
    /** Hero {life, max life, mana, max mana}. */
    public static native int[] serverVitals();
    public static native void serverMove(int x, int y, boolean run);
    /** Left skill (attack) on a unit, packet 0x06. */
    public static native void serverAttack(int type, int guid);
    /** After death: revive in town (packet 0x41, the client's Esc). */
    public static native void serverResurrect();
    /** Interact with a unit (pick up, talk, operate), packet 0x13. */
    public static native void serverInteract(int type, int guid);
    public static native long serverAct();
    /** Player state like playerState(). */
    public static native int[] serverPlayer();
    /** Other units: 8 ints each {type, classId, guid, x, y, mode, direction, frame}. */
    public static native int[] serverUnits();
    /** Graphics key of a unit (type 0 + guid 0 = the player). */
    public static native String unitLook(int type, int guid);
    /** items.txt flippy DC6 path of an item lying on the ground, or null. */
    public static native String groundItemFile(int guid);
    /** Animation for a graphics key, same layout as unitAnimation. */
    public static native int[] lookAnimation(String key, int direction);
    /** Tiles of all rooms the game keeps active (any level), layout as levelTiles. */
    public static native int[] activeTiles(long act);
    /** Automap of revealed rooms: {tileX, tileY, cell} per tile (cell = frame of the automap DC6). */
    public static native int[] automapCells(long act);
    /** All frames of a DC6 file (game path): {count, then per frame width, height, offX, offY, argb...}. */
    public static native int[] dc6Frames(String path);
    /** Hero inventory: {int[72] panel layout (inventory.txt, 800x600), String[] item DC6s, int[] per item {page, body location, x, y, w, h, guid, mode}}. */
    public static native Object[] inventory();
    /** Item move packet 0x17/0x18/0x19/0x1A/0x1C/0x1D (see sgame_item_command). */
    public static native void itemCommand(int command, int guid, int a, int b, int c);
    /** Developer helper: one walk command along a route towards a level (false when there or no route). */
    public static native boolean devWalkToLevel(int level, boolean run);
    /** Developer helper: one walk command along a route towards a subtile (false when there or no route). */
    public static native boolean devWalkToPoint(int x, int y, boolean run);
}
