# pd2-android

Diablo II (Lord of Destruction 1.10 rules) / Project Diablo 2 data running natively on Android,
built on the original game logic as reconstructed by [D2MOO](https://github.com/ThePhrozenKeep/D2MOO).
The goal is a 1:1 port in the spirit of DevilutionX: the same server code, the same random rolls,
the same 25 game frames per second, the same animations — not a remake.

> **No game files are included and none will be.** You need your own copy of Diablo II: Lord of
> Destruction (and, for PD2 data, a Project Diablo 2 installation). The app reads your MPQ archives.
> Diablo II is a trademark of Blizzard Entertainment; this project is not affiliated with or
> endorsed by Blizzard or the Project Diablo 2 team.

## What works (October 2026)

- **Data:** D2Common tables (PD2's own `.bin` files), Act I map generation (DRLG), collision.
- **Server:** D2Game, the real game server, runs inside the app. A single-player game is created
  the way the 1.10 client did it, through an in-memory replacement of D2Net (no network).
- **Movement:** the hero walks/runs by sending the original client commands (packets 0x01/0x03);
  speed, pathing and animation come from the game code. Levels are crossed seamlessly (town to
  Blood Moor on foot).
- **Monsters and combat:** monsters are spawned and driven by D2Game's AI. The hero attacks
  (packet 0x06), kills Fallen packs, picks up gold and items (0x13), and dies and revives in town
  (0x41).
- **Graphics:** DT1 tiles, COF/DCC units and DC6 images are decoded by our own code and drawn in
  depth order, with client-side walk/idle animation of monsters and objects.
- **UI:** automap minimap, life/mana globes, items on the ground, an inventory panel (view only),
  a virtual stick modelled on PD2's gamepad scheme. The app's buttons are labelled in Polish.

Not done yet: moving items, belt and skill bar, NPC interaction, quests, sound, saving, Acts II-V.
See [docs/ROADMAP.md](docs/ROADMAP.md).

## Documentation

| File | Contents |
| --- | --- |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | How the pieces fit: D2MOO, our platform layers, the loopback, the app |
| [docs/BUILDING.md](docs/BUILDING.md) | Toolchain, building the APK, getting game data onto a device |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | Test loop on a phone, debugging native crashes, conventions, pitfalls |
| [docs/D2MOO-CHANGES.md](docs/D2MOO-CHANGES.md) | Every change made to D2MOO and squall, and why |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Status, next steps, known gaps and unverified parts |

## Licence

Our code is MIT (see [LICENSE](LICENSE)). D2MOO (`third_party/D2MOO`) is MIT, squall
(`third_party/D2MOO/external/squall`) is public domain; their licence files are kept with them.

## Po polsku (skrót)

Port Diablo II / Project Diablo 2 na Androida oparty na oryginalnej logice gry z D2MOO (serwer
D2Game działa w aplikacji). Repozytorium nie zawiera plików gry — potrzebna jest własna kopia
Diablo II LoD i PD2. Dokumentacja techniczna jest po angielsku w katalogu `docs/`.
