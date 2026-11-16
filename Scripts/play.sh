#!/bin/bash
# Plays one of the previews in a window.
#
#   Scripts/play.sh            the night clinic (Gary: only moves when you look away)
#   Scripts/play.sh office     the office (the Librarian: blind, hunts by sound)
#   Scripts/play.sh mine       the mine (the Mouth: charges, cannot turn)
#
# In the game, F1/F2/F3 switch between the three. Extra arguments go to the engine.

set -euo pipefail
source "$(dirname "$0")/env.sh"

VARIANT="${1:-clinic}"
shift || true

case "$VARIANT" in
  clinic|office|mine) ;;
  *)
    echo "Unknown variant '$VARIANT'. Use: clinic, office or mine." >&2
    exit 2
    ;;
esac

if [ ! -f "$PROJECT_DIR/Binaries/Mac/UnrealEditor-HorrorGame.dylib" ]; then
  "$(dirname "$0")/build.sh"
fi

exec "$UE_EDITOR" "$PROJECT" -game -Variant="$VARIANT" -windowed -ResX=1600 -ResY=900 -nosplash "$@"
