#pragma once

#include "CoreMinimal.h"
#include "FGSchematic.h"
#include "FGResearchTree.h"
#include "AMResearch.generated.h"

/**
 * The mod's MAM research, all in C++ (no editor assets):
 *   tab "Automated Machines"
 *     node "Automated Machines"  (6 AI Limiter)          unlocks everything of the mod
 *       node "Output Overflow"   (2 High-Speed Connector) unlocks the Output overflow option and actions
 * The texts and type are set in the constructors; what needs game assets (the costs' items, the icon,
 * the tree's nodes, which are instances of the game's own node blueprint) is filled in at runtime by
 * AMResearch::Prepare, before the tree is registered with SML's content registry.
 */
UCLASS()
class AUTOMATEDMACHINES_API UAMSchematic_Automation : public UFGSchematic
{
	GENERATED_BODY()
public:
	UAMSchematic_Automation();
	/** Fills what needs loaded assets (cost, icon, dependency). */
	static void PrepareDefaults();
	/** Shared by both schematics. */
	static void PrepareCommon(UFGSchematic* Schematic, const TCHAR* CostItemPath, int32 CostAmount, TSubclassOf<UFGSchematic> DependsOn);
};

UCLASS()
class AUTOMATEDMACHINES_API UAMSchematic_Overflow : public UFGSchematic
{
	GENERATED_BODY()
public:
	UAMSchematic_Overflow();
	static void PrepareDefaults();
};

UCLASS()
class AUTOMATEDMACHINES_API UAMResearchTree : public UFGResearchTree
{
	GENERATED_BODY()
public:
	UAMResearchTree();
	/** Builds the two nodes (instances of the game's node blueprint) and the icon. False if it failed. */
	static bool PrepareDefaults();
};

namespace AMResearch
{
	/** Prepares the defaults (once) and registers the tree with SML. Call when a game world initializes its actors. */
	AUTOMATEDMACHINES_API void Register(UWorld* World);

	/** "Automated Machines" researched: the mod's tabs, side lists and rules work. */
	AUTOMATEDMACHINES_API bool IsAutomationUnlocked(const UObject* WorldContext);

	/** "Output Overflow" researched: the Output overflow option and its actions. */
	AUTOMATEDMACHINES_API bool IsOverflowUnlocked(const UObject* WorldContext);
}
