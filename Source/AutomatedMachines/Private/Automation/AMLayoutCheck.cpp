#include "Automation/AMLayoutCheck.h"
#include "AutomatedMachinesModule.h"
#include "Misc/EngineVersion.h"

#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableFactory.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Buildables/FGBuildableResourceExtractor.h"
#include "Buildables/FGBuildableStorage.h"
#include "Buildables/FGCentralStorageContainer.h"
#include "Buildables/FGBuildableGenerator.h"
#include "Buildables/FGBuildableGeneratorFuel.h"
#include "Buildables/FGBuildableWidgetSign.h"
#include "Buildables/FGBuildableCircuitSwitch.h"
#include "Buildables/FGBuildablePriorityPowerSwitch.h"
#include "Buildables/FGBuildablePipeReservoir.h"
#include "Buildables/FGBuildablePipelinePump.h"
#include "Buildables/FGBuildableResourceSink.h"
#include "Buildables/FGBuildableRailroadSignal.h"
#include "Buildables/FGBuildableRailroadStation.h"
#include "Buildables/FGBuildableTrainPlatformCargo.h"
#include "Buildables/FGBuildableDockingStation.h"
#include "Buildables/FGBuildablePowerStorage.h"
#include "Buildables/FGBuildableDroneStation.h"
#include "Buildables/FGBuildableFrackingActivator.h"
#include "Buildables/FGBuildableFrackingExtractor.h"
#include "Buildables/FGBuildableLightSource.h"
#include "Buildables/FGBuildableLightsControlPanel.h"
#include "Buildables/FGBuildableSplitterSmart.h"
#include "Buildables/FGBuildableMergerPriority.h"
#include "Buildables/FGBuildableTradingPost.h"
#include "Buildables/FGBuildableMAM.h"
#include "FGBuildablePowerBooster.h"
#include "FGDroneStationInfo.h"
#include "FGTrainStationIdentifier.h"
#include "FGInventoryComponent.h"
#include "FGPowerInfoComponent.h"
#include "FGPowerCircuit.h"
#include "FGPowerConnectionComponent.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGCircuitSubsystem.h"
#include "FGPipeSubsystem.h"
#include "FGCentralStorageSubsystem.h"
#include "FGResourceSinkSubsystem.h"
#include "FGRecipeManager.h"
#include "FGSchematicManager.h"
#include "FGResearchManager.h"
#include "FGBuildableSubsystem.h"
#include "FGSchematic.h"
#include "FGRecipe.h"
#include "FGResearchTree.h"
#include "FGResearchTreeNode.h"
#include "FGTextRenderer.h"
#include "FGTrain.h"
#include "FGRailroadVehicle.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGPlayerState.h"
#include "FGHUD.h"
#include "UI/FGGameUI.h"
#include "Equipment/FGBuildGunBuild.h"
#include "Resources/FGItemDescriptor.h"
#include "ItemAmount.h"
#include "FGIconLibrary.h"
#include "FGIconDatabaseSubsystem.h"
#include "WheeledVehicles/FGWheeledVehicle.h"
#include "WheeledVehicles/FGWheeledVehicleIdentifier.h"
#include "WheeledVehicles/FGVehicleSubsystem.h"

namespace
{
	struct FAMLayoutEntry
	{
		const TCHAR* Name;
		int32 BuiltSize;   // sizeof in this build (our headers)
		int32 GameSize;    // size in the running game (its reflection data)
	};

#define AML_CLASS(Type)  { TEXT(#Type), static_cast<int32>(sizeof(Type)), Type::StaticClass()->GetPropertiesSize() }
#define AML_STRUCT(Type) { TEXT(#Type), static_cast<int32>(sizeof(Type)), Type::StaticStruct()->GetStructureSize() }

	bool GAMLChecked = false;
	bool GAMLMismatch = false;
	FString GAMLList;

	void AML_Run()
	{
		GAMLChecked = true;
		const FAMLayoutEntry Entries[] =
		{
			// Buildings the mod automates or reads.
			AML_CLASS(AFGBuildable),
			AML_CLASS(AFGBuildableFactory),
			AML_CLASS(AFGBuildableManufacturer),
			AML_CLASS(AFGBuildableResourceExtractor),
			AML_CLASS(AFGBuildableStorage),
			AML_CLASS(AFGCentralStorageContainer),
			AML_CLASS(AFGBuildableGenerator),
			AML_CLASS(AFGBuildableGeneratorFuel),
			AML_CLASS(AFGBuildableWidgetSign),
			AML_CLASS(AFGBuildableCircuitSwitch),
			AML_CLASS(AFGBuildablePriorityPowerSwitch),
			AML_CLASS(AFGBuildablePipeReservoir),
			AML_CLASS(AFGBuildablePipelinePump),
			AML_CLASS(AFGBuildableResourceSink),
			AML_CLASS(AFGBuildableRailroadSignal),
			AML_CLASS(AFGBuildableRailroadStation),
			AML_CLASS(AFGBuildableTrainPlatformCargo),
			AML_CLASS(AFGBuildableDockingStation),
			AML_CLASS(AFGBuildablePowerStorage),
			AML_CLASS(AFGBuildableDroneStation),
			AML_CLASS(AFGBuildableFrackingActivator),
			AML_CLASS(AFGBuildableFrackingExtractor),
			AML_CLASS(AFGBuildableLightSource),
			AML_CLASS(AFGBuildableLightsControlPanel),
			AML_CLASS(AFGBuildableSplitterSmart),
			AML_CLASS(AFGBuildableMergerPriority),
			AML_CLASS(AFGBuildableTradingPost),
			AML_CLASS(AFGBuildableMAM),
			AML_CLASS(AFGBuildablePowerBooster),
			AML_CLASS(AFGDroneStationInfo),
			AML_CLASS(AFGTrainStationIdentifier),
			// Components and data they hold.
			AML_CLASS(UFGInventoryComponent),
			AML_STRUCT(FInventoryStack),
			AML_STRUCT(FInventoryItem),
			AML_STRUCT(FItemAmount),
			AML_STRUCT(FIconData),
			AML_CLASS(AFGIconDatabaseSubsystem),
			AML_CLASS(AFGWheeledVehicle),
			AML_CLASS(AFGWheeledVehicleIdentifier),
			AML_CLASS(AFGVehicleSubsystem),
			AML_CLASS(UFGPowerInfoComponent),
			AML_CLASS(UFGPowerCircuit),
			AML_CLASS(UFGPowerConnectionComponent),
			AML_CLASS(UFGFactoryConnectionComponent),
			AML_CLASS(UFGPipeConnectionComponent),
			AML_CLASS(UFGTextRenderer),
			// Subsystems and managers.
			AML_CLASS(AFGCircuitSubsystem),
			AML_CLASS(AFGPipeSubsystem),
			AML_CLASS(AFGCentralStorageSubsystem),
			AML_CLASS(AFGResourceSinkSubsystem),
			AML_CLASS(AFGRecipeManager),
			AML_CLASS(AFGSchematicManager),
			AML_CLASS(AFGResearchManager),
			AML_CLASS(AFGBuildableSubsystem),
			// Research, recipes, items.
			AML_CLASS(UFGSchematic),
			AML_CLASS(UFGRecipe),
			AML_CLASS(UFGResearchTree),
			AML_CLASS(UFGResearchTreeNode),
			AML_CLASS(UFGItemDescriptor),
			// Trains, players, UI.
			AML_CLASS(AFGTrain),
			AML_CLASS(AFGRailroadVehicle),
			AML_CLASS(AFGCharacterPlayer),
			AML_CLASS(AFGPlayerController),
			AML_CLASS(AFGPlayerState),
			AML_CLASS(AFGHUD),
			AML_CLASS(UFGGameUI),
			AML_CLASS(UFGBuildGunStateBuild),
		};

		TArray<FString> Different;
		for (const FAMLayoutEntry& Entry : Entries)
		{
			if (Entry.BuiltSize != Entry.GameSize)
			{
				Different.Add(Entry.Name);
				UE_LOG(LogAutomatedMachines, Warning, TEXT("Game update check: %s is %d bytes in this game, %d in the game this mod was built for"),
					Entry.Name, Entry.GameSize, Entry.BuiltSize);
			}
		}
		GAMLMismatch = Different.Num() > 0;
		GAMLList = FString::Join(Different, TEXT(", "));
		const int32 GameChangelist = static_cast<int32>(FEngineVersion::Current().GetChangelist());
		if (GAMLMismatch)
		{
			UE_LOG(LogAutomatedMachines, Error, TEXT("Game update check: %d of %d game classes changed in this game version (CL%d). Automated Machines is paused until it is updated for this game version."),
				Different.Num(), static_cast<int32>(UE_ARRAY_COUNT(Entries)), GameChangelist);
		}
		else
		{
			UE_LOG(LogAutomatedMachines, Log, TEXT("Game update check: the %d game classes the mod reads match this game version (CL%d)"),
				static_cast<int32>(UE_ARRAY_COUNT(Entries)), GameChangelist);
		}
	}

#undef AML_CLASS
#undef AML_STRUCT
}

bool AMLayout::IsMismatch()
{
	if (!GAMLChecked)
	{
		AML_Run();
	}
	return GAMLMismatch;
}

FString AMLayout::GetMismatchList()
{
	IsMismatch();
	return GAMLList;
}
