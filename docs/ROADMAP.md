# Roadmap and status

## Done

- Native build of D2Common + D2Game for Android; own Fog/D2Lang/D2CMP/Storm layers.
- PD2 tables, Act I DRLG, collision, tile rendering.
- D2Game single-player game through the D2Net loopback; hero movement by client packets.
- COF/DCC rendering of hero (with held weapons), objects and monsters/NPCs, depth-sorted.
- Monster spawning (implemented from decompilation), camp NPCs with their AI.
- Drawing all active rooms (seamless level crossing).
- Leaving town on foot into Blood Moor (seamless); developer route helper `route.cpp` and the
  "→ poziom 2 (dev)" button.
- Automap minimap: cells from D2Common (`DATATBLS_GetAutomapCellId`) for rooms the hero has been
  near, drawn with `maximap.dc6` frames (own DC6 decoder). Revealed rooms are remembered natively.

## Next

1. **Automap polish** — D2Client also reveals by line of sight and draws level names, exits and
   unit markers; our minimap reveals whole active rooms and shows only the hero.
2. **Blood Moor** — reached on foot (bridge, seamless); D2Game spawns Fallen packs there. Next:
   see them on screen, their AI reacting to the hero.
3. **Combat** — attack/skill packets (0x05-0x11), hit recovery, death, item drops on the ground
   (draw ground items: DC6 `data/global/items` flippy files).
4. **HUD** — life/mana orbs, belt, skill buttons; read stats from the server unit.
5. **NPCs** — interaction packet 0x13, menus, trade, quests (Den of Evil first).
6. **Client side** — use the server→client packets (sounds, overhead text, state updates).
7. **Saving** — `.d2s` writing (D2Game does it; point `FOG_GetSavePath` at app storage).
8. **Other acts**, Battle.net-free multiplayer later if ever.

## Known gaps / unverified

- Tile order returned by `D2CMP_10088_GetTiles` (`cmp_android.cpp`) is assumed (affects which
  variant a rarity roll picks, not the number of rolls).
- `FOG_ComputeStringCRC16`, `FOG_ComputeChecksum`, `FOG_Encode14BitsToString` are our own
  algorithms (names/checksums, not gameplay).
- Monster SpawnCol=1 placement offsets (see D2MOO-CHANGES.md).
- No lighting/palette shifts, no shadows of units, no overlays (auras, missiles).
- Virtual stick: the target distance for a given tilt is our choice; PD2's exact value was not
  recovered.
