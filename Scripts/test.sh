#!/bin/bash
# Checks the maps, then lets the game play each preview by itself with no window.
# The bot fires a weapon (the monster must come and look), collects every fuse and
# leaves through the exit. Prints PASS or FAIL per preview.
#
#   Scripts/test.sh            rules check, no rendering
#   Scripts/test.sh shots      render each preview and save pictures to Saved/Shots/

set -uo pipefail
source "$(dirname "$0")/env.sh"

python3 "$PROJECT_DIR/Scripts/check_maps.py" || exit 1

MODE="${1:-bot}"
FAILED=0
for VARIANT in clinic office mine; do
  LOG="$PROJECT_DIR/Saved/Logs/$MODE-$VARIANT.log"
  if [ "$MODE" = "shots" ]; then
    "$UE_EDITOR" "$PROJECT" -game -HGShots -Variant="$VARIANT" -windowed -ResX=1280 -ResY=720 \
      -nosound -unattended -nosplash -abslog="$LOG" >/dev/null 2>&1
    grep -o "HGTEST SHOT .*" "$LOG" || { echo "$VARIANT: no pictures, see $LOG"; FAILED=1; }
  else
    "$UE_EDITOR" "$PROJECT" -game -HGBot -Variant="$VARIANT" -nullrhi -nosound -unattended -nosplash \
      -abslog="$LOG" >/dev/null 2>&1
    RESULT="$(grep -o "HGTEST RESULT .*" "$LOG" || true)"
    echo "$VARIANT: ${RESULT:-no result, see $LOG}"
    case "$RESULT" in *PASS*) ;; *) FAILED=1 ;; esac
  fi
done
exit $FAILED
