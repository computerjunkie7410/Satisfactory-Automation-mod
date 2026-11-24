#include "Automation/AMLocator.h"
#include "AutomatedMachinesModule.h"
#include "Automation/AMSound.h"
#include "FGActorRepresentationManager.h"
#include "FGActorRepresentation.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Representation/FGResourceNodeRepresentation.h"
#include "FGCompassMaterialSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UnrealType.h"
#include "FGIconDatabaseSubsystem.h"
#include "FGIconLibrary.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Canvas.h"
#include "TextureResource.h"

namespace
{
	/** The icon's colour on the compass and the map: white, like the ores (the material multiplies the icon by it). */
	const FLinearColor AMLC_Colour(1.f, 1.f, 1.f, 1.f);

	/** Closer than this to the machine's edge, in cm (a foundation): arrived, the markers go. */
	constexpr float AMLC_ArriveDistance = 800.f;

	/** At the latest after this long, in seconds. */
	constexpr double AMLC_MaxSeconds = 15.0 * 60.0;

	/** The ring effect again every this many seconds. */
	constexpr double AMLC_PulseSeconds = 4.0;

	/** The Ping sound of AMRules::GetSounds (the resource scanner's ping). */
	constexpr int32 AMLC_PingSound = 1;

	TWeakObjectPtr<AAMLocator> AMLC_Instance;

	/**
	 * The ring effect: the resource scanner's hit ping on a node (found with an asset dump of the
	 * effects named *Scan* / *Ping*, 5 Nov 2026), else the ping folder's highlight marker.
	 */
	const TCHAR* const AMLC_EffectPaths[] = {
		TEXT("/Game/FactoryGame/VFX/Misc/Ping/P_ResourceHitPing_01.P_ResourceHitPing_01"),
		TEXT("/Game/FactoryGame/VFX/Misc/Ping/NS_HighlightMarker_01.NS_HighlightMarker_01"),
	};
	bool bAMLC_EffectSearched = false;
	TWeakObjectPtr<UObject> AMLC_Effect;

	UObject* AMLC_FindEffect()
	{
		if (!bAMLC_EffectSearched)
		{
			bAMLC_EffectSearched = true;
			for (const TCHAR* Path : AMLC_EffectPaths)
			{
				if (UObject* Effect = LoadObject<UObject>(nullptr, Path))
				{
					AMLC_Effect = Effect;
					break;
				}
			}
			UE_LOG(LogAutomatedMachines, Log, TEXT("Locate effect: %s"), AMLC_Effect.IsValid() ? *AMLC_Effect->GetPathName() : TEXT("none found"));
		}
		return AMLC_Effect.Get();
	}

	/** UNiagaraFunctionLibrary::SpawnSystemAtLocation, called by name (no build dependency on Niagara). */
	void AMLC_SpawnNiagara(UWorld* World, UObject* System, const FVector& Location)
	{
		UClass* Library = FindObject<UClass>(nullptr, TEXT("/Script/Niagara.NiagaraFunctionLibrary"));
		UFunction* Function = Library ? Library->FindFunctionByName(TEXT("SpawnSystemAtLocation")) : nullptr;
		if (!Function)
		{
			return;
		}
		TArray<uint8> Params;
		Params.SetNumZeroed(Function->ParmsSize);
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Params.GetData());
			const FString Name = It->GetName();
			if (FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(*It))
			{
				if (Name == TEXT("WorldContextObject"))
				{
					Object->SetObjectPropertyValue_InContainer(Params.GetData(), World);
				}
				else if (Name == TEXT("SystemTemplate"))
				{
					Object->SetObjectPropertyValue_InContainer(Params.GetData(), System);
				}
			}
			else if (FStructProperty* Struct = CastField<FStructProperty>(*It))
			{
				if (Name == TEXT("Location") && Struct->Struct == TBaseStructure<FVector>::Get())
				{
					*Struct->ContainerPtrToValuePtr<FVector>(Params.GetData()) = Location;
				}
				else if (Name == TEXT("Scale") && Struct->Struct == TBaseStructure<FVector>::Get())
				{
					*Struct->ContainerPtrToValuePtr<FVector>(Params.GetData()) = FVector::OneVector;
				}
			}
			else if (FBoolProperty* Bool = CastField<FBoolProperty>(*It))
			{
				Bool->SetPropertyValue_InContainer(Params.GetData(), Name == TEXT("bAutoDestroy") || Name == TEXT("bAutoActivate"));
			}
		}
		Library->GetDefaultObject()->ProcessEvent(Function, Params.GetData());
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Params.GetData());
		}
	}
}

// ----- Compass representation ---------------------------------------------------------------------

UMaterialInterface* UAMLocateRepresentation::GetRepresentationCompassMaterial() const
{
	const UFGCompassMaterialSettings* Settings = UFGCompassMaterialSettings::Get();
	UMaterialInterface* Material = Settings ? Settings->mResourceCompassMaterial.LoadSynchronous() : nullptr;
	return Material ? Material : Super::GetRepresentationCompassMaterial();
}

void UAMLocateRepresentation::UpdateRepresentationCompassMaterial(UMaterialInstanceDynamic* CompassMaterialInstance, APlayerController* OwnerPlayerController) const
{
	Super::UpdateRepresentationCompassMaterial(CompassMaterialInstance, OwnerPlayerController);
	// A point icon: the compass shows the plain symbol (it takes only the outline: the circle was a
	// white disc there, 24 Nov 2026). Whichever texture parameter got the icon gets the symbol.
	UTexture2D* Icon = GetRepresentationTexture();
	UTexture2D* Symbol = AAMLocator::GetPointIconSymbol(Icon);
	if (!CompassMaterialInstance || !Symbol)
	{
		return;
	}
	TArray<FName> Names;
	for (const FTextureParameterValue& Parameter : CompassMaterialInstance->TextureParameterValues)
	{
		if (Parameter.ParameterValue == Icon)
		{
			Names.Add(Parameter.ParameterInfo.Name);
		}
	}
	for (const FName& Name : Names)
	{
		CompassMaterialInstance->SetTextureParameterValue(Name, Symbol);
	}
	static bool bLogged = false;
	if (!bLogged)
	{
		// Once: which parameter took it (or which there are, if none had the icon yet).
		bLogged = true;
		TArray<FString> All;
		for (const FTextureParameterValue& Parameter : CompassMaterialInstance->TextureParameterValues)
		{
			All.Add(FString::Printf(TEXT("%s=%s"), *Parameter.ParameterInfo.Name.ToString(), *GetNameSafe(Parameter.ParameterValue)));
		}
		UE_LOG(LogAutomatedMachines, Log, TEXT("Point icons: compass symbol set on %d parameter(s); texture parameters: %s"), Names.Num(), *FString::Join(All, TEXT(", ")));
	}
}

float UAMLocateRepresentation::GetCompassHeightAlignment() const
{
	// The ores' row.
	return GetDefault<UFGResourceNodeRepresentation>()->GetCompassHeightAlignment();
}

FText UAMLocateRepresentation::GetDynamicCompassRepresentationText(APlayerController* OwnerPlayerController, float DistanceToPlayer) const
{
	return FText::FromString(FString::Printf(TEXT("%d m"), FMath::RoundToInt(DistanceToPlayer / 100.f)));
}

float UAMLocateRepresentation::CalculateCompassRepresentationScale(APlayerController* OwnerPlayerController, float DistanceToPlayer) const
{
	// Bigger when near, smaller when far, like the ores.
	const UFGCompassMaterialSettings* Settings = UFGCompassMaterialSettings::Get();
	if (!Settings || Settings->mResourceMaxCompassDistance <= 0.f)
	{
		return 1.f;
	}
	const float Far = FMath::Clamp(DistanceToPlayer / Settings->mResourceMaxCompassDistance, 0.f, 1.f);
	return FMath::Lerp(Settings->mResourceCompassDistanceNearScale, Settings->mResourceCompassDistanceFarScale, Far);
}

UMaterialInterface* UAMLocateRepresentation::GetRepresentationCompassEffectMaterial() const
{
	const UFGCompassMaterialSettings* Settings = UFGCompassMaterialSettings::Get();
	return Settings ? Settings->mResourceScannerPingEffectMaterial.LoadSynchronous() : nullptr;
}

FVector2f UAMLocateRepresentation::GetRepresentationCompassEffectSize() const
{
	const UFGCompassMaterialSettings* Settings = UFGCompassMaterialSettings::Get();
	return Settings ? Settings->mResourceScannerPingEffectImageSize : FVector2f(400.f);
}

bool UAMLocateRepresentation::UpdateRepresentationCompassEffectMaterial(UMaterialInstanceDynamic* CompassMaterialInstance, APlayerController* OwnerPlayerController, float DistanceToPlayer, float TotalTime)
{
	// The scanner's ping when it appears, as the game plays it for ores (watched 5 Nov 2026):
	// EffectAlpha of MM_UI_CompassResourceScanFX goes 0 -> 1 over one loop (1.5 s), and the effect
	// says it is done (false) after its total time.
	const UFGCompassMaterialSettings* Settings = UFGCompassMaterialSettings::Get();
	const float Loop = Settings ? FMath::Max(Settings->mResourceScannerPingEffectLoopTime, 0.01f) : 1.5f;
	const float Total = Settings ? Settings->mResourceScannerPingEffectTotalTime : 1.5f;
	if (CompassMaterialInstance)
	{
		CompassMaterialInstance->SetScalarParameterValue(TEXT("EffectAlpha"), FMath::Fmod(TotalTime, Loop) / Loop);
	}
	return TotalTime < Total;
}

// ----- Marker -------------------------------------------------------------------------------------

AAMLocateMarker::AAMLocateMarker()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AAMLocateMarker::Setup(UTexture2D* InTexture, const FText& InName, const FLinearColor& InColour)
{
	mTexture = InTexture;
	mName = InName;
	mColour = InColour;
}

FLinearColor AAMLocateMarker::GetActorRepresentationColor()
{
	return mColour;
}

bool AAMLocateMarker::AddAsRepresentation()
{
	if (AFGActorRepresentationManager* Manager = AFGActorRepresentationManager::Get(GetWorld()))
	{
		// Local: only this player's compass and map; shown like an ore (UAMLocateRepresentation).
		mRepresentation = Manager->CreateAndAddNewRepresentation(this, true, UAMLocateRepresentation::StaticClass());
	}
	return mRepresentation != nullptr;
}

bool AAMLocateMarker::UpdateRepresentation()
{
	AFGActorRepresentationManager* Manager = AFGActorRepresentationManager::Get(GetWorld());
	return Manager && mRepresentation && Manager->UpdateRepresentation(mRepresentation);
}

bool AAMLocateMarker::RemoveAsRepresentation()
{
	AFGActorRepresentationManager* Manager = AFGActorRepresentationManager::Get(GetWorld());
	if (Manager && mRepresentation)
	{
		Manager->RemoveRepresentation(mRepresentation);
		mRepresentation = nullptr;
		return true;
	}
	return false;
}

// ----- Locator ------------------------------------------------------------------------------------

AAMLocator::AAMLocator()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

AAMLocator* AAMLocator::Get(UWorld* World)
{
	if (AAMLocator* Existing = AMLC_Instance.Get())
	{
		if (Existing->GetWorld() == World)
		{
			return Existing;
		}
	}
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAMLocator* Locator = World->SpawnActor<AAMLocator>(Params);
	AMLC_Instance = Locator;
	return Locator;
}

bool AAMLocator::IsLocating(const AActor* Target) const
{
	return mLocated.ContainsByPredicate([Target](const FLocated& Entry) { return Entry.Target.Get() == Target; });
}

bool AAMLocator::Toggle(AActor* Target, UTexture2D* Icon, const FString& Name)
{
	UWorld* World = GetWorld();
	if (!Target || !World)
	{
		return false;
	}
	const int32 Existing = mLocated.IndexOfByPredicate([Target](const FLocated& Entry) { return Entry.Target.Get() == Target; });
	if (Existing != INDEX_NONE)
	{
		Stop(Existing, TEXT("asked again"));
		return false;
	}
	return Start(Target, Icon, Name);
}

bool AAMLocator::Start(AActor* Target, UTexture2D* Icon, const FString& Name, const FLinearColor& Colour)
{
	UWorld* World = GetWorld();
	if (!Target || !World)
	{
		return false;
	}
	// Already shown: it starts counting again (the 15 minutes), with a ping.
	if (FLocated* Shown = mLocated.FindByPredicate([Target](const FLocated& Entry) { return Entry.Target.Get() == Target; }))
	{
		Shown->Start = FPlatformTime::Seconds();
		// Its icon may change (another event's action with another icon): made again, as its kind on
		// the map (ore / beacon) may change with it.
		if (AAMLocateMarker* Marker = Shown->Marker.Get())
		{
			Marker->RemoveAsRepresentation();
			Marker->Setup(Icon, FText::FromString(Name), Colour);
			Marker->AddAsRepresentation();
		}
		if (APlayerController* Player = World->GetFirstPlayerController())
		{
			AMSound::PlayForPlayer(Player, AMLC_PingSound);
		}
		return true;
	}
	FVector Origin;
	FVector Extent;
	Target->GetActorBounds(true, Origin, Extent);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAMLocateMarker* Marker = World->SpawnActor<AAMLocateMarker>(Origin, FRotator::ZeroRotator, Params);
	if (Marker)
	{
		Marker->Setup(Icon, FText::FromString(Name), Colour);
		if (!Marker->AddAsRepresentation())
		{
			UE_LOG(LogAutomatedMachines, Warning, TEXT("Locate: no compass / map representation for %s"), *Name);
		}
	}
	FLocated& Added = mLocated.AddDefaulted_GetRef();
	Added.Target = Target;
	Added.Marker = Marker;
	Added.Name = Name;
	Added.Start = FPlatformTime::Seconds();
	Added.NextPulse = Added.Start;
	Added.Radius = static_cast<float>(Extent.Size2D());
	Added.Bounds = FBox(Origin - Extent, Origin + Extent);

	if (APlayerController* Player = World->GetFirstPlayerController())
	{
		AMSound::PlayForPlayer(Player, AMLC_PingSound);
	}
	UE_LOG(LogAutomatedMachines, Log, TEXT("Locate: %s at %s"), *Name, *Origin.ToCompactString());
	Check();
	if (!World->GetTimerManager().IsTimerActive(mTimer))
	{
		World->GetTimerManager().SetTimer(mTimer, this, &AAMLocator::Check, 0.25f, true);
	}
	return true;
}

void AAMLocator::Check()
{
	UWorld* World = GetWorld();
	const APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	const double Now = FPlatformTime::Seconds();
	for (int32 Index = mLocated.Num() - 1; Index >= 0; --Index)
	{
		FLocated& Entry = mLocated[Index];
		const AActor* Target = Entry.Target.Get();
		if (!Target)
		{
			Stop(Index, TEXT("gone"));
			continue;
		}
		const bool bNear = Pawn && Entry.Bounds.ComputeSquaredDistanceToPoint(Pawn->GetActorLocation()) < FMath::Square(AMLC_ArriveDistance);
		if (!bNear)
		{
			Entry.bArmed = true;
		}
		else if (Entry.bArmed)
		{
			Stop(Index, TEXT("arrived"));
			continue;
		}
		if (Now - Entry.Start > AMLC_MaxSeconds)
		{
			Stop(Index, TEXT("time"));
			continue;
		}
		if (Now >= Entry.NextPulse)
		{
			Entry.NextPulse = Now + AMLC_PulseSeconds;
			Pulse(Target, Entry.Radius);
		}
	}
	if (mLocated.Num() == 0 && World)
	{
		World->GetTimerManager().ClearTimer(mTimer);
	}
}

void AAMLocator::Stop(int32 Index, const TCHAR* Why)
{
	if (!mLocated.IsValidIndex(Index))
	{
		return;
	}
	if (AAMLocateMarker* Marker = mLocated[Index].Marker.Get())
	{
		Marker->RemoveAsRepresentation();
		Marker->Destroy();
	}
	UE_LOG(LogAutomatedMachines, Log, TEXT("Locate: %s done (%s)"), *mLocated[Index].Name, Why);
	mLocated.RemoveAt(Index);
}

void AAMLocator::Pulse(const AActor* Target, float Radius)
{
	UWorld* World = GetWorld();
	UObject* Effect = AMLC_FindEffect();
	if (!World || !Target || !Effect)
	{
		return;
	}
	FVector Origin;
	FVector Extent;
	Target->GetActorBounds(true, Origin, Extent);
	if (UParticleSystem* Cascade = Cast<UParticleSystem>(Effect))
	{
		UGameplayStatics::SpawnEmitterAtLocation(World, Cascade, Origin, FRotator::ZeroRotator, FVector::OneVector, true);
	}
	else
	{
		AMLC_SpawnNiagara(World, Effect, Origin);
	}
}

void AAMLocator::EndPlay(const EEndPlayReason::Type Reason)
{
	for (int32 Index = mLocated.Num() - 1; Index >= 0; --Index)
	{
		Stop(Index, TEXT("world ends"));
	}
	Super::EndPlay(Reason);
}

namespace
{
	/**
	 * Names to look for per point icon (AMRules::EPointIcon order, Machine first): an icon whose name
	 * equals one of them wins, else one whose name contains one (lower case, in this order).
	 */
	const TArray<TArray<FString>>& AMLC_PointIconNames()
	{
		static const TArray<TArray<FString>> Names = {
			{},
			{ TEXT("droplet"), TEXT("drop"), TEXT("water"), TEXT("fluid") },
			{ TEXT("radiation"), TEXT("radioactive"), TEXT("nuclear") },
			{ TEXT("thumbs up"), TEXT("thumb up"), TEXT("thumbsup"), TEXT("like") },
			{ TEXT("thumbs down"), TEXT("thumb down"), TEXT("thumbsdown"), TEXT("dislike") },
			{ TEXT("stop"), TEXT("x"), TEXT("cross"), TEXT("cancel"), TEXT("no") },
			{ TEXT("storage crate"), TEXT("crate"), TEXT("storage"), TEXT("box"), TEXT("container") },
			{ TEXT("power"), TEXT("lightning"), TEXT("electricity"), TEXT("energy"), TEXT("bolt") },
			{ TEXT("warning"), TEXT("caution"), TEXT("danger"), TEXT("exclamation"), TEXT("alert") },
			{ TEXT("ficsit checkmark"), TEXT("ficsit check"), TEXT("checkmark"), TEXT("check"), TEXT("ficsit") },
			{ TEXT("recycle"), TEXT("recycling") },
			{ TEXT("trash"), TEXT("trash can"), TEXT("garbage"), TEXT("bin") },
			{ TEXT("question mark"), TEXT("question") },
		};
		return Names;
	}

	/** Point icon (circle + symbol) -> its plain symbol, for the compass. Both are rooted. */
	TMap<TWeakObjectPtr<const UTexture>, TWeakObjectPtr<UTexture2D>> AMLC_Symbols;

	/** The point icons' size, in pixels (the picture made from the circle and the symbol). */
	constexpr int32 AMLC_IconSize = 128;

	/** A new picture of Size x Size from Pixels (BGRA, rows top to bottom), kept loaded. */
	UTexture2D* AMLC_MakeTexture(const TArray<FColor>& Pixels, int32 Size)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8);
		if (!Texture || Pixels.Num() != Size * Size)
		{
			return nullptr;
		}
		Texture->SRGB = true;
		Texture->Filter = TF_Bilinear;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		Texture->AddToRoot();
		return Texture;
	}

	/**
	 * The symbol's shape: how much of each pixel it covers (0..255), at Size x Size, the symbol taking
	 * Scale of it in the middle. The GPU draws it over black (its white shape shows as its coverage,
	 * whether it has an alpha channel or is a grey image), then it is read back.
	 */
	bool AMLC_SymbolCoverage(UWorld* World, UTexture2D* Symbol, int32 Size, float Scale, TArray<uint8>& OutCoverage)
	{
		UTextureRenderTarget2D* Target = UKismetRenderingLibrary::CreateRenderTarget2D(World, Size, Size, RTF_RGBA8, FLinearColor::Black);
		if (!Target)
		{
			return false;
		}
		UCanvas* Canvas = nullptr;
		FVector2D CanvasSize;
		FDrawToRenderTargetContext Context;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, Target, Canvas, CanvasSize, Context);
		if (Canvas)
		{
			const FVector2D SymbolSize = CanvasSize * Scale;
			Canvas->K2_DrawTexture(Symbol, (CanvasSize - SymbolSize) * 0.5f, SymbolSize, FVector2D::ZeroVector, FVector2D::UnitVector, FLinearColor::White, BLEND_Translucent);
		}
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);
		TArray<FColor> Pixels;
		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		const bool bRead = Canvas && Resource && Resource->ReadPixels(Pixels) && Pixels.Num() == Size * Size;
		UKismetRenderingLibrary::ReleaseRenderTarget2D(Target);
		if (!bRead)
		{
			return false;
		}
		OutCoverage.SetNumUninitialized(Size * Size);
		for (int32 Index = 0; Index < Pixels.Num(); ++Index)
		{
			OutCoverage[Index] = FMath::Max3(Pixels[Index].R, Pixels[Index].G, Pixels[Index].B);
		}
		return true;
	}

	/**
	 * The point icon: a gray circle (darker rim, soft edge) with the game's white sign symbol on it, as
	 * one picture (the compass and the map take one picture per mark). Made once per icon, kept.
	 */
	UTexture2D* AMLC_Compose(UWorld* World, UTexture2D* Symbol)
	{
		const int32 Size = AMLC_IconSize;
		TArray<uint8> Coverage;
		if (!World || !Symbol || !AMLC_SymbolCoverage(World, Symbol, Size, 0.62f, Coverage))
		{
			return nullptr;
		}
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Size * Size);
		const float Centre = (Size - 1) * 0.5f;
		const float Radius = Size * 0.5f - 1.f;
		const float Rim = Size * 0.045f;
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const int32 Index = Y * Size + X;
				const float Distance = FVector2f(X - Centre, Y - Centre).Size();
				const bool bRim = Distance > Radius - Rim;
				const float CircleAlpha = FMath::Clamp((Radius - Distance) / 1.5f, 0.f, 1.f) * (bRim ? 0.96f : 0.88f);
				const float CircleGrey = bRim ? 38.f : 72.f;
				const float Symbol01 = Coverage[Index] / 255.f;
				// White symbol over the circle.
				const float Alpha = Symbol01 + CircleAlpha * (1.f - Symbol01);
				const float Grey = Alpha > 0.f ? (255.f * Symbol01 + CircleGrey * CircleAlpha * (1.f - Symbol01)) / Alpha : 0.f;
				const uint8 G = static_cast<uint8>(FMath::Clamp(Grey, 0.f, 255.f));
				Pixels[Index] = FColor(G, G, G, static_cast<uint8>(FMath::Clamp(Alpha * 255.f, 0.f, 255.f)));
			}
		}
		return AMLC_MakeTexture(Pixels, Size);
	}

	/** One search of the game's icons; a long word must not be matched by a short one ("no" in "nuclear"). */
	UTexture2D* AMLC_FindIcon(const TArray<FIconData>& Data, const TArray<FString>& Wanted, FString& OutName)
	{
		for (const bool bExact : { true, false })
		{
			for (const FString& Word : Wanted)
			{
				if (!bExact && Word.Len() < 4)
				{
					continue;
				}
				for (const FIconData& Icon : Data)
				{
					const FString Name = Icon.IconName.ToString().ToLower();
					if (bExact ? Name == Word : Name.Contains(Word))
					{
						if (UTexture2D* Texture = Cast<UTexture2D>(Icon.Texture.LoadSynchronous()))
						{
							OutName = Icon.IconName.ToString();
							return Texture;
						}
					}
				}
			}
		}
		return nullptr;
	}
}

UTexture2D* AAMLocator::GetPointIconSymbol(const UTexture* Texture)
{
	const TWeakObjectPtr<UTexture2D>* Symbol = Texture ? AMLC_Symbols.Find(Texture) : nullptr;
	return Symbol ? Symbol->Get() : nullptr;
}

UTexture2D* AAMLocator::GetPointIcon(UWorld* World, uint8 Icon)
{
	// Each icon searched once per game; the picture made (circle + symbol) is kept loaded (rooted: a mark
	// is the only thing that holds it, and the textures were garbage collected once it went, 24 Nov 2026).
	static TMap<uint8, TWeakObjectPtr<UTexture2D>> Found;
	static TSet<uint8> Searched;
	static bool bListed = false;
	const TArray<TArray<FString>>& Names = AMLC_PointIconNames();
	if (!World || !Names.IsValidIndex(Icon) || Names[Icon].Num() == 0)
	{
		return nullptr;
	}
	if (Searched.Contains(Icon))
	{
		return Found.FindRef(Icon).Get();
	}
	AFGIconDatabaseSubsystem* Icons = AFGIconDatabaseSubsystem::Get(World);
	if (!Icons)
	{
		return nullptr;
	}
	Searched.Add(Icon);
	TArray<FIconData> SignIcons;
	TArray<FIconData> MapStamps;
	Icons->GetAllIconDataForType(EIconType::ESIT_Monochrome, true, SignIcons);
	Icons->GetAllIconDataForType(EIconType::ESIT_MapStamp, true, MapStamps);
	if (!bListed)
	{
		// Once, so names can be matched exactly if a guess picks the wrong one.
		bListed = true;
		for (const TPair<const TCHAR*, const TArray<FIconData>*> List : { TPair<const TCHAR*, const TArray<FIconData>*>(TEXT("sign"), &SignIcons), TPair<const TCHAR*, const TArray<FIconData>*>(TEXT("map stamp"), &MapStamps) })
		{
			TArray<FString> All;
			for (const FIconData& Data : *List.Value)
			{
				All.Add(Data.IconName.ToString());
			}
			UE_LOG(LogAutomatedMachines, Log, TEXT("Point icons: %d %s icons: %s"), All.Num(), List.Key, *FString::Join(All, TEXT(", ")));
		}
	}
	FString Name;
	UTexture2D* Texture = AMLC_FindIcon(SignIcons, Names[Icon], Name);
	if (!Texture)
	{
		Texture = AMLC_FindIcon(MapStamps, Names[Icon], Name);
	}
	if (Texture)
	{
		// On the gray circle (one picture); the symbol alone if that fails.
		const FString SymbolPath = Texture->GetPathName();
		if (UTexture2D* Composed = AMLC_Compose(World, Texture))
		{
			Texture->AddToRoot();
			AMLC_Symbols.Add(Composed, Texture);
			Texture = Composed;
		}
		else
		{
			Texture->AddToRoot();
		}
		Found.Add(Icon, Texture);
		UE_LOG(LogAutomatedMachines, Log, TEXT("Point icons: %s = %s (%s)%s"), *Names[Icon][0], *Name, *SymbolPath,
			Texture->GetPathName() == SymbolPath ? TEXT(", without circle") : TEXT(", on the circle"));
	}
	else
	{
		UE_LOG(LogAutomatedMachines, Warning, TEXT("Point icons: no icon found for %s, the machine's icon is used"), *Names[Icon][0]);
	}
	return Texture;
}
