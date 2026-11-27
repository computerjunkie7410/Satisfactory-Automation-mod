#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AMAutomationWindow.generated.h"

class UAMAutomationTab;

/**
 * Our own window holding a machine's Automation tab (the card layout), for buildings with no game window
 * the tab can hook into (other mods' buildings: docs/MODDING.md). Opened by UAMBlueprintLibrary::
 * OpenAutomationWindow on the local player; closed by its X, Escape, or the building going away.
 */
UCLASS()
class AUTOMATEDMACHINES_API UAMAutomationWindow : public UUserWidget
{
	GENERATED_BODY()
public:
	/** Opens the window for Machine on Player's screen. Null if it cannot (no automation for it, not researched). */
	static UAMAutomationWindow* Open(APlayerController* Player, AActor* Machine);

	UFUNCTION()
	void Close();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void Build(AActor* Machine);

	UPROPERTY()
	TObjectPtr<UAMAutomationTab> mTab;

	TWeakObjectPtr<AActor> mMachine;
	TWeakObjectPtr<APlayerController> mPlayer;
	bool bClosed = false;
};
