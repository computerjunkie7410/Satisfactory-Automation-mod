#include "Automation/AMBlueprintLibrary.h"
#include "Automation/AMAutomationSubsystem.h"
#include "Automation/AMTypes.h"
#include "UI/AMAutomationWindow.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

void UAMBlueprintLibrary::TriggerEvent(const UObject* WorldContext, const FString& Event, AActor* Source)
{
	AAMAutomationSubsystem* Subsystem = AAMAutomationSubsystem::Get(WorldContext);
	const FString Name = AMRules::NormalizeEventName(Event);
	if (Subsystem && !Name.IsEmpty())
	{
		Subsystem->FireEvent(Name, Source);
	}
}

AAMAutomationSubsystem* UAMBlueprintLibrary::GetAutomationSubsystem(const UObject* WorldContext)
{
	return AAMAutomationSubsystem::Get(WorldContext);
}

bool UAMBlueprintLibrary::IsEventActive(const UObject* WorldContext, const FString& Event)
{
	const AAMAutomationSubsystem* Subsystem = AAMAutomationSubsystem::Get(WorldContext);
	return Subsystem && Subsystem->IsEventActive(AMRules::NormalizeEventName(Event));
}

bool UAMBlueprintLibrary::OpenAutomationWindow(AActor* Building)
{
	UWorld* World = Building ? Building->GetWorld() : nullptr;
	if (!World || IsRunningDedicatedServer())
	{
		return false;
	}
	return UAMAutomationWindow::Open(World->GetFirstPlayerController(), Building) != nullptr;
}
