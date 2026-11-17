# Runs when the editor starts. Registers this project's own MCP toolsets.

import unreal

if hasattr(unreal, "MetaHumanCharacterEditorSubsystem"):
    try:
        from horror_metahuman_toolset import HorrorMetaHumanToolset

        unreal.ToolsetRegistry.register_toolset_class(HorrorMetaHumanToolset)
        unreal.log("[HorrorGame] MetaHuman toolset registered")
    except Exception as error:
        unreal.log_warning(f"[HorrorGame] MetaHuman toolset not registered: {error}")
