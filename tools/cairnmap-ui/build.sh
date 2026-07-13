#!/usr/bin/env bash
# CairnMap UI pak pipeline (Linux). Usage: UE_ROOT=~/dev/UnrealEngine-5.1.1 ./build.sh
set -euo pipefail
cd "$(dirname "$0")"

UE_ROOT="${UE_ROOT:-$HOME/dev/UnrealEngine-5.1.1}"
EDITOR_CMD="$(find "$UE_ROOT" -maxdepth 4 -name UnrealEditor-Cmd -path '*/Linux/*' | head -1)"
UNREALPAK="$(find "$UE_ROOT" -maxdepth 4 -name UnrealPak -path '*/Linux/*' | head -1)"
PROJECT="$PWD/CairnMapUI.uproject"
[ -x "$EDITOR_CMD" ] || { echo "UnrealEditor-Cmd introuvable sous $UE_ROOT"; exit 1; }

echo "== 1/3 assets (python) =="
"$EDITOR_CMD" "$PROJECT" -run=pythonscript -script="$PWD/scripts/build_panel.py" \
    -unattended -nopause -nosplash -nullrhi -stdout

echo "== 2/3 cook (Windows target) =="
"$EDITOR_CMD" "$PROJECT" -run=cook -targetplatform=${COOK_TARGET:-Linux} -unversioned \
    -unattended -nopause -nosplash -nullrhi -stdout

echo "== 3/3 pak =="
COOKED="$PWD/Saved/Cooked/${COOK_TARGET:-Linux}/CairnMapUI/Content"
STAGE="$PWD/Saved/pakstage"
rm -rf "$STAGE" && mkdir -p "$STAGE"
RESP="$PWD/Saved/pak_response.txt"
: > "$RESP"
while IFS= read -r -d '' f; do
    rel="${f#"$COOKED"/}"
    echo "\"$f\" \"../../../Pal/Content/$rel\"" >> "$RESP"
done < <(find "$COOKED" -type f \( -name '*.uasset' -o -name '*.uexp' -o -name '*.ubulk' \) -print0)
"$UNREALPAK" "$PWD/Saved/CairnMapUI_P.pak" -create="$RESP" -compress
echo "OK: $PWD/Saved/CairnMapUI_P.pak"
echo "Installer dans: Palworld/Pal/Content/Paks/~mods/"
