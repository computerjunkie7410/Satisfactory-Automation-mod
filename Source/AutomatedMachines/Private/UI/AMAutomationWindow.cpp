#include "UI/AMAutomationWindow.h"
#include "UI/AMAutomationTab.h"
#include "UI/AMUIHelpers.h"
#include "Automation/AMAutomationSubsystem.h"
#include "Automation/AMResearch.h"
#include "Automation/AMLayoutCheck.h"
#include "AutomatedMachinesModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"

namespace
{
	/** The window's size on a 1080p screen (the game's machine windows are about this size). */
	constexpr float AMAW_Width = 1240.f;
	constexpr float AMAW_Height = 760.f;
}

UAMAutomationWindow* UAMAutomationWindow::Open(APlayerController* Player, AActor* Machine)
{
	if (!Player || !Player->IsLocalController() || !Machine || AMLayout::IsMismatch()
		|| !AAMAutomationSubsystem::CanAutomate(Machine) || !AMResearch::IsAutomationUnlocked(Machine))
	{
		return nullptr;
	}
	UAMAutomationWindow* Window = CreateWidget<UAMAutomationWindow>(Player, UAMAutomationWindow::StaticClass());
	if (!Window)
	{
		return nullptr;
	}
	Window->mPlayer = Player;
	Window->Build(Machine);
	Window->AddToViewport(50);
	// The mouse for the window; Escape and the X close it (the game does not move meanwhile).
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Window->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Player->SetInputMode(Mode);
	Player->SetShowMouseCursor(true);
	UE_LOG(LogAutomatedMachines, Log, TEXT("Automation window opened for %s"), *GetNameSafe(Machine));
	return Window;
}

void UAMAutomationWindow::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
}

void UAMAutomationWindow::Build(AActor* Machine)
{
	mMachine = Machine;
	// [ dark backdrop over the screen [ title ............ X ] [ the Automation tab ] ]
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("AMWindowRoot"));
	WidgetTree->RootWidget = Root;
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Backdrop->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.55f));
	if (UOverlaySlot* BackdropSlot = Root->AddChildToOverlay(Backdrop))
	{
		BackdropSlot->SetHorizontalAlignment(HAlign_Fill);
		BackdropSlot->SetVerticalAlignment(VAlign_Fill);
	}
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Size->SetWidthOverride(AMAW_Width);
	Size->SetHeightOverride(AMAW_Height);
	if (UOverlaySlot* SizeSlot = Root->AddChildToOverlay(Size))
	{
		SizeSlot->SetHorizontalAlignment(HAlign_Center);
		SizeSlot->SetVerticalAlignment(VAlign_Center);
	}
	UBorder* Frame = AMUI::MakeRoundedBox(WidgetTree, AMUI::PageBackground, FMargin(16.f, 10.f, 16.f, 16.f), 10.f);
	Size->AddChild(Frame);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Frame->AddChild(Column);

	// Title bar: "AUTOMATION" and the close button.
	UHorizontalBox* TitleBar = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (UVerticalBoxSlot* TitleSlot = Column->AddChildToVerticalBox(TitleBar))
	{
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	}
	FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 16);
	UTextBlock* Title = AMUI::MakeText(WidgetTree, TEXT("AUTOMATION"), TitleFont, AMUI::Orange);
	if (UHorizontalBoxSlot* TitleTextSlot = TitleBar->AddChildToHorizontalBox(Title))
	{
		TitleTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitleTextSlot->SetVerticalAlignment(VAlign_Center);
	}
	UButton* CloseButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	AMUI::SetButtonRounded(CloseButton, AMUI::CardRow, 6.f);
	UTextBlock* CloseText = AMUI::MakeText(WidgetTree, TEXT("X"), FCoreStyle::GetDefaultFontStyle("Bold", 14), AMUI::White);
	CloseButton->AddChild(CloseText);
	CloseButton->OnClicked.AddDynamic(this, &UAMAutomationWindow::Close);
	if (UHorizontalBoxSlot* CloseSlot = TitleBar->AddChildToHorizontalBox(CloseButton))
	{
		CloseSlot->SetVerticalAlignment(VAlign_Center);
	}

	// The machine's Automation tab, standalone (no game window around it).
	UAMAutomationTab::SetNextStandaloneMachine(Machine);
	mTab = CreateWidget<UAMAutomationTab>(this, UAMAutomationTab::StaticClass());
	UAMAutomationTab::SetNextStandaloneMachine(nullptr);
	if (mTab)
	{
		if (UVerticalBoxSlot* TabSlot = Column->AddChildToVerticalBox(mTab))
		{
			TabSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			TabSlot->SetHorizontalAlignment(HAlign_Fill);
			TabSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}
}

FReply UAMAutomationWindow::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UAMAutomationWindow::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// The building dismantled meanwhile (or the mod paused): nothing left to edit.
	if (!mMachine.IsValid())
	{
		Close();
	}
}

void UAMAutomationWindow::Close()
{
	if (bClosed)
	{
		return;
	}
	bClosed = true;
	if (APlayerController* Player = mPlayer.Get())
	{
		Player->SetInputMode(FInputModeGameOnly());
		Player->SetShowMouseCursor(false);
	}
	RemoveFromParent();
}

void UAMAutomationWindow::NativeDestruct()
{
	// Removed by someone else (a level change): give the game its input back too.
	if (!bClosed)
	{
		bClosed = true;
		if (APlayerController* Player = mPlayer.Get())
		{
			Player->SetInputMode(FInputModeGameOnly());
			Player->SetShowMouseCursor(false);
		}
	}
	Super::NativeDestruct();
}
