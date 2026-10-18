r"""Extract D2/PD2 game files for the native engine (lower-case paths, '/' separators).

Archives are searched in PD2 load order: PD2 archives first, then the Diablo II ones.
Usage: python tools/d2data_extract.py OUT_DIR name [name ...]   (names like DATA\GLOBAL\AnimData.d2)
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from assets_mpq import MPQ

GAME = Path(__file__).resolve().parents[3]  # D:/gry/Diablo II
PD2 = GAME / 'ProjectD2'
ORDER = [PD2/'pd2maps.mpq', PD2/'pd2data.mpq', PD2/'pd2monchars.mpq', PD2/'pd2assets.mpq', PD2/'patch_d2.mpq',
         GAME/'patch_d2.mpq', GAME/'d2exp.mpq', GAME/'d2data.mpq', GAME/'d2char.mpq']

def main():
    out = Path(sys.argv[1]); names = sys.argv[2:]
    archives = [MPQ(p) for p in ORDER if p.exists()]
    missing = 0
    for name in names:
        key = name.replace('/', '\\')
        for a in archives:
            try: data = a.read(key)
            except (FileNotFoundError, OSError): continue
            dst = out / key.replace('\\', '/').lower()
            dst.parent.mkdir(parents=True, exist_ok=True); dst.write_bytes(data)
            print('ok', name, a.path.name, len(data)); break
        else:
            print('missing', name); missing += 1
    for a in archives: a.close()
    return 1 if missing else 0

if __name__ == '__main__': sys.exit(main())
