#include "Automation/AMBelt.h"
#include "AutomatedMachinesModule.h"
#include "Buildables/FGBuildableSplitterSmart.h"
#include "Buildables/FGBuildableMergerPriority.h"

bool AMBelt::IsBelt(const UObject* Object)
{
	return Object && (Object->IsA<AFGBuildableSplitterSmart>() || Object->IsA<AFGBuildableMergerPriority>());
}

bool AMBelt::IsMerger(const UObject* Object)
{
	return Object && Object->IsA<AFGBuildableMergerPriority>();
}

FAMBeltSetting AMBelt::Read(const AFGBuildable* Machine, const FString& Event)
{
	FAMBeltSetting Setting;
	Setting.Event = Event;
	if (const AFGBuildableSplitterSmart* Splitter = Cast<AFGBuildableSplitterSmart>(Machine))
	{
		for (const FSplitterSortRule& Rule : Splitter->GetSortRules())
		{
			Setting.RuleItems.Add(Rule.ItemClass);
			Setting.RuleOutputs.Add(Rule.OutputIndex);
		}
	}
	else if (const AFGBuildableMergerPriority* Merger = Cast<AFGBuildableMergerPriority>(Machine))
	{
		Setting.Priorities = Merger->GetInputPriorities();
	}
	return Setting;
}

void AMBelt::Apply(AFGBuildable* Machine, const FAMBeltSetting& Setting)
{
	if (!Machine || !Machine->HasAuthority())
	{
		return;
	}
	if (AFGBuildableSplitterSmart* Splitter = Cast<AFGBuildableSplitterSmart>(Machine))
	{
		TArray<FSplitterSortRule> Rules;
		const int32 Count = FMath::Min(Setting.RuleItems.Num(), Setting.RuleOutputs.Num());
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Rules.Emplace(Setting.RuleItems[Index], Setting.RuleOutputs[Index]);
		}
		Splitter->SetSortRules(Rules);
	}
	else if (AFGBuildableMergerPriority* Merger = Cast<AFGBuildableMergerPriority>(Machine))
	{
		// One priority per input: the merger's own count, our values first and its own for the rest.
		TArray<int32> Priorities = Merger->GetInputPriorities();
		for (int32 Index = 0; Index < Priorities.Num() && Index < Setting.Priorities.Num(); ++Index)
		{
			Priorities[Index] = Setting.Priorities[Index];
		}
		Merger->SetInputPriorities(Priorities);
	}
}
