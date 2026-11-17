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
# The engine reads the list of enabled plugins from the last build, so build once after fetching.
"$(dirname "$0")/build.sh"
