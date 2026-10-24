# Roadmap and status

## Done

- **Build:** native D2Common + D2Game for Android; own Fog/D2Lang/D2CMP/Storm layers.
- **World:** PD2 tables, Act I DRLG, collision, tile rendering; all active rooms are drawn, so
  level changes are seamless (town to Blood Moor on foot).
- **Server game:** D2Game single-player game through the D2Net loopback; hero movement by client
  packets 0x01/0x03.
- **Units:** COF/DCC rendering of the hero (with held weapons), objects and monsters/NPCs,
  depth-sorted, at precise positions with client-side walk/idle animation.
- **Monsters:** spawning implemented from decompilation; camp NPCs and Blood Moor Fallen packs
  run their D2Game AI and hit the hero (verified in d2test, seed 777).
- **Combat:** attack with packet 0x06 (left skill on unit); monsters die, corpses stay, loot drops.
- **Items:** ground items drawn from `items.txt` flippy DC6 (last frame); pick-up with packet
  0x13 (gold and items verified in d2test). In the inventory panel a tap takes an item to the
  cursor (0x19, 0x1C) and a second tap places it (0x18), equips or swaps it (0x1A, 0x1D) or drops it
  outside the panel (0x17); checked on the phone. The belt's first row is shown at the bottom;
  a tap drinks the potion (0x26; life 10 -> 55 over 3 s in d2test).
- **Death:** hero death and revival in town with packet 0x41 (d2test with `D2DEATH=1`).
- **HUD:** life/mana globes from `hlthmana.dc6`; inventory panel ("Ekwipunek") with the layout
  from `inventory.txt` (800x600 rows), PD2's panel as `invchar6.dc6` frames 4-7, items from their
  invfile DC6 at grid cells or centred in body boxes.
- **Automap minimap:** cells from D2Common (`DATATBLS_GetAutomapCellId`) for rooms the hero has
  been near, drawn with `maximap.dc6` frames (own DC6 decoder); revealed rooms are remembered.
- **Developer helpers:** route helper `route.cpp` and the "→ poziom 2 (dev)" / "→ potwór (dev)"
  buttons (walk to the next level, to the nearest hostile monster, or explore when none is loaded).

## Next

1. **Items** — full belt rows, item tooltips.
2. **HUD** — the rest of the control panel: skill buttons, stats.
3. **Combat** — other skills and attack packets (0x05-0x11), hit recovery visuals, missiles.
4. **Automap polish** — D2Client also reveals by line of sight and draws level names, exits and
   unit markers; our minimap reveals whole active rooms and shows only the hero.
5. **NPCs** — interaction, menus, trade, quests (Den of Evil first).
6. **Client side** — use the server→client packets (sounds, overhead text, state updates).
7. **Saving** — `.d2s` writing (D2Game does it; point `FOG_GetSavePath` at app storage).
8. **Other acts**; Battle.net-free multiplayer later, if ever.

## Known gaps / unverified

- Tile order returned by `D2CMP_10088_GetTiles` (`cmp_android.cpp`) is assumed (affects which
  variant a rarity roll picks, not the number of rolls).
- `FOG_ComputeStringCRC16`, `FOG_ComputeChecksum`, `FOG_Encode14BitsToString` are our own
  algorithms (names/checksums, not gameplay).
- Monster SpawnCol=1 placement offsets (see D2MOO-CHANGES.md).
- Ground item centring (by half the DC6 width) is not verified against D2Client.
- No lighting/palette shifts, no shadows of units, no overlays (auras, missiles).
- Virtual stick: the target distance for a given tilt is our choice; PD2's exact value was not
  recovered.
