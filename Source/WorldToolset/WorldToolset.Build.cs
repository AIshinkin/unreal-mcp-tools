using UnrealBuildTool;

public class WorldToolset : ModuleRules
{
	public WorldToolset(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		IWYUSupport = IWYUSupport.Full;

		PublicDependencyModuleNames.Add("Core");

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"EditorScriptingUtilities",
			"Engine",
			"LevelEditor",
			"ToolsetRegistry",
			"UnrealEd",
		});
	}
}
