# Automated Machines for mod makers

Your mod's buildings can get conditions and actions in the **Automation** tab, and your code can trigger and hear **events**. There are two parts:

| Part | What it gives | Needs Automated Machines as a dependency? |
|---|---|---|
| **1. Naming convention** | Your building's own conditions and actions in its Automation tab | **No.** Without Automated Machines your functions are simply never called |
| **2. Blueprint library** | Trigger events from your code; hear every event | Yes (optional dependency) |

Nothing here lets a mod do more than it could already do: your functions decide what happens to your own building.

---

## Part 1: the naming convention

Add functions with these names to your **building class** (a Blueprint function, or a C++ `UFUNCTION()`). Automated Machines finds them by name when the game runs.

| Function name | Inputs | Returns | Shows in the Automation tab as |
|---|---|---|---|
| `AM_Condition_<Name>` | none | Boolean | a condition, **Name** |
| `AM_Condition_<Name>` | one number | Boolean | a condition with a number field |
| `AM_Action_<Name>` | none | nothing | an action |
| `AM_Action_<Name>` | one Boolean | nothing | an action with the on / off switch |
| `AM_Action_<Name>` | one number | nothing | an action with a number field |
| `AM_Info_<Name>` | none | Text or String | the explanation of `<Name>` at the bottom of the tab *(optional)* |

- **The name shown** is `<Name>` with its underscores as spaces: `AM_Condition_Tank_Empty` shows as **Tank Empty**. Use the words you want players to read; capitals as you like.
- **A number** can be Float, Double (Blueprint's "Float") or Integer. The player types it; no range, no unit.
- **`AM_Info_<Name>`** goes with the condition or action of the same `<Name>`: `AM_Info_Tank_Empty` explains `AM_Condition_Tank_Empty`. Without it the tab says "A condition / An action added by the mod of this building."
- A function whose inputs or result do not match the table is skipped, with a warning in the game log.

### Blueprint example

On your building's Blueprint (for example `Build_PressureTank`), add functions:

```
AM_Condition_Tank_Empty            -> Return: Boolean        (FluidAmount <= 0)
AM_Condition_Pressure_Above        Input: Value (Float)
                                   -> Return: Boolean        (Pressure > Value)
AM_Action_Vent                     (no input, no output)     (calls your Vent)
AM_Action_Lid_Open                 Input: On (Boolean)       (opens the lid if On, closes it if not)
AM_Info_Vent                       -> Return: Text           ("Releases all the pressure of the tank.")
```

A Blueprint function's output pin is its result: give it exactly one output (Boolean, or Text / String for `AM_Info_`).

### C++ example

```cpp
UCLASS()
class AMyPressureTank : public AFGBuildableFactory
{
    GENERATED_BODY()
public:
    UFUNCTION() bool AM_Condition_Tank_Empty() const { return mFluid <= 0.f; }
    UFUNCTION() bool AM_Condition_Pressure_Above(float Value) const { return mPressure > Value; }
    UFUNCTION() void AM_Action_Vent() { Vent(); }
    UFUNCTION() void AM_Action_Lid_Open(bool On) { SetLidOpen(On); }
    UFUNCTION() FText AM_Info_Vent() const { return FText::FromString(TEXT("Releases all the pressure of the tank.")); }
};
```

They must be `UFUNCTION()` (the mod finds them through the game's reflection data). `BlueprintCallable` is not needed. Use a return value, not an output reference (`bool& Out`).

### How Automated Machines uses them

- **Conditions** are checked **once a second**. When a condition becomes true, the event the player named next to it is triggered; while it stays true, the event is "active" (for AND). Keep them quick: no searching the whole world.
- **Actions** run when an event the player picked is triggered.
- **Everything runs on the server** (the host in multiplayer), like the mod's own conditions and actions. Your function must work there and replicate its results the way your building already does.
- **Saving:** a rule stores your function's name. If you rename or remove a function, the rules that used it are dropped the next time that building's automation is edited. Keep the names stable between versions.
- Your building gets the circuit **glow** under it while it has automation, like other machines.
- **Copy / paste** (Ctrl+C / V, middle click) copies your conditions and actions too.

### Where the Automation tab appears

The tab is added to **the game's own windows**. Your building gets it if its window is one of these (most buildings made from a game building's Blueprint keep its window):

Manufacturing (`Widget_Manufacturing`), extractors (`Widget_ResourceExtractorUI`, `BPW_ExtractorPump`, `BPW_FrackingExtractor`, `BPW_FrackingSmasher`), storage (`Widget_Storage`, `BPW_CentralStorage`, `Widget_PipeStorageTank`), generators (`Widget_Generator`, `BPW_AlienPower`), `BPW_PowerStorage`, `BPW_PowerSwitch`, `BPW_PipeValve`, `BPW_ResourceSink`, train and truck stations (`Widget_TrainStationNew`, `Widget_Train_DockingStation`, `BPW_Train_DockingStation_Fluid`, `Widget_DockingStation`), `Widget_RailroadSignal`, `BPW_Drone_Port`, signs, lights, splitters, mergers, the HUB and the MAM.

A building with **a window of its own, or no window at all**, opens Automated Machines' own **Automation window** instead (next section).

### Opening the Automation window (buildings without a game window)

`OpenAutomationWindow(Building)` opens a window with the building's Automation tab, on the local player's screen. Escape or its X closes it; it closes by itself if the building is dismantled. It returns false (and opens nothing) when Automated Machines is not researched yet, or the building has no `AM_` functions.

Call it **on the player's own game**, where the use or key press happens (for example in your `OnUse` when `byCharacter->IsLocallyControlled()`), not on the server.

**Without depending on Automated Machines** (C++), call it by name; if the mod is not installed it does nothing:

```cpp
// Opens Automated Machines' Automation window for Building, if that mod is installed.
static bool OpenAutomationWindow(AActor* Building)
{
    UClass* Library = FindObject<UClass>(nullptr, TEXT("/Script/AutomatedMachines.AMBlueprintLibrary"));
    UFunction* Function = Library ? Library->FindFunctionByName(TEXT("OpenAutomationWindow")) : nullptr;
    if (!Function || !Building)
    {
        return false;
    }
    struct { AActor* Building; bool ReturnValue; } Params{ Building, false };
    Library->GetDefaultObject()->ProcessEvent(Function, &Params);
    return Params.ReturnValue;
}
```

**With the optional dependency** (Blueprint or C++): the node **AM Open Automation Window**.

How players reach it is up to you: a key held while using the building, a button in your own window, a second use action. The player must be able to tell (your look-at text, your mod page).

Your building's game-type conditions and actions (for example a production building's Input full, Standby) stay; yours are listed after them.

---

## Part 2: the Blueprint library (optional dependency)

Add Automated Machines as an **optional** dependency in your `.uplugin`, so your mod still loads without it:

```json
"Plugins": [
    { "Name": "AutomatedMachines", "Enabled": true, "Optional": true, "SemVersion": "^1.2.0" }
]
```

| Node | What it does |
|---|---|
| **AM Trigger Event** (Event, Source) | Triggers an event as if a condition had: every machine listening to it acts. Source: your building, or nothing. The name is made uppercase. Server only; does nothing until Automated Machines is researched in the MAM. |
| **AM Get Automation Subsystem** | The mod's subsystem. Bind to its **On Event Triggered** (Event, Source) to hear every event, from any machine. Server only. |
| **AM Is Event Active** (Event) | True while one of the event's conditions is met. Server only. |
| **AM Open Automation Window** (Building) | Opens the Automation window for your building on the local player's screen (see Part 1). |

Example: a defence mod calls **AM Trigger Event** `RAID INCOMING`; players then make lights turn red and machines stop with the mod's own actions.

---

## Testing your mod

1. Build your mod and Automated Machines into the game; research **Automated Machines** in the MAM.
2. The game log (`%LOCALAPPDATA%\FactoryGame\Saved\Logs\FactoryGame.log`) has, the first time your building's tab opens:
   `LogAutomatedMachines: Mod hooks: Build_PressureTank_C: condition Tank Empty, condition Pressure Above (number), action Vent, action Lid Open (on / off)`
   and a warning for every `AM_` function it skipped, with the reason.
3. Open your building's window (or the Automation window): the Automation tab lists your conditions under **+ condition** and your actions under **+ event** / **+ action**. Pointing at one shows your `AM_Info_` text.
4. Opening the Automation window writes `Automation window opened for <building>`.
5. Each action run writes `<building>: mod action AM_Action_Vent` (and `(off)` for a switch turned off).
