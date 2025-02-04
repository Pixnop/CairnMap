## ${\textsf{\color{lightgreen}Update to version 4.0.0+ for icon position and lag fixes!!!}}$

## ${\textsf{\color{orange}PLEASE READ IF YOU USED A PREVIOUS VERSION}}$
If you used a previous version of the mod (pre-2.0) you will see a button at the bottom of the interface that removes the old markers. **<ins>You must then wait for an autosave for it to stick.</ins>** You should now no longer have the old custom markers!

# DESCRIPTION

Adds checkboxes to the map to toggle:

- Show effigies
- Include collected effigies
- Show notes
- Include collected notes
- Show active dungeons (only nearby in multiplayer)
- Show nearby chests
- Show nearby eggs
- Show nearby outposts
- Show fruit trees
- Show nearby junk piles
- Show coal nodes
- Show copper nodes
- Show quartz nodes
- Show sulfur nodes
- Show hexolite quartz nodes
- Show oil fields

These are shown as icons on the map

Adds progress counter for effigies and notes

Adds buttons to teleport you to two effigies hidden at the bottom of the map (singleplayer only)

Supports UI settings with the Mod Config Menu (UI) mod also installed

# WORKS ON SINGLEPLAYER AND MULTIPLAYER

Make sure your UE4SS version is **_<ins>AT LEAST 3.0</ins>** to use on multiplayer

${\textsf{\color{orange}Possibly due to anti-cheat, some icons can only be shown if nearby to the player}}$

# INSTALLATION

1. Install UE4SS if it's not already installed. Many mods here have detailed instructions to do so.
2. Make sure in "Pal/Binaries/Win64/Mods/mods.txt" set the line "BPModLoaderMod" to 1. For GamePass, the folder is WinGDK instead of Win64.
3. Put "MapCollectablesMod.pak", "MapCollectablesMod.modconfig.json", and the "MapCollectablesMod.ImageOverrides" folder in "Pal/Content/Paks/LogicMods". If "LogicMods" does not exist, create it.
4. If you experience crashes or lag spikes, in "Pal/Binaries/Win64/UE4SS-settings.ini" set the line "bUseUObjectArrayCache" to false. For GamePass, the folder is WinGDK instead of Win64.

# ISSUES

"Palworld sometimes crashes immediately after launching!" - This just happens sometimes and isn't a bug in the mod. Sorry.

"Palworld sometimes crashes after exiting the game!" - This just happens sometimes and isn't a bug in the mod. Sorry.

"I teleported and somehow got stuck!" - Get the Spectator Mode mod and you'll be able to escape.
