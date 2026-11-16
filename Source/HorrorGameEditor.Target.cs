using UnrealBuildTool;

public class HorrorGameEditorTarget : TargetRules
{
	public HorrorGameEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("HorrorGame");
	}
}
