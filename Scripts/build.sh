#!/bin/bash
# Compiles the game's C++ module. Run this after changing anything under Source/.

set -euo pipefail
source "$(dirname "$0")/env.sh"

"$UE_BUILD" HorrorGameEditor Mac Development -project="$PROJECT" -waitmutex
