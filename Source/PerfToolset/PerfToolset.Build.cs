using UnrealBuildTool;

public class PerfToolset : ModuleRules
{
	public PerfToolset(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		IWYUSupport = IWYUSupport.Full;

		PublicDependencyModuleNames.Add("Core");

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"RHI",
			"RenderCore",
			"ToolsetRegistry",
			"UnrealEd",
		});
	}
}
