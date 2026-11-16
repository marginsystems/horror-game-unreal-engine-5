#!/bin/bash
# Regenerates the project's binary assets from the Python scripts next to this file.
# The results are committed, so you only need this after changing one of those scripts.

set -euo pipefail
source "$(dirname "$0")/env.sh"

"$UE_ROOT/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" -run=pythonscript \
  -script="$PROJECT_DIR/Scripts/make_materials.py" -unattended -nosplash -nullrhi
