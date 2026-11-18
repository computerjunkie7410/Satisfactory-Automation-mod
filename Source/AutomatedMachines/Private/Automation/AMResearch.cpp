#include "Automation/AMResearch.h"
#include "AutomatedMachinesModule.h"
#include "Automation/AMLayoutCheck.h"
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
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"
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

	/**
	 * Keeps an object for the whole game. Our classes' defaults (CDOs) are native: the garbage
	 * collector does not look through them, so what only they point to (loaded items, the icon, the
	 * objects we make) would be freed at the next map load (it crashed SML's registration with a
	 * freed cost item after going from the main menu to a save).
	 */
	template <typename T>
	T* AMR_Keep(T* Object)
	{
		if (Object && !Object->IsRooted())
		{
			Object->AddToRoot();
		}
		return Object;
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
		// Any struct with numeric X and Y (FVector2D, FVector2f, FIntPoint, FVector, a blueprint struct...).
		if (!Field || !Value)
		{
			return false;
		}
		bool bX = false;
		bool bY = false;
		for (TFieldIterator<FProperty> It(Field->Struct); It; ++It)
		{
			FNumericProperty* Number = CastField<FNumericProperty>(*It);
			const FString Name = It->GetAuthoredName();
			const bool bIsX = Name == TEXT("X") || Name.StartsWith(TEXT("X_"));
			const bool bIsY = Name == TEXT("Y") || Name.StartsWith(TEXT("Y_"));
			if (!Number || (!bIsX && !bIsY))
			{
				continue;
			}
			void* NumberValue = Number->ContainerPtrToValuePtr<void>(Value);
			const int32 Coordinate = bIsX ? Point.X : Point.Y;
			if (Number->IsFloatingPoint())
			{
				Number->SetFloatingPointPropertyValue(NumberValue, static_cast<double>(Coordinate));
			}
			else
			{
				Number->SetIntPropertyValue(NumberValue, static_cast<int64>(Coordinate));
			}
			bX |= bIsX;
			bY |= bIsY;
		}
		if (!(bX && bY))
		{
			UE_LOG(LogAutomatedMachines, Warning, TEXT("[Research] %s [%s]: no X / Y to set"), *Field->GetAuthoredName(), *Field->Struct->GetName());
		}
		return bX && bY;
	}

	/** Reads a point from any struct with numeric X and Y. */
	FIntPoint AMR_GetPoint(const FStructProperty* Field, const void* Value)
	{
		FIntPoint Point(0, 0);
		for (TFieldIterator<FProperty> It(Field->Struct); It && Value; ++It)
		{
			const FNumericProperty* Number = CastField<FNumericProperty>(*It);
			const FString Name = It->GetAuthoredName();
			if (!Number)
			{
				continue;
			}
			const void* NumberValue = Number->ContainerPtrToValuePtr<void>(Value);
			const int32 Coordinate = Number->IsFloatingPoint() ? FMath::RoundToInt(Number->GetFloatingPointPropertyValue(NumberValue)) : static_cast<int32>(Number->GetSignedIntPropertyValue(NumberValue));
			if (Name == TEXT("X") || Name.StartsWith(TEXT("X_")))
			{
				Point.X = Coordinate;
			}
			else if (Name == TEXT("Y") || Name.StartsWith(TEXT("Y_")))
			{
				Point.Y = Coordinate;
			}
		}
		return Point;
	}

	/** Logs a struct's fields, and those of the structs inside it (to see the game's node layout). */
	void AMR_LogSchema(const UStruct* Struct, const FString& Indent)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			UE_LOG(LogAutomatedMachines, Verbose, TEXT("[Research] schema %s%s [%s]"), *Indent, *It->GetAuthoredName(), *It->GetCPPType());
			const FProperty* Inner = *It;
			if (const FArrayProperty* Array = CastField<FArrayProperty>(Inner))
			{
				Inner = Array->Inner;
				UE_LOG(LogAutomatedMachines, Verbose, TEXT("[Research] schema %s  of [%s]"), *Indent, *Inner->GetCPPType());
			}
			else if (const FMapProperty* Map = CastField<FMapProperty>(Inner))
			{
				UE_LOG(LogAutomatedMachines, Verbose, TEXT("[Research] schema %s  key [%s] value [%s]"), *Indent, *Map->KeyProp->GetCPPType(), *Map->ValueProp->GetCPPType());
				if (const FStructProperty* Value = CastField<FStructProperty>(Map->ValueProp); Value && Indent.Len() < 8)
				{
					AMR_LogSchema(Value->Struct, Indent + TEXT("    "));
				}
				Inner = Map->KeyProp;
			}
			if (const FStructProperty* Nested = CastField<FStructProperty>(Inner); Nested && Indent.Len() < 8)
			{
				AMR_LogSchema(Nested->Struct, Indent + TEXT("  "));
			}
		}
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
			UE_LOG(LogAutomatedMachines, Verbose, TEXT("[Research] %s %s.%s [%s] = %s"), What, *Node->GetClass()->GetName(), *It->GetAuthoredName(), *It->GetCPPType(), *Value.Left(600));
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
				return AMR_Keep(Found);
			}
		}
		TArray<UClass*> Children;
		GetDerivedClasses(Base, Children, true);
		for (UClass* Child : Children)
		{
			if (!Child->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				return AMR_Keep(Child);
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
		UFGUnlock* Unlock = AMR_Keep(NewObject<UFGUnlock>(Outer, UnlockClass));
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
		return AMR_Keep(Item);
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
		TEXT("Machines trigger events when something happens to them (a belt backs up, a storage fills, the power goes), and other machines act when they hear them: pause, resume, change clock speed, switch power, show a message, and more.\n\nEvery machine gets an Automation tab in its window."),
		1, 30.f, 1.f)
}

void UAMSchematic_Automation::PrepareDefaults()
{
	UAMSchematic_Automation* Cdo = GetMutableDefault<UAMSchematic_Automation>();
	UTexture2D* Icon = AMR_Keep(LoadObject<UTexture2D>(nullptr, AMR_Icon));
	AMR_SCHEMATIC_ASSETS(Cdo, AMR_AILimiter, 6, Icon, TEXT("Automation tab"),
		TEXT("Events and actions in the windows of production machines, extractors, generators, storage, power switches, stations, signals, signs, lights and more."))
}

UAMSchematic_Overflow::UAMSchematic_Overflow()
{
	AMR_SCHEMATIC_BASICS(TEXT("Output Overflow"),
		TEXT("Machines can pass the items that would block their input belt on to their output belt (the Output overflow option in the Automation tab), and events can switch it on and off."),
		1, 20.f, 2.f)
}

void UAMSchematic_Overflow::PrepareDefaults()
{
	UAMSchematic_Overflow* Cdo = GetMutableDefault<UAMSchematic_Overflow>();
	UTexture2D* Icon = AMR_Keep(LoadObject<UTexture2D>(nullptr, AMR_Icon));
	AMR_SCHEMATIC_ASSETS(Cdo, AMR_HighSpeedConnector, 2, Icon, TEXT("Output overflow"),
		TEXT("The Output overflow option and the Output overflow on / off actions."))
	// Only after "Automated Machines": it hangs under it in the tree (the MAM's own parent check),
	// plus a "purchased" dependency if the game's (blueprint) class for it is found.
	Cdo->mDependsOnSchematic = UAMSchematic_Automation::StaticClass();
	Cdo->mSchematicDependencies.Reset();
	UClass* DependencyClass = AMR_ConcreteClass(UFGSchematicPurchasedDependency::StaticClass(), {
		TEXT("/Game/FactoryGame/AvailabilityDependencies/BP_SchematicPurchasedDependency.BP_SchematicPurchasedDependency_C") });
	UFGAvailabilityDependency* Dependency = DependencyClass ? AMR_Keep(NewObject<UFGAvailabilityDependency>(Cdo, DependencyClass)) : nullptr;
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
	mPreUnlockDescription = AMR_Text(TEXT("Machines that trigger events and react to them."));
	mPostUnlockDescription = AMR_Text(TEXT("Machines that trigger events and react to them: research it to get an Automation tab in their windows."));
}

bool UAMResearchTree::PrepareDefaults()
{
	UAMResearchTree* Cdo = GetMutableDefault<UAMResearchTree>();
	UTexture2D* Icon = AMR_Keep(LoadObject<UTexture2D>(nullptr, AMR_Icon));
	Cdo->mResearchTreeIcon = AMR_Brush(Icon);

	// The game's node blueprint: from a vanilla tree (logged as a model), or by its path.
	UClass* NodeClass = nullptr;
	// The game's trees: research tree blueprints from the asset registry (found by their parent
	// class), else the guessed paths. Its nodes give the node class, and its root node and that
	// root's first child give where to put ours (centred like the game's) and the road between them.
	TArray<FString> TreePaths;
	{
		TArray<FAssetData> Assets;
		IAssetRegistry::GetChecked().GetAssetsByPath(FName(TEXT("/Game/FactoryGame/Schematics/Research")), Assets, true);
		for (const FAssetData& Asset : Assets)
		{
			const FString Parent = Asset.GetTagValueRef<FString>(FName(TEXT("NativeParentClass"))) + Asset.GetTagValueRef<FString>(FName(TEXT("ParentClass")));
			if (Parent.Contains(TEXT("FGResearchTree")) && !Parent.Contains(TEXT("FGResearchTreeNode")))
			{
				TreePaths.AddUnique(FString::Printf(TEXT("%s.%s_C"), *Asset.PackageName.ToString(), *Asset.AssetName.ToString()));
			}
		}
		UE_LOG(LogAutomatedMachines, Verbose, TEXT("[Research] %d assets in /Game/FactoryGame/Schematics/Research, %d research trees"), Assets.Num(), TreePaths.Num());
	}
	for (const TCHAR* TreePath : AMR_VanillaTrees)
	{
		TreePaths.AddUnique(TreePath);
	}
	TArray<UObject*> VanillaNodes;
	FString VanillaTree;
	const FArrayProperty* NodesProperty = CastField<FArrayProperty>(UFGResearchTree::StaticClass()->FindPropertyByName(TEXT("mNodes")));
	const FObjectPropertyBase* NodesInner = NodesProperty ? CastField<FObjectPropertyBase>(NodesProperty->Inner) : nullptr;
	for (const FString& TreePath : TreePaths)
	{
		UClass* TreeClass = LoadObject<UClass>(nullptr, *TreePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!TreeClass || !TreeClass->IsChildOf(UFGResearchTree::StaticClass()) || !NodesInner)
		{
			continue;
		}
		// Read a vanilla tree's mNodes by reflection (protected, and not through our own type).
		FScriptArrayHelper Nodes(NodesProperty, NodesProperty->ContainerPtrToValuePtr<void>(TreeClass->GetDefaultObject()));
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			if (UObject* Node = NodesInner->GetObjectPropertyValue(Nodes.GetRawPtr(Index)))
			{
				VanillaNodes.Add(Node);
			}
		}
		if (VanillaNodes.Num() > 0)
		{
			VanillaTree = TreeClass->GetName();
			NodeClass = VanillaNodes[0]->GetClass();
			for (int32 Index = 0; Index < VanillaNodes.Num() && Index < 4; ++Index)
			{
				AMR_LogNode(*VanillaTree, VanillaNodes[Index]);
			}
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
	UE_LOG(LogAutomatedMachines, Verbose, TEXT("[Research] node class %s, data %s [%s]"), *NodeClass->GetPathName(), *DataProperty->GetName(), *DataProperty->Struct->GetName());
	AMR_LogSchema(DataProperty->Struct, FString());

	// Node data: Schematic, Coordinates (FIntVector2D), Parents / UnhiddenBy / NodesToUnhide
	// (arrays of coordinates), ChildrenAndRoads (map: a child's coordinates -> the road to it).
	const UStruct* Data = DataProperty->Struct;
	const FObjectPropertyBase* SchematicField = CastField<FObjectPropertyBase>(AMR_Field(Data, TEXT("Schematic")));
	const FStructProperty* CoordinatesField = CastField<FStructProperty>(AMR_Field(Data, TEXT("Coordinates")));
	const FArrayProperty* ParentsField = CastField<FArrayProperty>(AMR_Field(Data, TEXT("Parents")));
	const FMapProperty* ChildrenField = CastField<FMapProperty>(AMR_Field(Data, TEXT("ChildrenAndRoads")));

	// Where the game puts a tree's first node and its first child (else our guess).
	FIntPoint RootAt(3, 0);
	FIntPoint ChildAt(3, 1);
	const void* VanillaRoad = nullptr;
	for (UObject* Node : VanillaNodes)
	{
		if (Node->GetClass() != NodeClass || !CoordinatesField || !ParentsField || !ChildrenField)
		{
			continue;
		}
		const void* NodeData = DataProperty->ContainerPtrToValuePtr<void>(Node);
		FScriptArrayHelper Parents(ParentsField, ParentsField->ContainerPtrToValuePtr<void>(NodeData));
		FScriptMapHelper Children(ChildrenField, ChildrenField->ContainerPtrToValuePtr<void>(NodeData));
		if (Parents.Num() != 0 || Children.Num() == 0)
		{
			continue;
		}
		const FStructProperty* KeyField = CastField<FStructProperty>(ChildrenField->KeyProp);
		for (int32 Index = 0; Index < Children.GetMaxIndex(); ++Index)
		{
			if (Children.IsValidIndex(Index) && KeyField)
			{
				RootAt = AMR_GetPoint(CoordinatesField, CoordinatesField->ContainerPtrToValuePtr<void>(NodeData));
				ChildAt = AMR_GetPoint(KeyField, Children.GetKeyPtr(Index));
				VanillaRoad = Children.GetValuePtr(Index);
				break;
			}
		}
		if (VanillaRoad)
		{
			break;
		}
	}
	UE_LOG(LogAutomatedMachines, Verbose, TEXT("[Research] from %s: root at %d,%d, child at %d,%d, road %s"),
		VanillaTree.IsEmpty() ? TEXT("(no vanilla tree)") : *VanillaTree, RootAt.X, RootAt.Y, ChildAt.X, ChildAt.Y, VanillaRoad ? TEXT("copied") : TEXT("none"));

	AMR_Keep(NodeClass);
	UFGResearchTreeNode* Root = AMR_Keep(NewObject<UFGResearchTreeNode>(Cdo, NodeClass));
	UFGResearchTreeNode* Child = AMR_Keep(NewObject<UFGResearchTreeNode>(Cdo, NodeClass));
	void* RootData = DataProperty->ContainerPtrToValuePtr<void>(Root);
	void* ChildData = DataProperty->ContainerPtrToValuePtr<void>(Child);

	if (SchematicField)
	{
		SchematicField->SetObjectPropertyValue_InContainer(RootData, UAMSchematic_Automation::StaticClass());
		SchematicField->SetObjectPropertyValue_InContainer(ChildData, UAMSchematic_Overflow::StaticClass());
	}
	else
	{
		UE_LOG(LogAutomatedMachines, Error, TEXT("[Research] node data has no Schematic field"));
	}
	if (CoordinatesField)
	{
		AMR_SetPoint(CoordinatesField, CoordinatesField->ContainerPtrToValuePtr<void>(RootData), RootAt);
		AMR_SetPoint(CoordinatesField, CoordinatesField->ContainerPtrToValuePtr<void>(ChildData), ChildAt);
	}
	// The child hangs under the root: the MAM keeps it locked until the root is researched.
	if (const FStructProperty* ParentPoint = ParentsField ? CastField<FStructProperty>(ParentsField->Inner) : nullptr)
	{
		FScriptArrayHelper Parents(ParentsField, ParentsField->ContainerPtrToValuePtr<void>(ChildData));
		AMR_SetPoint(ParentPoint, Parents.GetRawPtr(Parents.AddValue()), RootAt);
	}
	// The road from the root to the child (copied from the game's tree, whose two nodes sit where ours do).
	if (const FStructProperty* KeyField = ChildrenField ? CastField<FStructProperty>(ChildrenField->KeyProp) : nullptr)
	{
		FScriptMapHelper Children(ChildrenField, ChildrenField->ContainerPtrToValuePtr<void>(RootData));
		const int32 Added = Children.AddDefaultValue_Invalid_NeedsRehash();
		AMR_SetPoint(KeyField, Children.GetKeyPtr(Added), ChildAt);
		if (VanillaRoad)
		{
			ChildrenField->ValueProp->CopyCompleteValue(Children.GetValuePtr(Added), VanillaRoad);
		}
		Children.Rehash();
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
	// The game changed since this build: no research tab (its setup writes into the game's classes).
	if (AMLayout::IsMismatch())
	{
		return;
	}
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
	if (AMLayout::IsMismatch())
	{
		return false;
	}
	return AMR_IsPurchased(WorldContext, UAMSchematic_Automation::StaticClass());
}

bool AMResearch::IsOverflowUnlocked(const UObject* WorldContext)
{
	return AMR_IsPurchased(WorldContext, UAMSchematic_Overflow::StaticClass());
}
