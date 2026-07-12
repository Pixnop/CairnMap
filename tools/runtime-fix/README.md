# Runtime fix (UE4SS Lua) — Palworld 1.0

Companion Lua mod that makes the 5.1 cooked pak of Map Collectables Helper fully
work on Palworld 1.0 without rebuilding it in the editor. Root cause of the 1.0
breakage: the mod's Blueprint looks up the map's icon canvas by child index, and
1.0 inserted new children into `WBP_Map_Body` (`WBP_SkyIslandCloud`,
`Image_MapMask`, `Canvas_ForIcon_NoMask`, `Canvas_ForIcon_Priority`), so the mod's
icon widgets were created but never added to the visual tree.

## Install

Copy to `Palworld/Mods/NativeMods/UE4SS/Mods/MCDiag/scripts/main.lua` (or any mod
folder name) and add `MCDiag : 1` to `Mods/mods.txt`. Requires the Map Collectables
Helper pak in LogicMods and RE-UE4SS experimental-palworld (1.0 build).

## Keys

- **F7** (map open): attaches all collectable icons to `Canvas_ForIcon_Mask`,
  positioned with the exact world→map transform, filtered by the mod's own
  checkboxes, with stale pre-1.0 resource entries snapped to fresh 1.0 positions
  (embedded, surface-only). Press again after reopening the map or toggling boxes.
- **F8**: prints the delta between the game's player pin and our projection
  (sanity check, expect ~0 px).
- **F6**: `UnlockAllWorldMap` cheat (needs CheatManagerEnabler).

## World → map-canvas projection (Palworld 1.0)

The world map is a plain affine mapping; blind point matching is ambiguous, so the
transform is anchored on exact correspondences at runtime:

1. The 8 boss towers (world positions embedded, extracted from the 1.0 pak) are
   matched to the 8 `WBP_Map_IconTower_C` pins via farthest-pair + 4 orientation
   hypotheses → exact seed (0 px residual).
2. Refined by ICP + trimmed least squares over ~150 `WBP_Map_IconFTTower_C`
   fast-travel statue pins (0.3 px residual).
3. Sanity-anchored on the player pin (offset ~0).

Axis form: slotX = a·worldY + b, slotY = c·worldX + d (plus tiny affine cross
terms fitted by least squares).

## Notes

- Cave/dungeon interiors sit on an instanced grid (deep Z or offshore): all
  embedded location data is filtered to surface nodes (z > -15000).
- Hot-reloading (Ctrl+R) UE4SS Lua while widgets are being torn down can crash
  the game: prefer restarting after script edits.
