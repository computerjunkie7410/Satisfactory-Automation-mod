// Smart / programmable splitters and priority mergers: their settings (a splitter's sort rules, a
// merger's input priorities) as our saved belt settings, and applying them.
#pragma once

#include "CoreMinimal.h"
#include "Automation/AMTypes.h"

class AFGBuildable;

namespace AMBelt
{
	/** A smart or programmable splitter (one class) or a priority merger. */
	AUTOMATEDMACHINES_API bool IsBelt(const UObject* Object);

	/** A priority merger (else a splitter). */
	AUTOMATEDMACHINES_API bool IsMerger(const UObject* Object);

	/** What the building has now (replicated: works on clients too), for the alarm Event. */
	AUTOMATEDMACHINES_API FAMBeltSetting Read(const AFGBuildable* Machine, const FString& Event);

	/** Applies settings (server only). */
	AUTOMATEDMACHINES_API void Apply(AFGBuildable* Machine, const FAMBeltSetting& Setting);
}
