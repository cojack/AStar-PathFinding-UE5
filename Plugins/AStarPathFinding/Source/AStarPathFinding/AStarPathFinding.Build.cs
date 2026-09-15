using UnrealBuildTool;

public class AStarPathFinding : ModuleRules
{
	public AStarPathFinding(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
