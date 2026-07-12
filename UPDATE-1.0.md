# Migration Palworld 1.0 (10 juillet 2026)

État de la migration du mod vers Palworld 1.0. Vérifications faites contre le
**vrai jeu 1.0** (buildid Steam 24088745, index du `Pal-Windows.pak` listé avec repak)
et contre le [PalworldModdingKit](https://github.com/localcc/PalworldModdingKit) 1.0
(commit du 11 juillet 2026).

## Déjà fait (cette branche)

- `Source/`, `Config/`, `Plugins/` remplacés par les versions 1.0 du kit
  (moteur inchangé: UE 5.1, pas de migration d'engine).
- `Pal.uproject` pris du kit 1.0, en réinjectant les 2 plugins marketplace du mod:
  `ElectronicNodes` (confort éditeur) et `DcJsonAsset` (requis: import des
  `Data/*.json` en DataAssets, cf. `NewIconChecklist.txt`).
- `ObjectHelper.txt` complété avec les rochers 1.0 (0020 à 0022, identifiés
  en extrayant les blueprints du pak).

## Audit de compatibilité: VERT

- **Classes C++**: 23/23 classes du jeu référencées par le mod existent en 1.0.
- **Fonctions**: sur les 11 fonctions appelées, une seule renommée:
  `UPalLocationManager::RemoveLocalCustomLocation(FGuid)` devient
  `RemoveLocalCustomMarker(FGuid)`. Utilisée uniquement par la feature legacy
  "Remove old markers" (pre-2.0) dans `CollectablesManager`: re-brancher le nœud
  ou supprimer la feature. (`AddLocalCustomLocation` → `AddLocalCustomMarker`
  ne touche que `RelicNoteManagerOld.uasset.bak`, ignorable. `LocationMap` scindé
  en `LocationMapInServer/InLocal/Combined`: non lu par le mod, sans impact.)
- **Blueprints de contenu: 22/22 chemins référencés existent encore dans le pak 1.0**,
  y compris les widgets carte (`WBP_Map_Base`, `WBP_Map_Body`, `WBP_MapFilter_Win`)
  et leurs ancres internes (`WBP_MapFilter_Content_C`, `BP_PalTextBlock_C`).
  Aucun re-pointage de chemin nécessaire.
- **Effigies 1.0**: les nouvelles effigies par Pal (`BP_LevelObject_Relic_FlameBambi`,
  `_GuardianDog`, `_IceCrocodile`, etc.) héritent de `PalLevelObjectRelic`:
  la détection existante par classe les attrape automatiquement.

## Environnement de test local (cette machine, Proton)

Palworld 1.0 installé dans `~/.local/share/Steam/steamapps/common/Palworld` avec
RE-UE4SS build 1.0 (Okaetsu, format Workshop: `Mods/NativeMods/UE4SS/`),
`BPModLoaderMod: 1` actif, LogicMods fonctionnels (YetAnotherMinimap, PalAnalyzer
chargent, cf. `UE4SS.log`). **Dump headers 1.0 dispo localement**:
`Palworld/Mods/NativeMods/UE4SS/CXXHeaderDump/`.

## Données de positions: régénérées offline, plus besoin de tracking in-game

Les `Data/*Locations.json` ont été régénérés en parsant les ~10 000 cellules
World Partition du pak 1.0 (outillage réutilisable dans `tools/extract-locations/`,
validé contre les données trackées par miapuffia: distance médiane 0.0 unité).

- **Mis à jour** (plus complets, positions 1.0 exactes): Coal 1021, Copper 2027,
  Quartz 675, Sulfur 718, Hexolite 635 (= spawner `Crystal`), Oil 185.
- **Nouveaux** (à brancher dans les blueprints): SkyIslandOre 226, WorldTreeOre 80,
  MagmaRock 10, NightStone 271, DogCoin 128, Lotus par stat (HP 182, Attack 213,
  Stamina 189, Weight 118, Workspeed 176), CaveMushroom 1463, Junk 858 (toutes
  zones), TreasureChest 1559 (tous spawners de coffres).

## Nouvelles structures 1.0 à ajouter au mod (feature work)

Inventaire tiré de l'index du pak 1.0. Nouvelles zones: Sky Island, World Tree,
Yakushima, Sakurajima, Dark Island.

Nouveaux minables (mêmes patterns que l'existant):
- `BP_MapObject_DamagableRock0020` = minerai Sky Island (+ spawner `_SkyIslandOre`)
- `BP_MapObject_DamagableRock0021` = minerai World Tree (+ spawner `_WorldTreeOre`)
- `BP_MapObject_DamagableRock0022` = roche magmatique (+ spawner dédié)
- `BP_PalMapObjectSpawner_NightStone`, `_Yakushima_Crystal`, `_PalCrystal_Small`

Nouveaux collectables:
- **DogCoin** (`BP_PalMapObjectSpawner_DogCoin`, item `PickupItem_DogCoin`):
  spawner `PalMapObjectSpawnerSimple`, même classe que l'existant, facile.
- **Fleurs Lotus** (boosts de stats): famille `BP_PalMapObjectSpawner_Lotus_*`
  (HP/Attack/Stamina/Weight/Workspeed, par biome et rareté).
- **Points de carte au trésor**: `BP_LevelObject_TreasureMapPoint`
  (classe C++ `PalTreasureMapPoint`, header présent dans le kit 1.0).
- **Champignons de grotte / Yakushima**: `_CaveMushroom`, `_YakushimaMushroom_01/02`.
- Junk piles et fruits à compétence des nouvelles zones: variantes par biome
  (`_Junk_SkyIsland`, `_Junk_WorldTree`, `_SkillFruits_Sakura`, etc.):
  vérifier si la détection actuelle par classe parente suffit.
- Nouveaux coffres par zone/grade (`_Treasure_Element_*`, `_Grade_*`,
  `_Dungeon_Elixir`): probablement déjà couverts par la détection TreasureBox,
  à confirmer en jeu.

## Reste à faire (éditeur UE 5.1, Windows ou VM)

1. Intégrer Wwise dans `Plugins/` (cf. README du kit), ouvrir le projet.
2. Corriger le nœud `RemoveLocalCustomLocation` dans `CollectablesManager`
   (ou supprimer la feature legacy).
3. Recompiler/repacker en `MapCollectablesMod.pak` (LogicMods): l'état actuel
   devrait déjà fonctionner en 1.0 sur l'ancienne moitié de carte.
4. Feature work: nouveaux icônes/checkboxes pour les structures ci-dessus
   (workflow détaillé dans `NewIconChecklist.txt`).
5. ~~Re-tracker les ressources en jeu~~ Fait offline: réimporter les JSON en
   DataAssets via DcJsonAsset (glisser les nouveaux .json dans Data/, cf. workflow
   `NewIconChecklist.txt`) et brancher `LoadPreloadedData()`.
6. Test local possible sur cette machine (Proton + RE-UE4SS 1.0 déjà en place).

## Références

- RE-UE4SS 1.0: https://github.com/Okaetsu/RE-UE4SS/releases/tag/experimental-palworld
- usmap 1.0 pour FModel: https://www.nexusmods.com/palworld/mods/2854
- Fix non officiel 5.1 (jusqu'à 0.6.9): https://www.nexusmods.com/palworld/mods/2947
- Docs modding: https://pwmodding.wiki
