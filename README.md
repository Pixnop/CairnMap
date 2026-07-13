<p align="center">
  <img src="docs/logo.png" alt="CairnMap" width="180">
</p>

<h1 align="center">CairnMap</h1>

A map collectables overlay for **Palworld 1.0+**, written as a native C++
[UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) mod. Open the world map and CairnMap
draws every collectable as a coloured dot, with a categorised panel of toggles in
the top-left corner.

It is a from-scratch rewrite inspired by miapuffia's original
[Map Collectables Helper](https://github.com/miapuffia/MapCollectablesMod). No
Blueprint work, no in-game marker spam: everything is rendered in the mod's own
canvas overlay and positioned by a boss-tower-anchored affine projection.

## Features

- **Collectables**
  - **Effigies** (all 407, every relic type including Lamball) — collected ones are hidden automatically
  - **Notes** (all 64) — collected ones are hidden automatically
  - **Eggs (nearby)** — live-detected from loaded actors around you (eggs have no fixed positions in 1.0)
- **Ores** — Coal, Copper, Quartz, Sulfur, Hexolite, Sky Ore, Tree Ore, Magma, Night Stone
- **Resources** — Oil, Dog Coin, Lotus, Fruit Trees
- **Points of interest** — Chests, Outposts, Junk
- A **categorised panel** with a per-layer checkbox; toggle anything on/off live
- **Multiplayer-friendly**: fully client-side, works on dedicated servers (nothing is sent to the server)

## Installation

1. Install [RE-UE4SS](https://github.com/UE4SS-RE/RE-UE4SS/releases) for Palworld.
2. Copy the `CairnMap` folder from the release into your UE4SS `Mods` directory:
   ```
   Palworld/Binaries/Win64/ue4ss/Mods/CairnMap/dlls/main.dll
   ```
   (path may vary with your UE4SS install; drop `CairnMap/` next to the other mods)
3. Enable it by adding this line to `Mods/mods.txt`:
   ```
   CairnMap : 1
   ```
   (an `enabled.txt` is also included as a fallback)
4. Launch the game and open the world map.

## Notes and limitations

- **Effigies / Notes** masking is permanent and per-player (read from your record data), so it is accurate on
  dedicated servers too.
- **Chests** are shown as static spawn points but are **not** collected-masked: field chests respawn on a timer
  and their opened state is server-side world state, not a per-player record, so it cannot be known map-wide from
  a client.
- **Eggs** are placed by a random lottery and respawn, so there is no static list. The "Eggs (nearby)" layer only
  shows eggs currently streamed in around your character; it refreshes each time you reopen the map.

## Building

The Windows DLL is built by GitHub Actions (`.github/workflows/build-cpp-mod.yml`) against the UE4SS v3.0.1 ABI.
The projection core (`cpp-mod/mods/CairnMap/src/cairn_project.hpp`) is pure and unit-tested natively.
Collectable positions are extracted offline from the game pak (`tools/`).

## Credits

- Original mod and the idea: **miapuffia** — [MapCollectablesMod](https://github.com/miapuffia/MapCollectablesMod)
- CairnMap rewrite: **Pixnop**

## License

[MIT](LICENSE).
