// A circle that fills up clockwise from the top while a bin is held down (Events Overview, action
// groups): drawn over the bin itself, no popup. 0 = only a faint ring, 1 = the full orange circle.
#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Widgets/SLeafWidget.h"
#include "AMHoldRing.generated.h"

class SAMHoldRing : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAMHoldRing) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) {}
	void SetPercent(float InPercent) { Percent = FMath::Clamp(InPercent, 0.f, 1.f); }

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D(26.f, 26.f); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	float Percent = 0.f;
};

UCLASS()
class AUTOMATEDMACHINES_API UAMHoldRing : public UWidget
{
	GENERATED_BODY()

public:
	void SetPercent(float InPercent);

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SAMHoldRing> MyRing;
	float Percent = 0.f;
};
