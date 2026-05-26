using UnrealBuildTool;

public class ChronogyDemo : ModuleRules
{
    public ChronogyDemo(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "GameplayAbilities",
            "GameplayTags",
            "InputCore"
        });

        PrivateDependencyModuleNames.AddRange(new string[] { });
    }
}