// Copyright (c) 2018 Isara Technologies. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class LoliUnreal : ModuleRules
	{
		public LoliUnreal(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					// ... add other public dependencies that you statically link with here ...
				}
				);
			
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CoreUObject",
					"Engine",
					// ... add private dependencies that you statically link with here ...	
				}
				);
			if(Target.Platform == UnrealTargetPlatform.Android)
			{
				string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
				AdditionalPropertiesForReceipt.Add(new ReceiptProperty("AndroidPlugin", Path.Combine(PluginPath, "LoliUnreal_APL.xml")));
			}
		}
	}
}
