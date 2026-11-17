#include "Automation/AMResearch.h"
#include "AutomatedMachinesModule.h"
#include "FGSchematicManager.h"
#include "FGResearchTreeNode.h"
#include "ItemAmount.h"
#include "Resources/FGItemDescriptor.h"
#include "AvailabilityDependencies/FGSchematicPurchasedDependency.h"
#include "Unlocks/FGUnlockInfoOnly.h"
#include "Registry/ModContentRegistry.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"
#include "AvailabilityDependencies/FGAvailabilityDependency.h"

namespace
{
	const FName AMR_ModReference(TEXT("AutomatedMachines"));
	const TCHAR* const AMR_AILimiter = TEXT("/Game/FactoryGame/Resource/Parts/CircuitBoardHighSpeed/Desc_CircuitBoardHighSpeed.Desc_CircuitBoardHighSpeed_C");
	const TCHAR* const AMR_HighSpeedConnector = TEXT("/Game/FactoryGame/Resource/Parts/HighSpeedConnector/Desc_HighSpeedConnector.Desc_HighSpeedConnector_C");
	const TCHAR* const AMR_Icon = TEXT("/AutomatedMachines/Icons/T_AM_TabIcon.T_AM_TabIcon");
	// Vanilla MAM trees: one of them gives us the game's node blueprint class (and a model to log).
	const TCHAR* const AMR_VanillaTrees[] = {
		TEXT("/Game/FactoryGame/Schematics/Research/Research_Caterium.Research_Caterium_C"),
		TEXT("/Game/FactoryGame/Schematics/Research/Research_Quartz.Research_Quartz_C"),
		TEXT("/Game/FactoryGame/Schematics/Research/Research_Sulfur.Research_Sulfur_C"),
		TEXT("/Game/FactoryGame/Schematics/Research/Research_Mycelia.Research_Mycelia_C"),
	};
	const TCHAR* const AMR_NodeClassFallback = TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTreeNode.BPD_ResearchTreeNode_C");

	FText AMR_Text(const TCHAR* Text)
	{
		return FText::FromString(Text);
	}

	/** A field of a (blueprint) struct whose name starts with Prefix (blueprint struct fields end in a GUID). */
	FProperty* AMR_Field(const UStruct* Struct, const TCHAR* Prefix)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (It->GetAuthoredName().StartsWith(Prefix, ESearchCase::IgnoreCase) || It->GetName().StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** Writes a grid position into a FVector2D or FIntPoint field. */
	bool AMR_SetPoint(const FStructProperty* Field, void* Value, const FIntPoint& Point)
	{
		if (!Field)
		{
			return false;
		}
		if (Field->Struct == TBaseStructure<FVector2D>::Get())
		{
			*static_cast<FVector2D*>(Value) = FVector2D(Point.X, Point.Y);
			return true;
		}
		if (Field->Struct == TBaseStructure<FIntPoint>::Get())
		{
			*static_cast<FIntPoint*>(Value) = Point;
			return true;
		}
		return false;
	}

	/** Adds Object to an array-of-objects field of a struct value. */
	bool AMR_AddObject(const UStruct* Struct, void* StructValue, const TCHAR* Prefix, UObject* Object)
	{
		const FArrayProperty* Array = CastField<FArrayProperty>(AMR_Field(Struct, Prefix));
		const FObjectPropertyBase* Inner = Array ? CastField<FObjectPropertyBase>(Array->Inner) : nullptr;
		if (!Inner)
		{
			return false;
		}
		FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(StructValue));
		const int32 Index = Helper.AddValue();
		Inner->SetObjectPropertyValue(Helper.GetRawPtr(Index), Object);
		return true;
	}

	/** Logs every field of a node's data (to see the game's own layout when ours does not show right). */
	void AMR_LogNode(const TCHAR* What, UObject* Node)
	{
		if (!Node)
		{
			return;
		}
		for (TFieldIterator<FProperty> It(Node->GetClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			FString Value;
			It->ExportTextItem_Direct(Value, It->ContainerPtrToValuePtr<void>(Node), nullptr, Node, PPF_None);
			UE_LOG(LogAutomatedMachines, Log, TEXT("[Research] %s %s.%s [%s] = %s"), What, *Node->GetClass()->GetName(), *It->GetAuthoredName(), *It->GetCPPType(), *Value.Left(600));
		}
	}

	/** The data struct of a node blueprint (mNodeDataStruct, else its first struct field). */
	FStructProperty* AMR_NodeData(UClass* NodeClass)
	{
		if (FStructProperty* Named = CastField<FStructProperty>(NodeClass->FindPropertyByName(TEXT("mNodeDataStruct"))))
		{
			return Named;
		}
		for (TFieldIterator<FProperty> It(NodeClass); It; ++It)
		{
			if (FStructProperty* Struct = CastField<FStructProperty>(*It))
			{
				return Struct;
			}
		}
		return nullptr;
	}

	/** Sets a protected FText field (of an unlock) by name. */
	void AMR_SetText(UObject* Object, const TCHAR* Name, const FText& Text)
	{
		if (FTextProperty* Property = CastField<FTextProperty>(Object->GetClass()->FindPropertyByName(Name)))
		{
			Property->SetPropertyValue_InContainer(Object, Text);
		}
	}

	void AMR_SetTexture(UObject* Object, const TCHAR* Name, UTexture2D* Texture)
	{
		if (FObjectPropertyBase* Property = CastField<FObjectPropertyBase>(Object->GetClass()->FindPropertyByName(Name)))
		{
			Property->SetObjectPropertyValue_InContainer(Object, Texture);
		}
	}

	/**
	 * A class we can make objects of: the game's C++ unlock / dependency classes are abstract, their
	 * blueprint children are used. One of the paths, else any loaded non-abstract child, else null.
	 */
	UClass* AMR_ConcreteClass(UClass* Base, std::initializer_list<const TCHAR*> Paths)
	{
		for (const TCHAR* Path : Paths)
		{
			UClass* Found = FindObject<UClass>(nullptr, Path);
			if (!Found)
			{
				Found = LoadObject<UClass>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
			}
			if (Found && Found->IsChildOf(Base) && !Found->HasAnyClassFlags(CLASS_Abstract))
			{
				return Found;
			}
		}
		TArray<UClass*> Children;
		GetDerivedClasses(Base, Children, true);
		for (UClass* Child : Children)
		{
			if (!Child->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				return Child;
			}
		}
		UE_LOG(LogAutomatedMachines, Warning, TEXT("[Research] no usable class for %s: left out"), *Base->GetName());
		return nullptr;
	}

	/** An info-only line in the research's "Unlocks" list (null if the game's class is not found). */
	UFGUnlock* AMR_InfoUnlock(UObject* Outer, const TCHAR* Name, const TCHAR* Description, UTexture2D* Icon)
	{
		UClass* UnlockClass = AMR_ConcreteClass(UFGUnlockInfoOnly::StaticClass(), {
			TEXT("/Game/FactoryGame/Unlocks/BP_UnlockInfoOnly.BP_UnlockInfoOnly_C") });
		if (!UnlockClass)
		{
			return nullptr;
		}
		UFGUnlock* Unlock = NewObject<UFGUnlock>(Outer, UnlockClass);
		AMR_SetText(Unlock, TEXT("mUnlockName"), AMR_Text(Name));
		AMR_SetText(Unlock, TEXT("mUnlockDescription"), AMR_Text(Description));
		AMR_SetTexture(Unlock, TEXT("mUnlockIconBig"), Icon);
		AMR_SetTexture(Unlock, TEXT("mUnlockIconSmall"), Icon);
		AMR_SetTexture(Unlock, TEXT("mUnlockIconCategory"), Icon);
		return Unlock;
	}

	TSubclassOf<UFGItemDescriptor> AMR_Item(const TCHAR* Path)
	{
		UClass* Item = LoadClass<UFGItemDescriptor>(nullptr, Path);
		if (!Item)
		{
			UE_LOG(LogAutomatedMachines, Warning, TEXT("[Research] item not found: %s"), Path);
		}
		return Item;
	}

	FSlateBrush AMR_Brush(UTexture2D* Icon)
	{
		FSlateBrush Brush;
		Brush.SetResourceObject(Icon);
		Brush.ImageSize = FVector2D(256.f, 256.f);
		return Brush;
	}

	bool bAMR_Prepared = false;
}

// --- Schematics ---------------------------------------------------------------------------------

// The constructors set what needs no assets; PrepareDefaults the rest (each class sets its own
// protected fields, through its own type).
#define AMR_SCHEMATIC_BASICS(Name, Description, Tier, Seconds, Priority) \
	mType = ESchematicType::EST_MAM; \
	mDisplayName = AMR_Text(Name); \
	mDescription = AMR_Text(Description); \
	mTechTier = Tier; \
	mTimeToComplete = Seconds; \
	mMenuPriority = Priority;

#define AMR_SCHEMATIC_ASSETS(Cdo, ItemPath, Amount, Icon, UnlockName, UnlockText) \
	Cdo->mCost.Reset(); \
	if (TSubclassOf<UFGItemDescriptor> CostItem = AMR_Item(ItemPath)) \
	{ \
		Cdo->mCost.Add(FItemAmount(CostItem, Amount)); \
	} \
	Cdo->mSchematicIcon = AMR_Brush(Icon); \
	Cdo->mSmallSchematicIcon = Icon; \
	Cdo->mUnlocks.Reset(); \
	if (UFGUnlock* InfoUnlock = AMR_InfoUnlock(Cdo, UnlockName, UnlockText, Icon)) \
	{ \
		Cdo->mUnlocks.Add(InfoUnlock); \
	}

UAMSchematic_Automation::UAMSchematic_Automation()
{
	AMR_SCHEMATIC_BASICS(TEXT("Automated Machines"),
		TEXT("Machines ring alarms when something happens to them (a belt backs up, a storage fills, the power goes), and other machines act when they hear them: pause, resume, change clock speed, switch power, show a message, and more.\n\nEvery machine gets an Automation tab in its window."),
		1, 30.f, 1.f)
}

void UAMSchematic_Automation::PrepareDefaults()
{
	UAMSchematic_Automation* Cdo = GetMutableDefault<UAMSchematic_Automation>();
	UTexture2D* Icon = LoadObject<UTexture2D>(nullptr, AMR_Icon);
	AMR_SCHEMATIC_ASSETS(Cdo, AMR_AILimiter, 6, Icon, TEXT("Automation tab"),
		TEXT("Alarms and actions in the windows of production machines, extractors, generators, storage, power switches, stations, signals, signs, lights and more."))
}

UAMSchematic_Overflow::UAMSchematic_Overflow()
{
	AMR_SCHEMATIC_BASICS(TEXT("Output Overflow"),
		TEXT("Machines can pass the items that would block their input belt on to their output belt (the Output overflow option in the Automation tab), and alarms can switch it on and off."),
		1, 20.f, 2.f)
}

void UAMSchematic_Overflow::PrepareDefaults()
{
	UAMSchematic_Overflow* Cdo = GetMutableDefault<UAMSchematic_Overflow>();
	UTexture2D* Icon = LoadObject<UTexture2D>(nullptr, AMR_Icon);
	AMR_SCHEMATIC_ASSETS(Cdo, AMR_HighSpeedConnector, 2, Icon, TEXT("Output overflow"),
		TEXT("The Output overflow option and the Output overflow on / off actions."))
	// Only after "Automated Machines": it hangs under it in the tree (the MAM's own parent check),
	// plus a "purchased" dependency if the game's (blueprint) class for it is found.
	Cdo->mDependsOnSchematic = UAMSchematic_Automation::StaticClass();
	Cdo->mSchematicDependencies.Reset();
	UClass* DependencyClass = AMR_ConcreteClass(UFGSchematicPurchasedDependency::StaticClass(), {
		TEXT("/Game/FactoryGame/AvailabilityDependencies/BP_SchematicPurchasedDependency.BP_SchematicPurchasedDependency_C") });
	UFGAvailabilityDependency* Dependency = DependencyClass ? NewObject<UFGAvailabilityDependency>(Cdo, DependencyClass) : nullptr;
	if (!Dependency)
	{
		return;
	}
	// Init() is editor-only: its two fields by reflection.
	if (const FArrayProperty* Required = CastField<FArrayProperty>(Dependency->GetClass()->FindPropertyByName(TEXT("mSchematics"))))
	{
		FScriptArrayHelper List(Required, Required->ContainerPtrToValuePtr<void>(Dependency));
		if (const FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(Required->Inner))
		{
			Inner->SetObjectPropertyValue(List.GetRawPtr(List.AddValue()), UAMSchematic_Automation::StaticClass());
		}
	}
	if (FBoolProperty* RequireAll = CastField<FBoolProperty>(Dependency->GetClass()->FindPropertyByName(TEXT("mRequireAllSchematicsToBePurchased"))))
	{
		RequireAll->SetPropertyValue_InContainer(Dependency, true);
	}
	Cdo->mSchematicDependencies.Add(Dependency);
}

// --- Research tree ------------------------------------------------------------------------------

UAMResearchTree::UAMResearchTree()
{
	mPreUnlockDisplayName = AMR_Text(TEXT("Automated Machines"));
	mDisplayName = AMR_Text(TEXT("Automated Machines"));
	mPreUnlockDescription = AMR_Text(TEXT("Machines that ring alarms and react to them."));
	mPostUnlockDescription = AMR_Text(TEXT("Machines that ring alarms and react to them: research it to get an Automation tab in their windows."));
}

bool UAMResearchTree::PrepareDefaults()
{
	UAMResearchTree* Cdo = GetMutableDefault<UAMResearchTree>();
	UTexture2D* Icon = LoadObject<UTexture2D>(nullptr, AMR_Icon);
	Cdo->mResearchTreeIcon = AMR_Brush(Icon);

	// The game's node blueprint: from a vanilla tree (logged as a model), or by its path.
	UClass* NodeClass = nullptr;
	for (const TCHAR* TreePath : AMR_VanillaTrees)
	{
		UClass* TreeClass = LoadClass<UFGResearchTree>(nullptr, TreePath);
		if (!TreeClass)
		{
			continue;
		}
		// Read a vanilla tree's mNodes by reflection (protected, and not through our own type).
		const FArrayProperty* NodesProperty = CastField<FArrayProperty>(UFGResearchTree::StaticClass()->FindPropertyByName(TEXT("mNodes")));
		if (!NodesProperty)
		{
			break;
		}
		FScriptArrayHelper Nodes(NodesProperty, NodesProperty->ContainerPtrToValuePtr<void>(TreeClass->GetDefaultObject()));
		const FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(NodesProperty->Inner);
		for (int32 Index = 0; Inner && Index < Nodes.Num() && Index < 2; ++Index)
		{
			UObject* Node = Inner->GetObjectPropertyValue(Nodes.GetRawPtr(Index));
			if (Node && !NodeClass)
			{
				NodeClass = Node->GetClass();
			}
			AMR_LogNode(*TreeClass->GetName(), Node);
		}
		if (NodeClass)
		{
			break;
		}
	}
	if (!NodeClass)
	{
		NodeClass = LoadClass<UFGResearchTreeNode>(nullptr, AMR_NodeClassFallback);
	}
	if (NodeClass && NodeClass->HasAnyClassFlags(CLASS_Abstract))
	{
		NodeClass = nullptr;
	}
	FStructProperty* DataProperty = NodeClass ? AMR_NodeData(NodeClass) : nullptr;
	if (!DataProperty)
	{
		UE_LOG(LogAutomatedMachines, Error, TEXT("[Research] the game's research node class (or its data) was not found: no MAM tab"));
		return false;
	}
	UE_LOG(LogAutomatedMachines, Log, TEXT("[Research] node class %s, data %s [%s]"), *NodeClass->GetPathName(), *DataProperty->GetName(), *DataProperty->Struct->GetName());

	UFGResearchTreeNode* Root = NewObject<UFGResearchTreeNode>(Cdo, NodeClass);
	UFGResearchTreeNode* Child = NewObject<UFGResearchTreeNode>(Cdo, NodeClass);
	const UStruct* Data = DataProperty->Struct;
	void* RootData = DataProperty->ContainerPtrToValuePtr<void>(Root);
	void* ChildData = DataProperty->ContainerPtrToValuePtr<void>(Child);
	const FIntPoint RootAt(0, 0);
	const FIntPoint ChildAt(0, 1);

	// Schematic, coordinates.
	if (const FObjectPropertyBase* Schematic = CastField<FObjectPropertyBase>(AMR_Field(Data, TEXT("Schematic"))))
	{
		Schematic->SetObjectPropertyValue_InContainer(RootData, UAMSchematic_Automation::StaticClass());
		Schematic->SetObjectPropertyValue_InContainer(ChildData, UAMSchematic_Overflow::StaticClass());
	}
	else
	{
		UE_LOG(LogAutomatedMachines, Error, TEXT("[Research] node data has no Schematic field"));
	}
	if (const FStructProperty* Coordinates = CastField<FStructProperty>(AMR_Field(Data, TEXT("Coordinates"))))
	{
		AMR_SetPoint(Coordinates, Coordinates->ContainerPtrToValuePtr<void>(RootData), RootAt);
		AMR_SetPoint(Coordinates, Coordinates->ContainerPtrToValuePtr<void>(ChildData), ChildAt);
	}
	// The child hangs under the root.
	AMR_AddObject(Data, ChildData, TEXT("Parents"), Root);
	// The road from the root to the child (a list of children, each with roads made of points).
	if (const FArrayProperty* Children = CastField<FArrayProperty>(AMR_Field(Data, TEXT("ChildrenAndRoads"))))
	{
		if (const FStructProperty* Entry = CastField<FStructProperty>(Children->Inner))
		{
			FScriptArrayHelper ChildList(Children, Children->ContainerPtrToValuePtr<void>(RootData));
			void* EntryValue = ChildList.GetRawPtr(ChildList.AddValue());
			if (const FObjectPropertyBase* ChildField = CastField<FObjectPropertyBase>(AMR_Field(Entry->Struct, TEXT("Child"))))
			{
				ChildField->SetObjectPropertyValue_InContainer(EntryValue, Child);
			}
			if (const FArrayProperty* Roads = CastField<FArrayProperty>(AMR_Field(Entry->Struct, TEXT("Road"))))
			{
				FScriptArrayHelper RoadList(Roads, Roads->ContainerPtrToValuePtr<void>(EntryValue));
				const FStructProperty* RoadStruct = CastField<FStructProperty>(Roads->Inner);
				void* RoadValue = RoadList.GetRawPtr(RoadList.AddValue());
				// A road: points (from the root to the child), in its first array of points.
				if (RoadStruct && !AMR_SetPoint(RoadStruct, RoadValue, RootAt))
				{
					for (TFieldIterator<FProperty> It(RoadStruct->Struct); It; ++It)
					{
						const FArrayProperty* Points = CastField<FArrayProperty>(*It);
						const FStructProperty* Point = Points ? CastField<FStructProperty>(Points->Inner) : nullptr;
						if (Point)
						{
							FScriptArrayHelper PointList(Points, Points->ContainerPtrToValuePtr<void>(RoadValue));
							AMR_SetPoint(Point, PointList.GetRawPtr(PointList.AddValue()), RootAt);
							AMR_SetPoint(Point, PointList.GetRawPtr(PointList.AddValue()), ChildAt);
							break;
						}
					}
				}
				else if (RoadStruct)
				{
					// The road is a list of points itself: root, child.
					AMR_SetPoint(RoadStruct, RoadList.GetRawPtr(RoadList.AddValue()), ChildAt);
				}
			}
		}
	}

	Cdo->mNodes.Reset();
	Cdo->mNodes.Add(Root);
	Cdo->mNodes.Add(Child);
	AMR_LogNode(TEXT("ours"), Root);
	AMR_LogNode(TEXT("ours"), Child);
	return true;
}

// --- Registration and checks --------------------------------------------------------------------

void AMResearch::Register(UWorld* World)
{
	if (!bAMR_Prepared)
	{
		bAMR_Prepared = true;
		UAMSchematic_Automation::PrepareDefaults();
		UAMSchematic_Overflow::PrepareDefaults();
		if (!UAMResearchTree::PrepareDefaults())
		{
			return;
		}
	}
	UModContentRegistry* Registry = UModContentRegistry::Get(World);
	if (!Registry)
	{
		UE_LOG(LogAutomatedMachines, Warning, TEXT("[Research] no content registry in %s"), *World->GetName());
		return;
	}
	Registry->RegisterSchematic(AMR_ModReference, UAMSchematic_Automation::StaticClass());
	Registry->RegisterSchematic(AMR_ModReference, UAMSchematic_Overflow::StaticClass());
	Registry->RegisterResearchTree(AMR_ModReference, UAMResearchTree::StaticClass());
	UE_LOG(LogAutomatedMachines, Log, TEXT("[Research] MAM tab Automated Machines registered"));
}

namespace
{
	bool AMR_IsPurchased(const UObject* WorldContext, TSubclassOf<UFGSchematic> Schematic)
	{
		AFGSchematicManager* Manager = WorldContext ? AFGSchematicManager::Get(const_cast<UObject*>(WorldContext)) : nullptr;
		return Manager && Manager->IsSchematicPurchased(Schematic);
	}
}

bool AMResearch::IsAutomationUnlocked(const UObject* WorldContext)
{
	return AMR_IsPurchased(WorldContext, UAMSchematic_Automation::StaticClass());
}

bool AMResearch::IsOverflowUnlocked(const UObject* WorldContext)
{
	return AMR_IsPurchased(WorldContext, UAMSchematic_Overflow::StaticClass());
}
