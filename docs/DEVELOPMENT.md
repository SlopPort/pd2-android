# Development

## Principles

- **Faithful first.** Behaviour must come from the game code (D2MOO), not from our own guesses.
  When D2MOO is missing something, implement it from its decompilation comments or the original
  binary and say so in the code. Anything not verified gets an `UNVERIFIED:` comment.
- D2MOO is patched only where needed, marked `#ifdef __ANDROID__` when it differs from the PC
  build, and every change is listed in [D2MOO-CHANGES.md](D2MOO-CHANGES.md).
- Never commit game files (MPQ contents, screenshots of the game).

## Phone test loop

`d2test` runs without the app (see BUILDING.md). Useful environment switches:

| Variable | Effect |
| --- | --- |
| `D2SERVER=1` | full D2Game game: create, enter, walk, list units and camp presets |
| `D2MOVE=1` | player movement through D2Common paths only (no server) |
| `D2LEVEL=n` | writes `collision.pgm` and `tiles.ppm` of level n to `/data/local/tmp` |
| `D2DIRS=1` | prints D2 directions for 8 vectors |
| `D2EXIT=1` (with `D2SERVER`) | walks the hero from the camp into Blood Moor with the route helper (`route.cpp`: BFS over D2 collision of loaded rooms), hunts the nearest monsters and prints combat stats and hero life; after frame 1300 it picks up nearby loot and prints gold and inventory item count. See "Reference seed" below |
| `D2DEATH=1` (with `D2EXIT`) | sets hero life to 1 at the first attack, waits for `PLRMODE_DEAD`, sends packet 0x41 and checks the hero is back in camp with full life |
| `D2TXT=1` | compile tables from `.txt` instead of PD2's `.bin` |
| 3rd argument | directory with reference `.bin` files: compares our compiled tables |

```bash
adb push build-native/d2test build-native/libd2native.so /data/local/tmp/
adb shell "cd /data/local/tmp && chmod 755 d2test && D2SERVER=1 LD_LIBRARY_PATH=. ./d2test /data/local/tmp/d2data 777"
```

Note: `adb push` resets the executable bit — `chmod` every time.

## Native crashes

Logs use tag `D2Native` (`adb logcat -s D2Native`). D2Game's own log lines appear as
`[level] message` (all levels are forwarded on Android). For a crash, symbolize the frames from
logcat with the unstripped library:

```bash
llvm-addr2line -f -C -e build-native/libd2native.so 0x<pc>
```

A hang can be turned into a crash dump: `d2test` with `D2SERVER` aborts itself after 25 s
(`alarm`), which writes a stack to logcat. `debuggerd -b` needs root.

## Pitfalls met so far

- D2MOO ships Fog, D2Lang, D2CMP and parts of D2Game as stubs that return 0/null
  (`D2FUNC_DLL_STUB`, or `return nullptr;` followed by commented decompilation). If a feature
  silently does nothing, look for one.
- Two D2MOO globals sometimes describe the same original address with different types (MSVC
  name mangling hid the clash). On Android make them one variable.
- Code written for 32-bit Windows assumes user pointers are below 0x80000000 (squall's list end
  marker); Android's heap is higher.
- Windows critical sections are re-entrant; Linux mutexes are not unless asked.
- Commands sent before the server marks the client "in game" are treated as cheating and the
  client is dropped.
- PD2 data can overflow fixed 1.10 arrays (automap level types 36-45) and has one table in a newer
  layout (`itemstatcost`). Symptoms: corrupted pointers in `sgptDataTables`.
- On Windows with Git Bash: set `MSYS_NO_PATHCONV=1` for adb device paths, but never in the shell
  that runs `cmd //c gradle.bat` (the build silently does nothing).
- When two phones are attached, always pass `adb -s <serial>`.

## Reference seed

The game seed is the 2nd argument of `d2test`; the test fixes it with `GAME_SetInitSeed`. Seed
**777** is the reference: a Fallen pack near the Blood Moor entrance, hero life 55 → 51 on first
contact. Some seeds (e.g. 1234) never reach the gate: the 90-subtile planning radius of the route
helper is too small for that camp layout.

The app takes the same seed from an intent extra:

```bash
adb -s <device> shell am start -n pl.d2port/.GameActivity --ei seed 777
```

Then "Bieg" (run), "→ poziom 2 (dev)" and "→ potwór (dev)" take the hero to the Fallen packs and
kill them (the helper attacks within 4 subtiles, skips monsters it gets stuck walking to, and
explores otherwise). "Atak" (bottom right), held, attacks the nearest monster within 5 subtiles.

## Asserts

`FOG_DisplayAssert` (`fog_android.cpp`) logs and then traps, so an assert leaves a tombstone whose
stack can be symbolised with `llvm-addr2line` as above (D2 code would `exit(-1)` right after it
anyway).

## Commit messages

Imperative subject of at most 72 characters with an area prefix (`game:`, `render:`, `ui:`,
`d2moo:`, `d2test:`, `docs:`), one change per commit, and a body saying why and how it was checked
(seed, d2test switch, device). A `d2moo:` commit also adds a row to D2MOO-CHANGES.md. Polish UI
words get their English meaning, e.g. "Pick up (Podnieś) button".
