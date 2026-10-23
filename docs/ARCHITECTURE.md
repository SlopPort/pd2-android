# Architecture

```
┌──────────────────────────── Android app (Java, pl.d2port) ────────────────────────────┐
│ GameActivity: native thread at 25 frames/s, virtual stick, sprites list               │
│ LevelView: isometric tiles + units in depth order        Looks: COF/DCC animation cache│
│ DataImport / data.*: the user's MPQs (installation, PD2 load order)                    │
└───────────────┬───────────────────────────────────────────────────────────────────────┘
                │ JNI (pl.d2port.d2.D2Native  <->  app/src/main/cpp/d2native.cpp)
┌───────────────▼──────────────── libd2native.so (C++, armeabi-v7a) ────────────────────┐
│ sgame.cpp     client side of a game: create/join, commands, read units, unit looks    │
│ loopback.cpp  D2Net replacement: client->server message lists, server->client packets │
│ ─────────────────────────── D2MOO (third_party/D2MOO/source) ──────────────────────── │
│ D2Game  (server: units, AI, spawning, items, quests)   D2Common (tables, DRLG, paths,  │
│                                                         collision, units, composit)   │
│ ─────────────────────── our platform layers (D2MOO ships stubs) ───────────────────── │
│ fog_android.cpp     Fog: memory, txt parser, linkers, asserts, sin/cos tables         │
│ lang_android.cpp    D2Lang: .tbl string tables                                         │
│ cmp_android.cpp     D2CMP: DT1 tile libraries and tile queries                         │
│ storm_android.cpp   Storm/D2Hell archive access (files via the Java MPQ reader)        │
│ server_android.cpp  Storm/Fog/D2Net pieces D2Game needs (no networking)                │
│ dcc.cpp             COF and DCC decoding (unit graphics)                               │
│ compat/             Win32 shim headers (types, critical sections, SAL, intrinsics)     │
└──────────────────────────────────────────────────────────────────────────────────────┘
```

## Why 32-bit only

D2MOO reproduces the original memory layout of 32-bit Windows structures (`#pragma pack(1)`,
4-byte pointers inside game structs, offsets checked against the original). The library is built
for `armeabi-v7a` only; 64-bit phones run it in 32-bit mode. Android 16 shows a "not compatible
with the latest Android" notice for 32-bit-only apps — expected.

## Game data

Files are never bundled. `GameData`/`MpqSource` (Java) read the user's MPQ archives; every native
file request (`ARCHIVE_*` in `storm_android.cpp`) is answered through JNI by `D2Native.readFile`.
The installation order follows PD2: pd2maps > pd2data > pd2assets > pd2monchars > patch_d2 > d2exp >
d2char > d2data (`DataImport.ORDER`). Tables are PD2's compiled `.bin` files; when a `.bin` record
size differs from the 1.10 struct (PD2's `itemstatcost`), the `.txt` is compiled instead.

## How a game runs

1. `D2Native.init` loads all tables (D2Common `DATATBLS_LoadAllTxts`).
2. `startGame`: `loopback_start` initialises D2Game like the single-player client did
   (`D2Game_10046`, `GAME_InitGameDataTable`), then sends system messages as client 1:
   0x66 new game (single player, expansion) and 0x6A join act. D2Game creates the game, the act
   (DRLG) and the hero. A new hero (no `.d2s` save) is created from its class template.
3. Every 40 ms the native thread calls `serverTick` (`GAME_ProcessNetworkMessages` +
   `GAME_UpdateGamesProgress`). The server sets the client "in game" in its own frame; commands
   are only sent after that (earlier ones count as cheating in D2Game).
4. Movement: the stick produces a target subtile; `serverMove` sends packet 0x01 (walk) or 0x03
   (run) every 5 frames, like PD2's controller code.
5. Drawing reads the server state directly (single player, same process): player state, all units
   (`pGame->pUnitList`), unit looks via D2Common's composit code (`D2Common_10884`,
   `D2Common_11069` for monster components), and the tiles of every room the game keeps active —
   rooms of the next level appear as the hero approaches, so level changes are seamless.

Packets from server to client are collected (`loopback_next_packet`) but not used yet; a future
client side (sounds, overhead messages, item updates) can read them.

## Coordinates

Subtiles are the game's units (5 per tile). Screen position of subtile (x, y): `((x - y) * 16,
(x + y) * 8)`; a tile is 160x80 pixels. D2 directions (0-63) start at south (down the screen) and
go clockwise, 8 per octant; DCC/COF files order their directions SW NW NE SE S W N E (8) and then
SSW WSW WNW NNW NNE ENE ESE SSE (16). See `Looks.cofDirection`.
