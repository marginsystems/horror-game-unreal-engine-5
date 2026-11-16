#!/bin/bash
# Opens the project in Unreal Editor 5.8 with the built-in MCP server running
# on http://127.0.0.1:8000/mcp, so Claude Code (see .mcp.json) can drive the editor.

set -euo pipefail
source "$(dirname "$0")/env.sh"

exec "$UE_EDITOR" "$PROJECT" -ModelContextProtocolStartServer -ModelContextProtocolPort=8000
