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

	/**
	 * Opens the Automation window for Building on the local player's screen (for buildings with no game
	 * window the tab can be in: docs/MODDING.md). Call it on the player's own game (where the use / key
	 * press happens), not on the server. False if it cannot (no AM_ functions or automation for it, not
	 * researched yet, a dedicated server). Can be called by name without depending on this mod.
	 */
	UFUNCTION(BlueprintCallable, Category = "Automated Machines", meta = (DisplayName = "AM Open Automation Window"))
	static bool OpenAutomationWindow(AActor* Building);

	/** True if an event is active right now (a condition triggering it is met). Server only. */
	UFUNCTION(BlueprintPure, Category = "Automated Machines", meta = (WorldContext = "WorldContext", DisplayName = "AM Is Event Active"))
	static bool IsEventActive(const UObject* WorldContext, const FString& Event);
};
