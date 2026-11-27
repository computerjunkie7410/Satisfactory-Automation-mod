#include "Automation/AMModHooks.h"
#include "AutomatedMachinesModule.h"
#include "UObject/UnrealType.h"
#include "UObject/Class.h"

namespace
{
	const TCHAR* const AMMH_ConditionPrefix = TEXT("AM_Condition_");
	const TCHAR* const AMMH_ActionPrefix = TEXT("AM_Action_");
	const TCHAR* const AMMH_InfoPrefix = TEXT("AM_Info_");

	bool AMMH_IsNumber(const FProperty* Property)
	{
		return Property && (Property->IsA<FFloatProperty>() || Property->IsA<FDoubleProperty>() || Property->IsA<FIntProperty>());
	}

	/** The function's inputs and its return value (null if none). */
	void AMMH_Params(const UFunction* Function, TArray<FProperty*>& OutInputs, FProperty*& OutReturn)
	{
		OutReturn = nullptr;
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				OutReturn = *It;
			}
			else if (!It->HasAnyPropertyFlags(CPF_OutParm) || It->HasAnyPropertyFlags(CPF_ReferenceParm))
			{
				OutInputs.Add(*It);
			}
			else
			{
				// A Blueprint function's outputs are out parameters: the first Boolean / Text one is its result.
				if (!OutReturn)
				{
					OutReturn = *It;
				}
			}
		}
	}

	/** The enum of an enum input (a byte with an enum, or an enum property), or null. */
	const UEnum* AMMH_Enum(const FProperty* Property)
	{
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			return EnumProperty->GetEnum();
		}
		if (const FByteProperty* Byte = CastField<FByteProperty>(Property))
		{
			return Byte->Enum;
		}
		return nullptr;
	}

	/** "AlwaysOpen" / "Always_Open" -> "Always Open". */
	FString AMMH_Words(const FString& Name)
	{
		FString Result;
		for (int32 Index = 0; Index < Name.Len(); ++Index)
		{
			const TCHAR Char = Name[Index];
			if (Char == TEXT('_'))
			{
				Result += TEXT(' ');
				continue;
			}
			if (Index > 0 && FChar::IsUpper(Char) && FChar::IsLower(Name[Index - 1]))
			{
				Result += TEXT(' ');
			}
			Result += Char;
		}
		Result.TrimStartAndEndInline();
		return Result;
	}

	FString AMMH_Label(const FString& Rest)
	{
		FString Label = Rest.Replace(TEXT("_"), TEXT(" "));
		Label.TrimStartAndEndInline();
		return Label;
	}

	/** Sets a number input to Value (Float, Double or Integer). */
	void AMMH_SetNumber(const FProperty* Property, void* Params, float Value)
	{
		void* Address = Property->ContainerPtrToValuePtr<void>(Params);
		if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
		{
			Float->SetPropertyValue(Address, Value);
		}
		else if (const FDoubleProperty* Double = CastField<FDoubleProperty>(Property))
		{
			Double->SetPropertyValue(Address, Value);
		}
		else if (const FIntProperty* Int = CastField<FIntProperty>(Property))
		{
			Int->SetPropertyValue(Address, FMath::RoundToInt(Value));
		}
	}

	/** Calls Function on Object with its inputs set by Fill; Read gets the parameters after the call. */
	template <typename TFill, typename TRead>
	bool AMMH_Call(UObject* Object, FName FunctionName, TFill&& Fill, TRead&& Read)
	{
		UFunction* Function = Object ? Object->FindFunction(FunctionName) : nullptr;
		if (!Function)
		{
			return false;
		}
		uint8* Params = static_cast<uint8*>(FMemory_Alloca_Aligned(FMath::Max<int32>(Function->ParmsSize, 1), Function->GetMinAlignment()));
		FMemory::Memzero(Params, Function->ParmsSize);
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Params);
		}
		TArray<FProperty*> Inputs;
		FProperty* Return = nullptr;
		AMMH_Params(Function, Inputs, Return);
		Fill(Inputs, Params);
		Object->ProcessEvent(Function, Params);
		Read(Return, Params);
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Params);
		}
		return true;
	}

	TMap<TWeakObjectPtr<const UClass>, TArray<AMModHooks::FHook>> GAMMH_Cache;
}

const TArray<AMModHooks::FHook>& AMModHooks::GetHooks(const UClass* Class)
{
	static const TArray<FHook> None;
	if (!Class)
	{
		return None;
	}
	if (const TArray<FHook>* Cached = GAMMH_Cache.Find(Class))
	{
		return *Cached;
	}
	TArray<FHook> Hooks;
	TMap<FString, FName> Infos;
	for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		const UFunction* Function = *It;
		const FString Name = Function->GetName();
		const bool bCondition = Name.StartsWith(AMMH_ConditionPrefix);
		const bool bAction = Name.StartsWith(AMMH_ActionPrefix);
		const bool bInfo = Name.StartsWith(AMMH_InfoPrefix);
		if (!bCondition && !bAction && !bInfo)
		{
			continue;
		}
		TArray<FProperty*> Inputs;
		FProperty* Return = nullptr;
		AMMH_Params(Function, Inputs, Return);
		if (bInfo)
		{
			if (Inputs.Num() == 0 && Return && (Return->IsA<FTextProperty>() || Return->IsA<FStrProperty>()))
			{
				Infos.Add(Name.RightChop(FCString::Strlen(AMMH_InfoPrefix)), Function->GetFName());
			}
			else
			{
				UE_LOG(LogAutomatedMachines, Warning, TEXT("Mod hooks: %s.%s skipped: an AM_Info_ function takes no input and returns Text or String"), *Class->GetName(), *Name);
			}
			continue;
		}
		FHook Hook;
		Hook.Function = Function->GetFName();
		Hook.bCondition = bCondition;
		Hook.Label = AMMH_Label(Name.RightChop(FCString::Strlen(bCondition ? AMMH_ConditionPrefix : AMMH_ActionPrefix)));
		bool bValid = Inputs.Num() <= 1 && !Hook.Label.IsEmpty();
		if (bValid && Inputs.Num() == 1)
		{
			if (AMMH_IsNumber(Inputs[0]))
			{
				Hook.Value = EValue::Number;
			}
			else if (bAction && Inputs[0]->IsA<FBoolProperty>())
			{
				Hook.Value = EValue::Switch;
			}
			else if (const UEnum* Enum = bAction ? AMMH_Enum(Inputs[0]) : nullptr)
			{
				// Its entries' names (a Blueprint enum: the names typed in it), not the hidden _MAX.
				Hook.Value = EValue::Choice;
				const int32 Count = Enum->NumEnums() - (Enum->ContainsExistingMax() ? 1 : 0);
				for (int32 Index = 0; Index < Count; ++Index)
				{
					FString Shown = Enum->GetDisplayNameTextByIndex(Index).ToString();
					if (Shown.IsEmpty())
					{
						Shown = Enum->GetNameStringByIndex(Index);
					}
					Hook.Choices.Add(Shown.Contains(TEXT(" ")) ? Shown : AMMH_Words(Shown));
					Hook.ChoiceValues.Add(Enum->GetValueByIndex(Index));
				}
				bValid = Count > 0;
			}
			else
			{
				bValid = false;
			}
		}
		if (bValid && bCondition && !(Return && Return->IsA<FBoolProperty>()))
		{
			bValid = false;
		}
		if (!bValid)
		{
			UE_LOG(LogAutomatedMachines, Warning, TEXT("Mod hooks: %s.%s skipped: see docs/MODDING.md for the inputs and result it may have"), *Class->GetName(), *Name);
			continue;
		}
		Hooks.Add(Hook);
	}
	for (FHook& Hook : Hooks)
	{
		const FString Rest = Hook.Function.ToString().RightChop(FCString::Strlen(Hook.bCondition ? AMMH_ConditionPrefix : AMMH_ActionPrefix));
		if (const FName* Info = Infos.Find(Rest))
		{
			Hook.InfoFunction = *Info;
		}
	}
	Hooks.Sort([](const FHook& A, const FHook& B) { return A.Label < B.Label; });
	if (Hooks.Num() > 0)
	{
		TArray<FString> Names;
		for (const FHook& Hook : Hooks)
		{
			Names.Add(FString::Printf(TEXT("%s %s%s"), Hook.bCondition ? TEXT("condition") : TEXT("action"), *Hook.Label,
				Hook.Value == EValue::Number ? TEXT(" (number)") : Hook.Value == EValue::Switch ? TEXT(" (on / off)")
				: Hook.Value == EValue::Choice ? *FString::Printf(TEXT(" (%s)"), *FString::Join(Hook.Choices, TEXT(" / "))) : TEXT("")));
		}
		UE_LOG(LogAutomatedMachines, Log, TEXT("Mod hooks: %s: %s"), *Class->GetName(), *FString::Join(Names, TEXT(", ")));
	}
	return GAMMH_Cache.Add(Class, MoveTemp(Hooks));
}

const AMModHooks::FHook* AMModHooks::FindHook(const UObject* Object, FName Function)
{
	return Object && !Function.IsNone() ? GetHooks(Object->GetClass()).FindByPredicate([Function](const FHook& Hook) { return Hook.Function == Function; }) : nullptr;
}

bool AMModHooks::HasHooks(const UObject* Object)
{
	return Object && GetHooks(Object->GetClass()).Num() > 0;
}

bool AMModHooks::HasConditions(const UObject* Object)
{
	return Object && GetHooks(Object->GetClass()).ContainsByPredicate([](const FHook& Hook) { return Hook.bCondition; });
}

bool AMModHooks::HasActions(const UObject* Object)
{
	return Object && GetHooks(Object->GetClass()).ContainsByPredicate([](const FHook& Hook) { return !Hook.bCondition; });
}

bool AMModHooks::CallCondition(UObject* Object, FName Function, float Value)
{
	const FHook* Hook = FindHook(Object, Function);
	if (!Hook || !Hook->bCondition)
	{
		return false;
	}
	bool bResult = false;
	AMMH_Call(Object, Function,
		[Value](const TArray<FProperty*>& Inputs, void* Params)
		{
			if (Inputs.Num() == 1)
			{
				AMMH_SetNumber(Inputs[0], Params, Value);
			}
		},
		[&bResult](const FProperty* Return, void* Params)
		{
			if (const FBoolProperty* Bool = CastField<FBoolProperty>(Return))
			{
				bResult = Bool->GetPropertyValue_InContainer(Params);
			}
		});
	return bResult;
}

void AMModHooks::CallAction(UObject* Object, FName Function, float Value, bool bOn)
{
	const FHook* Hook = FindHook(Object, Function);
	if (!Hook || Hook->bCondition)
	{
		return;
	}
	AMMH_Call(Object, Function,
		[Value, bOn](const TArray<FProperty*>& Inputs, void* Params)
		{
			if (Inputs.Num() != 1)
			{
				return;
			}
			if (const FBoolProperty* Bool = CastField<FBoolProperty>(Inputs[0]))
			{
				Bool->SetPropertyValue_InContainer(Params, bOn);
			}
			else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Inputs[0]))
			{
				EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(EnumProperty->ContainerPtrToValuePtr<void>(Params), static_cast<int64>(FMath::RoundToInt(Value)));
			}
			else if (const FByteProperty* Byte = CastField<FByteProperty>(Inputs[0]))
			{
				Byte->SetPropertyValue_InContainer(Params, static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value), 0, 255)));
			}
			else
			{
				AMMH_SetNumber(Inputs[0], Params, Value);
			}
		},
		[](const FProperty*, void*) {});
}

FText AMModHooks::GetInfo(UObject* Object, FName Function)
{
	const FHook* Hook = FindHook(Object, Function);
	FText Result;
	if (Hook && !Hook->InfoFunction.IsNone())
	{
		AMMH_Call(Object, Hook->InfoFunction,
			[](const TArray<FProperty*>&, void*) {},
			[&Result](const FProperty* Return, void* Params)
			{
				if (const FTextProperty* Text = CastField<FTextProperty>(Return))
				{
					Result = Text->GetPropertyValue_InContainer(Params);
				}
				else if (const FStrProperty* String = CastField<FStrProperty>(Return))
				{
					Result = FText::FromString(String->GetPropertyValue_InContainer(Params));
				}
			});
	}
	if (Result.IsEmpty())
	{
		Result = FText::FromString(Hook && Hook->bCondition
			? TEXT("A condition added by the mod of this building.")
			: TEXT("An action added by the mod of this building."));
	}
	return Result;
}

FString AMModHooks::GetLabel(const UObject* Object, FName Function)
{
	const FHook* Hook = FindHook(Object, Function);
	return Hook ? Hook->Label : Function.ToString();
}

int32 AMModHooks::GetChoiceIndex(const FHook& Hook, float Value)
{
	const int32 Index = Hook.ChoiceValues.IndexOfByKey(static_cast<int64>(FMath::RoundToInt(Value)));
	return Index != INDEX_NONE ? Index : 0;
}
