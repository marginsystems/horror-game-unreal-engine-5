#!/bin/bash
# Regenerates the project's binary assets from the scripts next to this file.
# The results are committed, so you only need this after changing one of those scripts.
#
#   Scripts/make-assets.sh             everything
#   Scripts/make-assets.sh materials   Content/Materials  (make_materials.py)
#   Scripts/make-assets.sh sounds      Content/Audio      (make_sounds.py, import_sounds.py)
#   Scripts/make-assets.sh models      Content/Models     (make_models.py in Blender, import_models.py)
#
# Every run writes its WAV and FBX files and its logs under a new time-stamped name in Saved/,
# so nothing left over from an earlier run can be picked up by mistake.

set -euo pipefail
source "$(dirname "$0")/env.sh"

WHAT="${1:-all}"
STAMP="$(date +%Y%m%d-%H%M%S)"
GENERATED="$PROJECT_DIR/Saved/Generated"
BLENDER="${BLENDER:-/Applications/Blender.app/Contents/MacOS/Blender}"

# run_editor_script <script name> [folder the script reads its source files from]
run_editor_script() {
  local LOG="$PROJECT_DIR/Saved/Logs/assets-$1-$STAMP.log"
  # The editor exits 1 even after a clean run (it counts unrelated engine log errors),
  # so its exit status says nothing. This run's own log decides instead.
  HG_SOURCE="${2:-}" "$UE_ROOT/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" -run=pythonscript \
    -script="$PROJECT_DIR/Scripts/$1.py" -unattended -nosplash -nullrhi -abslog="$LOG" >/dev/null 2>&1 || true
  grep -o "HGASSET .*" "$LOG" || true
  if ! grep -q "Python script executed successfully" "$LOG" || grep -q "HGASSET FAILED" "$LOG"; then
    echo "$1 failed, see $LOG"
    exit 1
  fi
}

case "$WHAT" in all|materials|sounds|models) ;; *) echo "unknown target: $WHAT"; exit 1 ;; esac

if [ "$WHAT" = all ] || [ "$WHAT" = materials ]; then
  run_editor_script make_materials
fi

if [ "$WHAT" = all ] || [ "$WHAT" = sounds ]; then
  AUDIO="$GENERATED/Audio-$STAMP"
  mkdir -p "$AUDIO"
  python3 "$PROJECT_DIR/Scripts/make_sounds.py" "$AUDIO" >/dev/null
  run_editor_script import_sounds "$AUDIO"
fi

if [ "$WHAT" = all ] || [ "$WHAT" = models ]; then
  [ -x "$BLENDER" ] || { echo "Blender not found at $BLENDER (set BLENDER=/path/to/blender)"; exit 1; }
  MODELS="$GENERATED/Models-$STAMP"
  mkdir -p "$MODELS"
  # --python-exit-code makes Blender fail when the script raises; its default is to exit 0.
  "$BLENDER" --background --factory-startup --python-exit-code 1 \
    --python "$PROJECT_DIR/Scripts/make_models.py" -- "$MODELS" >/dev/null
  run_editor_script import_models "$MODELS"
fi
