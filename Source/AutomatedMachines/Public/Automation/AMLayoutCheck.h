#pragma once

#include "CoreMinimal.h"

/**
 * Game update guard. The mod reads the game's classes with the memory layout of the game headers it was
 * built with (SML's starter project). When a game update changes a class the mod reads, those reads land
 * on the wrong data and the game crashes (18 Nov 2026: CL520140, GetFillLevel).
 *
 * At the first call this compares, for every game class and struct the mod reads, its size in our build
 * (sizeof) with its size in the running game (the class's reflection data, filled by the game's own code).
 * Any difference: the mod pauses (no rules, no actions, no tabs, hooks do nothing), logs which classes
 * differ, and tells the player once, until a build made with the matching headers is installed.
 */
namespace AMLayout
{
	/** True if the running game's classes differ from those the mod was built with. Checked once, then cached. */
	AUTOMATEDMACHINES_API bool IsMismatch();

	/** The classes that differ, for the message ("AFGBuildableStorage, UFGInventoryComponent"). */
	AUTOMATEDMACHINES_API FString GetMismatchList();
}
