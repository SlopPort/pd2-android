package pl.d2port;

import android.graphics.Bitmap;
import java.util.HashMap;
import java.util.Map;
import pl.d2port.d2.D2Native;

/** Character graphics: composite COF/DCC animations per mode and direction (used on the native thread). */
final class HeroLook {
    /** Player mode numbers (plrmode.txt order) to DCC/COF mode codes. */
    static final String[] MODES = {"DT", "NU", "WL", "RN", "GH", "TN", "TW", "A1", "A2", "BL", "SC", "TH", "KK", "S1", "S2", "S3", "S4", "DD", "GH", "GH"};

    static final class Anim {
        Bitmap[] frames; int offX, offY, dirs;
    }
    private final String token, weaponClass;
    private final String[] armor = new String[16];
    private final Map<String, Anim> cache = new HashMap<>();

    HeroLook(String token, String weaponClass) {
        this.token = token; this.weaponClass = weaponClass;
        // Light armour on head, torso, legs and arms: a new character without items.
        for (int i = 0; i <= 4; i++) armor[i] = "LIT";
    }

    /**
     * COF direction for a D2 64-step direction. D2 directions start at south (down the screen) and
     * go clockwise, 8 steps per octant (PATH_ComputeDirection). DCC/COF files order their
     * directions SW NW NE SE S W N E, then (16-direction files) SSW WSW WNW NNW NNE ENE ESE SSE.
     */
    static int cofDirection(int dir64, int cofDirs) {
        if (cofDirs == 8) {
            int[] map = {4, 0, 5, 1, 6, 2, 7, 3}; // S SW W NW N NE E SE
            return map[((dir64 + 4) / 8) & 7];
        }
        if (cofDirs == 16) {
            int[] map = {4, 8, 0, 9, 5, 10, 1, 11, 6, 12, 2, 13, 7, 14, 3, 15}; // S SSW SW ... SE SSE
            return map[((dir64 + 2) / 4) & 15];
        }
        return 0;
    }

    Anim get(int mode, int dir64) {
        String code = mode >= 0 && mode < MODES.length ? MODES[mode] : "NU";
        Anim probe = cache.get(code + "?");
        int dirs = probe != null ? probe.dirs : 8;
        int cofDir = cofDirection(dir64, dirs);
        String key = code + cofDir;
        Anim a = cache.get(key);
        if (a != null) return a;
        int[] d = D2Native.unitAnimation(token, code, weaponClass, armor, cofDir);
        a = new Anim();
        if (d != null) {
            int frames = d[0], w = d[5], h = d[6];
            a.dirs = d[2]; a.offX = d[3]; a.offY = d[4];
            a.frames = new Bitmap[frames];
            for (int f = 0; f < frames; f++) a.frames[f] = Bitmap.createBitmap(d, 7 + w * h * f, w, w, h, Bitmap.Config.ARGB_8888);
            if (probe == null) { Anim p = new Anim(); p.dirs = a.dirs; cache.put(code + "?", p); }
            if (a.dirs != dirs) return get(mode, dir64); // first load told us the real direction count
        }
        android.util.Log.i("D2Native", "anim " + token + code + weaponClass + " dir " + cofDir + (a.frames == null ? " MISSING" : " frames " + a.frames.length + " of " + a.dirs + " dirs"));
        cache.put(key, a);
        return a;
    }
}
