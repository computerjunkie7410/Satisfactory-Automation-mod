#include "UI/AMHoldRing.h"
#include "UI/AMUIHelpers.h"
#include "Rendering/DrawElements.h"

int32 SAMHoldRing::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	constexpr float Thickness = 3.f;
	constexpr int32 Segments = 48;
	const FVector2f Size(AllottedGeometry.GetLocalSize());
	const FVector2f Center = Size * 0.5f;
	const float Radius = FMath::Max(1.f, FMath::Min(Size.X, Size.Y) * 0.5f - Thickness);
	// From the top (-90 degrees), clockwise (screen Y points down).
	auto PointAt = [&Center, Radius](float Fraction)
	{
		const float Angle = -UE_HALF_PI + UE_TWO_PI * Fraction;
		return FVector2f(Center.X + Radius * FMath::Cos(Angle), Center.Y + Radius * FMath::Sin(Angle));
	};

	TArray<FVector2f> Track;
	for (int32 Index = 0; Index <= Segments; ++Index)
	{
		Track.Add(PointAt(static_cast<float>(Index) / Segments));
	}
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Track,
		ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.25f), true, Thickness);

	if (Percent > 0.f)
	{
		const int32 Count = FMath::Max(2, FMath::CeilToInt(Segments * Percent) + 1);
		TArray<FVector2f> Arc;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Arc.Add(PointAt(Percent * Index / (Count - 1)));
		}
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Arc,
			ESlateDrawEffect::None, AMUI::Orange, true, Thickness);
	}
	return LayerId + 1;
}

void UAMHoldRing::SetPercent(float InPercent)
{
	Percent = FMath::Clamp(InPercent, 0.f, 1.f);
	if (MyRing.IsValid())
	{
		MyRing->SetPercent(Percent);
		MyRing->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void UAMHoldRing::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyRing.Reset();
}

TSharedRef<SWidget> UAMHoldRing::RebuildWidget()
{
	MyRing = SNew(SAMHoldRing);
	MyRing->SetPercent(Percent);
	return MyRing.ToSharedRef();
}
