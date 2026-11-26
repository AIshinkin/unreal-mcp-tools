using UnrealBuildTool;

public class TerrainToolset : ModuleRules
{
	public TerrainToolset(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		IWYUSupport = IWYUSupport.Full;

		PublicDependencyModuleNames.Add("Core");

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"Foliage",
			"Landscape",
			"LandscapeEditor",
			"ToolsetRegistry",
			"UnrealEd",
		});
	}
}
