using UnrealBuildTool;
using System.IO;
 
public class UDKImportPlugin : ModuleRules
{
    public UDKImportPlugin(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Engine",
				"UnrealEd",
				"CoreUObject",		// @todo Mac: for some reason CoreUObject and Engine are needed to link in debug on Mac
				"InputCore",
				"SlateCore",
				"Slate"
			}
		);
			
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"EditorStyle",
				"Projects",
				"Landscape",
				"LevelEditor",
				"AssetTools",
			}
		);
	}
}