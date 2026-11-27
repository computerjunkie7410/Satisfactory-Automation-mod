#pragma once

#include "CoreMinimal.h"

/**
 * Other mods' conditions and actions, by naming convention (docs/MODDING.md). A building class (C++ or
 * Blueprint) that has functions named
 *     AM_Condition_<Name>   returns a Boolean; no input, or one number (Float / Double / Integer)
 *     AM_Action_<Name>      returns nothing;  no input, one number, one Boolean (an on / off switch),
 *                           or one enum (a dropdown of its names)
 *     AM_Info_<Name>        returns Text or String, no input: the explanation of <Name> (optional)
 * gets them in its Automation tab: "<Name>" with its underscores as spaces. Nothing of the other mod is
 * needed at build time: the functions are found by name in the game's reflection data and called with
 * ProcessEvent (on the server, like the mod's own conditions and actions).
 */
namespace AMModHooks
{
	enum class EValue : uint8
	{
		None,    // no input
		Number,  // one number: a number field
		Switch,  // one Boolean: the on / off switch (actions only)
		Choice,  // one enum: a dropdown of its names (actions only)
	};

	struct FHook
	{
		/** The function's name (saved in the rule). */
		FName Function;
		/** "Tank Empty" */
		FString Label;
		bool bCondition = false;
		EValue Value = EValue::None;
		/** AM_Info_<Name>, if the class has it. */
		FName InfoFunction;
		/** EValue::Choice: the enum's names as shown ("Always Open"), and their values (saved in the rule). */
		TArray<FString> Choices;
		TArray<int64> ChoiceValues;
	};

	/** EValue::Choice: the index of Value among the hook's choices (0 if none matches). */
	AUTOMATEDMACHINES_API int32 GetChoiceIndex(const FHook& Hook, float Value);

	/** The hooks of Class (found once per class, then cached). */
	AUTOMATEDMACHINES_API const TArray<FHook>& GetHooks(const UClass* Class);

	/** The hook named Function on Object's class, or null. */
	AUTOMATEDMACHINES_API const FHook* FindHook(const UObject* Object, FName Function);

	/** True if Object's class has any AM_ condition or action. */
	AUTOMATEDMACHINES_API bool HasHooks(const UObject* Object);
	AUTOMATEDMACHINES_API bool HasConditions(const UObject* Object);
	AUTOMATEDMACHINES_API bool HasActions(const UObject* Object);

	/** Calls a condition; false if it is not there (or not a condition). */
	AUTOMATEDMACHINES_API bool CallCondition(UObject* Object, FName Function, float Value);

	/** Calls an action; bOn for a switch action, Value for a number one. */
	AUTOMATEDMACHINES_API void CallAction(UObject* Object, FName Function, float Value, bool bOn);

	/** The hook's explanation (AM_Info_<Name>), or a default one. */
	AUTOMATEDMACHINES_API FText GetInfo(UObject* Object, FName Function);

	/** "Tank Empty", or the function's name if Object's class has no such hook (any more). */
	AUTOMATEDMACHINES_API FString GetLabel(const UObject* Object, FName Function);
}
