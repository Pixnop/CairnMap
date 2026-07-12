# Migration Palworld 1.0 (10 juillet 2026)

État de la migration du mod vers Palworld 1.0. Squelette de projet déjà synchronisé
sur le [PalworldModdingKit](https://github.com/localcc/PalworldModdingKit) mis à jour
1.0 (commit du 11 juillet 2026, PR #51/#52 d'Okaetsu).

## Déjà fait (cette branche)

- `Source/`, `Config/`, `Plugins/` remplacés par les versions 1.0 du kit
  (moteur inchangé: UE 5.1, pas de migration d'engine à faire).
- `Pal.uproject` pris du kit 1.0, en réinjectant les 2 plugins marketplace du mod:
  `ElectronicNodes` (confort éditeur) et `DcJsonAsset` (requis: import des
  `Data/*.json` en DataAssets, cf. `NewIconChecklist.txt`).

## Casse identifiée (à corriger dans l'éditeur Unreal)

Audit des références du mod croisées avec les headers 1.0:

- **23/23 classes C++ du jeu référencées existent encore** (PalLocationManager,
  PalUtility, PalHUDService, PalLevelObjectRelic/Note/Obtainable, PalLocationPoint*,
  PalPlayerRecordData*, etc.).
- **1 seule fonction appelée par le mod a été renommée**:
  `UPalLocationManager::RemoveLocalCustomLocation(FGuid)` devient
  `RemoveLocalCustomMarker(FGuid)`.
  Appelée uniquement dans `CollectablesManager` par la fonctionnalité legacy
  "Remove old markers" (nettoyage des marqueurs pre-2.0). Deux options:
  re-brancher le nœud Blueprint sur la nouvelle fonction, ou supprimer carrément
  la feature legacy (plus personne ne migre depuis une 1.x de 2024).
  À noter aussi: `AddLocalCustomLocation` devient `AddLocalCustomMarker`
  (utilisé seulement dans `RelicNoteManagerOld.uasset.bak`, ignorable), et
  `LocationMap` est scindé en `LocationMapInServer` / `LocationMapInLocal` /
  `LocationMapCombined` (le mod ne le lit pas directement: pas d'impact).

## À vérifier avec le jeu (FModel, non vérifiable hors ligne)

Les 13 blueprints du jeu référencés par le mod, en particulier:

- Widgets carte: `WBP_Map_Base`, `WBP_Map_Body`, `WBP_MapFilter_Win`
  (risque principal: la carte a doublé en 1.0, l'UI a pu être réorganisée).
- Rochers minerai: `BP_MapObject_DamagableRock0002/0003/0004/0006/0019`
  (copper/quartz/coal/sulfur/hexolite). La nouvelle moitié de carte a
  probablement de nouveaux numéros à ajouter (cf. `ObjectHelper.txt`).
- `BP_LevelObject_OilField`, treasure box, dungeon entrance, PalEgg base,
  spawners, `BP_NPCCampPresetBase`.

Mapping usmap 1.0 pour FModel: https://www.nexusmods.com/palworld/mods/2854
(UE version: GAME_UE5_1).

## Reste à faire (Windows + UE 5.1)

1. Intégrer Wwise manuellement dans `Plugins/` (cf. README du kit), ouvrir le projet.
2. Corriger le nœud `RemoveLocalCustomLocation` dans `CollectablesManager`.
3. Vérifier/re-pointer les 13 refs de contenu contre le dump FModel 1.0.
4. Re-tracker les ressources sur la nouvelle moitié de carte avec l'outillage
   intégré du mod (workflow complet dans `NewIconChecklist.txt`:
   PrepareTrack/DoTrack + export JSON), et régénérer les DataAssets.
5. Ajouter les éventuels nouveaux collectables 1.0 (nouveaux minerais?).
6. Repack en `MapCollectablesMod.pak` dans `LogicMods`, tester avec
   RE-UE4SS experimental-palworld (build du 10 juillet 2026:
   https://github.com/Okaetsu/RE-UE4SS/releases/tag/experimental-palworld)
   et `BPModLoaderMod` activé.

## Références

- Fix non officiel 5.1 (idées de fixes, compatible jusqu'à 0.6.9 seulement):
  https://www.nexusmods.com/palworld/mods/2947
- Docs modding: https://pwmodding.wiki
