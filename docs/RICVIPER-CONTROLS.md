# RicViper gamepad controls

RicViper is an optional gamepad layout in Deathtrap Native 50 v0.0.240.
It moves spellcasting to **RB**, gives **B** a movement mode, and adds quick
healing and weapon recall to the D-pad. Thanks to **RicViper** for the design
and original implementation.

## Select the layout

Open **Setup → Native 50 → Page 2 → Gamepad layout** and choose **RicViper**.
The game saves your choice and applies it immediately. Choose **Standard**
in the same menu to return to the default controls.

On Steam Deck, keep Steam Input set to **Gamepad**. Choose RicViper in
the game’s own settings.

## Gameplay buttons

| Input | Action |
|---|---|
| Left stick | Move |
| Right stick | Look or highlight a slot in an open radial |
| **A** | Jump or climb. Use the highlighted potion or charm in its radial. |
| **B** | Hold for the movement mode described below. Press to go back in menus or cancel selection by release in a radial. |
| **X** | Interact |
| **Y** | Return the modern third-person camera behind the character |
| **LB** | Hold for cautious movement |
| **RB** | Cast the selected spell |
| **LT** | Block or parry |
| **RT** | Attack. Use the left stick for directional attacks, or hold **B** to keep the stick on movement. |
| **Start/Menu** | Open the pause menu |
| **View/Back** | Pause or resume gameplay |
| **R3** | Toggle the original first-person view |
| **L3** | Toggle the body-visible first-person view |

## Move while holding B

Start with the left stick centered. Hold **B** and tilt the stick. The
first direction selects how you move until the stick returns to its
neutral zone. **B** takes priority if you also hold **LB**.

- **Forward first.** Use your normal movement mode. You can steer by moving
  the stick to the side without returning it to center.
- **Sideways first.** Take a sidestep to the left or right using the original
  game’s side-step actions.
- **Backward first.** In third person, pull straight back to retreat while
  keeping your facing. Add sideways tilt to steer as you back away.

On the forward and backward routes, a light tilt takes cautious steps,
more tilt walks, and a near-full tilt runs. For example, hold **B** and
pull straight back to make room without turning away. Add **RT** to attack
while keeping the stick on movement, or use **RB** to cast.

## Tap or hold the D-pad

| Direction | Short tap | Hold |
|---|---|---|
| Up | Select the next available melee weapon | Melee weapons |
| Right | Select the next available ranged weapon | Ranged weapons and Chalk |
| Down | Select the next available spell | Spells and shields |
| Left | Use quick heal | Potions and charms |

The first tap after switching between melee and ranged recalls the last weapon
used in that category, if available. Tap again to cycle through the available
choices. Selecting a spell leaves both weapon choices in memory. Chalk and
Firefly are available in their radials and are skipped when cycling by taps.

Hold one D-pad direction and highlight a slot with the **right stick**. For
weapons and spells, let the right stick move outside its deadzone at least once,
then release the held direction to select the highlighted slot. Press **B**
while the wheel is open to cancel that selection and release the D-pad to close
the wheel.

In the potion and charm wheel, press **A** once per use. You can highlight and
use another item while holding the wheel open. The left stick can still move the
character while a radial is open. During an **RT** attack, hold **B** to keep the
left stick on movement.

## Set a quick-heal slot

A tap on **D-pad Left** uses the first available consumable whose name matches
a healing item. To choose a specific slot, close the game and edit
`deathtrap_native.ini`. Update `QuickHealSlot` in its existing `[XInput]`
section:

```ini
[XInput]
QuickHealSlot=0
```

`0` searches by item name. Values `1` through `8` use the corresponding slot
in the potion and charm wheel. Pick the slot that holds the item you want to use.
If no matching item is available or the chosen slot is empty, the tap has no effect.

[Return to the main controls guide](../README.md#xbox-compatible-controller)
