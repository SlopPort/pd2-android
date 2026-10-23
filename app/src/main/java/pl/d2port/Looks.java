package pl.d2port;

import android.graphics.Bitmap;
import java.util.HashMap;
import java.util.Map;
import pl.d2port.d2.D2Native;

/** Decoded COF/DCC animations by graphics key (from D2Native.unitLook) and direction; native thread only. */
final class Looks {
    static final class Anim {
        Bitmap[] frames; int offX, offY, dirs;
    }
    private final Map<String, Anim> cache = new HashMap<>();
    private final Map<String, Integer> dirCount = new HashMap<>();
    private int bytes;
    private static final int MAX_BYTES = 96 * 1024 * 1024;

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
        if (cofDirs == 4) {
            int[] map = {0, 1, 2, 3}; // SW NW NE SE
            return map[((dir64 + 8) / 16 + 3) & 3];
        }
        return 0;
    }

    Anim get(String key, int dir64) {
        if (key == null) return null;
        Integer dirs = dirCount.get(key);
        int cofDir = cofDirection(dir64, dirs == null ? 1 : dirs);
        String k = key + "#" + cofDir;
        Anim a = cache.get(k);
        if (a != null) return a;
        int[] d = D2Native.lookAnimation(key, cofDir);
        a = new Anim();
        if (d != null) {
            if (dirs == null || dirs != d[2]) {
                dirCount.put(key, d[2]);
                if (cofDirection(dir64, d[2]) != cofDir) return get(key, dir64); // now we know the real direction count
            }
            int frames = d[0], w = d[5], h = d[6];
            a.dirs = d[2]; a.offX = d[3]; a.offY = d[4];
            a.frames = new Bitmap[frames];
            for (int f = 0; f < frames; f++) a.frames[f] = Bitmap.createBitmap(d, 7 + w * h * f, w, w, h, Bitmap.Config.ARGB_8888);
            bytes += frames * w * h * 4;
        } else {
            android.util.Log.w("D2Native", "no graphics for " + key + " dir " + cofDir);
        }
        if (bytes > MAX_BYTES) { cache.clear(); bytes = 0; } // simple bound; animations reload on demand
        cache.put(k, a);
        return a;
    }
}
