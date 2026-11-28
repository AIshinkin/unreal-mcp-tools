using UnrealBuildTool;

public class ContentToolset : ModuleRules
{
	public ContentToolset(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		IWYUSupport = IWYUSupport.Full;

		PublicDependencyModuleNames.Add("Core");

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetRegistry",
			"AssetTools",
			"CoreUObject",
			"EditorScriptingUtilities",
			"Engine",
			"ToolsetRegistry",
			"UnrealEd",
		});
	}
}
