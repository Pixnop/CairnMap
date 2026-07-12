# Extraction offline des positions de ressources (Palworld 1.0+)

Régénère les `Data/*Locations.json` du mod directement depuis le pak du jeu,
sans tracking in-game. Validé contre les données trackées par miapuffia:
distance médiane 0.0 unité.

## Procédure

1. Lister et extraire les cellules du monde avec [repak](https://github.com/trumank/repak):
   ```
   repak unpack --output world \
     --include 'Pal/Content/Pal/Maps/MainWorld_5/**' \
     ".../Palworld/Pal/Content/Paks/Pal-Windows.pak"
   ```
2. `SCRATCH=$PWD python3 extract_locations.py` (attend `$SCRATCH/world`,
   produit `$SCRATCH/locations-raw.json`): parse chaque cellule World Partition
   (`_Generated_/*.umap`), repère les exports d'acteurs `BP_PalMapObjectSpawner_*`,
   `BP_LevelObject_OilField`, etc., et lit la position (3 doubles) du composant
   racine dans le `.uexp`.
3. `SCRATCH=$PWD python3 gen_data.py`: régénère les JSON dans
   `Content/Mods/MapCollectablesMod/Data/`.
4. `SCRATCH=$PWD python3 validate.py`: contrôle de cohérence contre les
   anciens fichiers (distances au plus proche voisin).

`uecell.py` est le mini-parseur de packages UE (summary/names/imports/exports),
calibré pour les packages cooked UE 5.1 legacy (non IoStore) de Palworld.

Correspondances utiles: spawner `Crystal` = quartz hexolite (rock 0019),
rock 0020 = minerai Sky Island, 0021 = minerai World Tree, 0022 = roche magmatique.
Les points de carte au trésor (`PalTreasureMapPoint`) sont dynamiques: pas de
positions statiques à extraire.
