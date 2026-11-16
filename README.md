# Horror Game (Unreal Engine 5)

An open source comedy-horror game built with Unreal Engine 5.8. The monster is real. Your
weapons are not: the pistol fires a "BANG!" flag, the shotgun fires confetti, and the grenade
is a dud until three minutes after you needed it.

Status: project skeleton. Nothing playable yet.

- [Game design](docs/GAME_DESIGN.md): the pitch, setting options, monsters, broken weapons, build order.
- [Assets](docs/ASSETS.md): free asset shortlist and the licence rule for this repository.

## Requirements

- Unreal Engine 5.8, installed from the Epic Games Launcher
- macOS with Xcode, or Windows with Visual Studio 2022
- [Git LFS](https://git-lfs.com)

## Getting started

```bash
git lfs install
```

```bash
git clone https://github.com/marginsystems/horror-game-unreal-engine-5.git
```

Open `HorrorGame.uproject`, or on macOS run:

```bash
Scripts/open-editor.sh
```

The script opens the project with Unreal's built-in MCP server running.

## Building with an AI agent

Unreal Engine 5.8 ships an experimental MCP plugin (`ModelContextProtocol`). It is enabled in
`HorrorGame.uproject`, and `.mcp.json` points Claude Code at it:

1. Start the editor with `Scripts/open-editor.sh`, or run `ModelContextProtocol.StartServer`
   in the editor console.
2. In **Edit > Plugins**, enable **All Toolsets** and restart the editor. This gives the agent
   the Blueprint, level, actor and asset tools.
3. Start Claude Code from the project root. The `unreal-mcp` server is at
   `http://127.0.0.1:8000/mcp`.

The server listens on localhost only and has no authentication. Do not expose it to a network.

## Licence

Code and original content are under the [MIT License](LICENSE). Third-party assets keep their
own licences; see [docs/ASSETS.md](docs/ASSETS.md).
