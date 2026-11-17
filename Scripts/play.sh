#!/bin/bash
# Plays one of the previews in a window.
#
#   Scripts/play.sh            the story: scene one, the ward
#   Scripts/play.sh ward       the same
#   Scripts/play.sh hospital   the abandoned hospital (ready-made map from the Horror Hospital kit)
#   Scripts/play.sh clinic     the night clinic (Gary: only moves when you look away)
#   Scripts/play.sh office     the office (the Librarian: blind, hunts by sound)
#   Scripts/play.sh mine       the mine (the Mouth: charges, cannot turn)
#
# In the game, F1 to F5 switch between them (F5 is the ward). Extra arguments go to the engine.

set -euo pipefail
source "$(dirname "$0")/env.sh"

HOSPITAL_MAP="$PROJECT_DIR/Content/Horror_Hospital/Maps/DemoMap.umap"
VARIANT="${1:-ward}"
shift || true

MAP=""
case "$VARIANT" in
  ward|clinic|office|mine) ;;
  hospital)
    if [ ! -f "$HOSPITAL_MAP" ]; then
      echo "The hospital needs the Horror Hospital kit from Fab in Content/Horror_Hospital (see the README)." >&2
      exit 2
    fi
    # The kit's own map; the game mode is named here because the map may ask for a different one.
    MAP="/Game/Horror_Hospital/Maps/DemoMap?game=/Script/HorrorGame.HGGameMode"
    ;;
  *)
    echo "Unknown variant '$VARIANT'. Use: ward, hospital, clinic, office or mine." >&2
    exit 2
    ;;
esac

if [ ! -f "$PROJECT_DIR/Binaries/Mac/libUnrealEditor-HorrorGame.dylib" ]; then
  "$(dirname "$0")/build.sh"
fi

exec "$UE_EDITOR" "$PROJECT" $MAP -game -Variant="$VARIANT" -windowed -ResX=1600 -ResY=900 -nosplash "$@"
