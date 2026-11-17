#!/bin/bash
# Fetches the private assets plugin (Fab packs and other files that may not be redistributed
# in this public repo) into Plugins/HorrorPrivate. Needs access to the private repository.
# Without it the game still runs, on its own generated art.

set -euo pipefail
source "$(dirname "$0")/env.sh"

REPO="${HORROR_PRIVATE_REPO:-https://github.com/marginsystems/horror-game-assets.git}"
TARGET="$PROJECT_DIR/Plugins/HorrorPrivate"

if [ -d "$TARGET/.git" ]; then
  git -C "$TARGET" pull --ff-only
else
  mkdir -p "$PROJECT_DIR/Plugins"
  git clone "$REPO" "$TARGET"
fi
# The people are MetaHumans and move with Epic's mannequin clips. Those ship with the engine, not
# with either repository, so copy the three files the game uses out of the engine's own templates.
FROM="$UE_ROOT/Templates/TemplateResources/High/Characters/Content/Mannequins"
TO="$PROJECT_DIR/Content/Characters/Mannequins"
for FILE in Meshes/SK_Mannequin.uasset Anims/Unarmed/MM_Idle.uasset Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd.uasset; do
  if [ -f "$FROM/$FILE" ] && [ ! -f "$TO/$FILE" ]; then
    mkdir -p "$(dirname "$TO/$FILE")"
    cp "$FROM/$FILE" "$TO/$FILE"
  fi
done

# The engine reads the list of enabled plugins from the last build, so build once after fetching.
"$(dirname "$0")/build.sh"
