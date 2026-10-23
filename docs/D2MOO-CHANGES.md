# Changes to third-party code

D2MOO upstream commit: see `third_party/D2MOO-UPSTREAM.txt`. The full diff of D2MOO is in
`third_party/D2MOO-android.patch` (regenerate it whenever D2MOO files change).

## D2MOO

| File | Change | Why |
| --- | --- | --- |
| `Fog/include/Fog.h`, `D2Common/include/Drlg/D2DrlgDrlg.h`, `D2Game/include/SKILLS/Skills.h`, `D2Game/src/SKILLS/Skills.cpp` | function-pointer fields/params use `D2AnyFnPtr` on Android | MSVC converts function pointers to `void*` implicitly in table initialisers, clang does not |
| `D2Common/src/DataTbls/LevelsTbls.cpp` | automap level types: numeric names, table of 64 instead of 36 | PD2 adds types 36-45; the 1.10 array overflowed into `pLvlMazeTxt` |
| `D2Common/src/DataTbls/DataTbls.cpp` | `.bin` record size check; compile the `.txt` when it differs | PD2's `itemstatcost.bin` uses 336-byte records, 1.10 has 324 |
| `D2Game/src/GAME/Game.cpp` | `va_list` init, two struct-size asserts off, all log levels to logcat, `gnGamesGUIDs` aliases `hGameArray` | clang; squall packs differently (internal only); diagnostics; one array at 0x6FD447F8 in the original |
| `D2Game/src/PLAYER/PlayerList.cpp` | `dword_6FD4DC40` is the variable from `SUnit.cpp` | one original variable, declared twice with different types |
| `D2Game/src/PLAYER/PlrSave.cpp` | without a save file, create a new character from its class template (expansion flag from the game) | the 1.10 client wrote a fresh `.d2s` first; we have no client-side character creation yet |
| `D2Game/src/UNIT/Party.cpp` | assert `!pPartyControl` | inverted check in D2MOO (fails on PC too) |
| `D2Game/src/OBJECTS/ObjRgn.cpp` | object regions stored at `[i]`, not `[0]` | D2MOO put every level's region in slot 0; populating the first non-town room crashed |
| `D2Game/src/MONSTER/MonsterSpawn.cpp` | implemented `D2GAME_SpawnNormalMonster_6FC68E30` and `MONSTERSPAWN_SetupNormalMods` from their decompilation comments | stubs: no monster or NPC was ever created |
| `D2Game/src/UNIT/SUnitInactive.cpp` | `SUNITINACTIVE_CompressUnitIfNeeded` reads the unit type once, before compressing | compressing a ground item frees it; D2MOO then read `dwUnitType` from freed memory (the original kept it in a register) and crashed when the hero left loot behind |

`UNVERIFIED` in `MonsterSpawn.cpp`: the four offsets `unk_6FD28BC8` used when placing SpawnCol=1
monsters (frog demons, Act III) are not in D2MOO and are assumed.

## squall (`third_party/D2MOO/external/squall`, upstream commit in D2MOO-UPSTREAM.txt)

| File | Change | Why |
| --- | --- | --- |
| `storm/list/TSLink.hpp`, `storm/hash/TSHashTable.hpp` | end-of-list test `SquallIsListEnd` (null or odd pointer) instead of `pointer <= 0` | the end marker is `~address`; on Android heap addresses are above 0x80000000 |
| `storm/thread/CCritSect.cpp` | recursive mutex | Win32 critical sections are re-entrant; D2Game relies on it |

## Built but not taken from D2MOO

D2MOO's `Fog.cpp`, `D2LangStubs.cpp`, `D2CMP.cpp` (all stubs), `D2Game/src/GAME/Task.cpp` (Windows
worker threads) and the `DllMain.cpp` files are excluded in `app/src/main/cpp/CMakeLists.txt`; our
`*_android.cpp` files replace them.
