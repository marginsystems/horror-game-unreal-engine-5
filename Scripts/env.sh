#!/bin/bash
# Shared by the other scripts: finds the engine and the project.
# Override the engine location with UE_ROOT if yours is not the launcher default.

UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
UE_EDITOR="$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
UE_BUILD="$UE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$PROJECT_DIR/HorrorGame.uproject"

if [ ! -x "$UE_EDITOR" ]; then
  echo "Unreal Editor not found at: $UE_EDITOR" >&2
  echo "Install UE 5.8 from the Epic Games Launcher, or set UE_ROOT." >&2
  exit 1
fi
