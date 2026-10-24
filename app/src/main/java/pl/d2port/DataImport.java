package pl.d2port;

import java.io.File;
import pl.d2port.data.InstallationManifest;
import pl.d2port.data.InstallationStore;

/** Adds the user's MPQs to the app's installation and keeps them in the game's load order. */
final class DataImport {
    private DataImport() {}

    /** Diablo II: Lord of Destruction load order: patch over expansion over classic. */
    static final String[] ORDER = {"patch_d2.mpq", "d2exp.mpq", "d2char.mpq", "d2data.mpq"};

    /**
     * Dev import: MPQs copied by adb to Android/data/pl.d2port/files/import are added, then the game's
     * order is applied. Archives that are not part of the game (mods) are searched before it.
     */
    static void importPending(android.app.Activity a, InstallationStore store) throws java.io.IOException {
        File dir = new File(a.getExternalFilesDir(null), "import");
        File[] files = dir.listFiles((d, n) -> n.toLowerCase(java.util.Locale.ROOT).endsWith(".mpq"));
        if (files == null || files.length == 0) return;
        for (File f : files) {
            try (java.io.InputStream in = new java.io.FileInputStream(f)) { store.add(f.getName().toLowerCase(java.util.Locale.ROOT), in); }
            if (!f.delete()) android.util.Log.w("D2Native", "could not delete " + f);
        }
        java.util.List<String> names = new java.util.ArrayList<>();
        java.util.List<String> present = new java.util.ArrayList<>();
        for (InstallationManifest.Archive ar : store.current().archives) present.add(ar.name);
        java.util.List<String> game = new java.util.ArrayList<>();
        for (String n : ORDER) if (present.remove(n)) game.add(n);
        names.addAll(present); // mods first
        names.addAll(game);
        store.reorder(names);
        android.util.Log.i("D2Native", "installation order " + names);
    }

}
