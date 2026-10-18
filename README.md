# D2 Port

Faithful (1:1) Diablo II / Project Diablo 2 engine for Android, built on the original game code
reconstructed by D2MOO (`third_party/D2MOO`, MIT, 1.10f). Separate from `../android-port`
(the earlier Java remake, kept as a side project); the two projects share no code at build time.

- `app/src/main/cpp` — native core: D2MOO D2Common + own Android Fog/D2Lang/D2CMP/Storm layers
  (`*_android.cpp`, D2MOO ships those only as stubs), JNI bridge `d2native.cpp`, phone test `d2test.cpp`.
- `app/src/main/java/pl/d2port` — app; `data/` reads the user's MPQs (copied from the old project).
- `third_party/D2MOO` — D2MOO with Android patches marked `#ifdef __ANDROID__` (`git -C third_party/D2MOO diff`).
- `tools/` — `d2data_extract.py` (files from MPQ in PD2 order), `d2moo_layout_check.py` (struct offsets).

Game files are never bundled: the user imports their own MPQs. Dev import: copy MPQs to
`/sdcard/Android/data/pl.d2port/files/import/` and start the app; PD2 order is applied
(pd2maps > pd2data > pd2assets > d2exp > d2char > d2data).
