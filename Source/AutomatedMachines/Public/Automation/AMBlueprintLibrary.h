#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AMBlueprintLibrary.generated.h"

class AAMAutomationSubsystem;

/**
 * For other mods (docs/MODDING.md, part 2): trigger Automated Machines events from their own code, and
 * get the subsystem to listen to every event triggered (its OnEventTriggered). Needs this mod as a
 * dependency; the naming convention (part 1) does not.
 */
UCLASS()
class AUTOMATEDMACHINES_API UAMBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/**
	 * Triggers an event, as if a machine's condition had triggered it: every machine listening to it
	 * acts. The name is made uppercase like in the game. Source: the building it comes from, or none.
	 * Server only (does nothing on a client), and only once Automated Machines is researched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Automated Machines", meta = (WorldContext = "WorldContext", DisplayName = "AM Trigger Event"))
	static void TriggerEvent(const UObject* WorldContext, const FString& Event, AActor* Source = nullptr);

	/** The Automated Machines subsystem (bind to its OnEventTriggered to hear every event). Null early on. */
	UFUNCTION(BlueprintPure, Category = "Automated Machines", meta = (WorldContext = "WorldContext", DisplayName = "AM Get Automation Subsystem"))
	static AAMAutomationSubsystem* GetAutomationSubsystem(const UObject* WorldContext);

	/** True if an event is active right now (a condition triggering it is met). Server only. */
	UFUNCTION(BlueprintPure, Category = "Automated Machines", meta = (WorldContext = "WorldContext", DisplayName = "AM Is Event Active"))
	static bool IsEventActive(const UObject* WorldContext, const FString& Event);
};
