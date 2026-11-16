#!/bin/bash
# Opens the project in Unreal Editor 5.8 with the built-in MCP server running
# on http://127.0.0.1:8000/mcp, so Claude Code (see .mcp.json) can drive the editor.
#
# Usage: Scripts/open-editor.sh
# Override the engine location with UE_ROOT if yours is not the launcher default.

set -euo pipefail

UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
EDITOR="$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
PROJECT="$(cd "$(dirname "$0")/.." && pwd)/HorrorGame.uproject"

if [ ! -x "$EDITOR" ]; then
  echo "Unreal Editor not found at: $EDITOR" >&2
  echo "Install UE 5.8 from the Epic Games Launcher, or set UE_ROOT." >&2
  exit 1
fi

exec "$EDITOR" "$PROJECT" -ModelContextProtocolStartServer -ModelContextProtocolPort=8000
