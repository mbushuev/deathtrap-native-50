# Input rebinding research (2026-08-30)

## Goal

Add a complete, usable keyboard-binding screen without changing the accepted
XInput layout or any of its context-sensitive behaviour.

The non-negotiable ownership rule is:

- keyboard remapping changes only physical keyboard input;
- the XInput layout remains the accepted fixed Deathtrap Native 50 layout;
- D-pad inventory selection, menu navigation, gameplay, combat and camera
  input remain separate contexts;
- no keyboard binding is ever used as the definition of a controller action.

## Current production state

The production `main` branch does not yet satisfy complete input independence.
Third-person movement already enters through the native joystick axes at
`Dungeon.dll+0x51500`, but several accepted controller commands still use
fixed synthetic keyboard keys:

| XInput input | Current internal key/path |
| --- | --- |
| A | `Space` / jump-climb |
| X | `E` / operate |
| RB | `Q` / cast spell |
| R3 | held `Tab` / retail first person |
| L3 | patch-owned immersive first person |
| View/Back | stable `P` / pause-resume |
| Start and menu B | `Escape` |
| run threshold | `Left Shift` |
| LB side-step | `J` / `K` |

Controller combat is already closer to the required design. RT and the
continuously classified stick sector are published as a patch-owned combat
state and converted at the DirectInput keyboard boundary into the verified
retail combat grammar. D-pad selection, the radial wheel, right-stick orbit,
the immersive first-person toggle and vibration are also patch-owned paths.

The remaining fixed synthetic keys mean that saving different keyboard
bindings can currently break controller jump, operate, spell, first-person,
run or pause behaviour. This must be removed before exposing a public
keyboard-remapping screen.

## Rejected experiment

Local branch `experimental/action-aware-gamepad-remapping` reads the current
keyboard chords from `ASYLUM/keys.cfg` and makes XInput inject those chords.
It is deliberately not merged.

That direction is wrong for Deathtrap Native 50: keyboard changes become
controller changes, multi-key chords can overlap, and one physical controller
button can leak between gameplay, radial and menu contexts. The reported D-pad
and button regressions are therefore architectural, not tuning errors.

The branch is retained only as negative evidence and parser test material.

## Native keyboard menu findings

The retail keyboard screen is not a general action-list editor. It is an
11-key grammar editor.

- main screen routine: `Dungeon.dll+0x16260`;
- renderer/redraw routines: `+0x15B10`, `+0x15C50`, `+0x15D00`;
- current 11-key array: image RVA `0xBED48`;
- two 11-key default arrays: `0xBE688` and `0xBE6A0`;
- menu/action conversion table: `0xE1600`;
- combination synthesis table: `0xE1660`;
- action-to-menu conversion: `+0x5EE40`;
- menu-to-action conversion: `+0x5EFB0`.

Rendering, input dispatch, duplicate detection and saving all compare against
the literal count `11`. There is no scroll offset. The storage after the
11-entry array belongs to unrelated globals, so increasing the loop bound
would overwrite memory.

The 11 slots are base inputs for forward, backward, left, right, run, step,
jump, attack, cast, retail first-person and operate. The game synthesizes
run, side-step, directional attacks and directional jumps from combinations
of those base inputs. It cannot represent patch-only commands such as the
immersive first-person toggle or native-render controls.

The native serializer reconstructs `keys.cfg` from its runtime definitions.
An observed save from the retail screen replaced the user-facing keyboard
grammar but preserved supplemental mouse and `JOY_*` definitions. This is
useful compatibility evidence, but it is not sufficient to turn the native
screen into a safe long-list editor.

## Required architecture

### 1. Semantic commands

Introduce one patch-owned command model. Examples are move forward/back,
left/right, run, side-step, jump/climb, operate, cast, block, attack and attack
direction, retail first-person, immersive first-person, pause and the four
inventory-category selectors.

The command model contains no DIK, virtual-key or XInput button numbers.

### 2. Independent producers

Two producers publish command states independently:

1. Physical keyboard/mouse reads the user's keyboard binding table.
2. XInput uses a fixed table equivalent to the accepted production layout.

The gamepad table is not loaded from `keys.cfg` and is not edited by the
keyboard menu. Its current layout and behaviour remain unchanged:

- left stick: accepted native/camera-relative movement;
- A: jump/climb;
- X: operate;
- RT plus left-stick sector: directional melee grammar;
- LT: parry/block;
- RB: cast spell;
- LB plus left stick: explicit side-step;
- D-pad: context-owned radial inventory categories;
- right stick: menu pointer, first-person look or third-person orbit;
- R3: retail first-person;
- L3: immersive first-person;
- SELECT: gameplay pause/resume;
- Start: pause/menu.

### 3. Stable internal retail ABI

`ASYLUM/keys.cfg` must become the patch's stable internal wiring, not the
user-preference store. The verified action grammar remains constant. At the
DirectInput boundary, semantic command states are converted into those stable
retail inputs before the game resolves its actions.

This preserves the game's locomotion, animation, combat and collision
ownership without rewriting the live action table. Physical keyboard state and
controller command state are ORed only after both have been converted from
their independent sources.

This is the safe adaptation of the Crime Cities pattern. Crime Cities resolves
controller actions through the user's current keyboard table; Deathtrap must
reuse the context/semantic/DirectInput layering but keep its controller table
fixed.

### 4. Explicit contexts

At minimum, use mutually exclusive contexts:

- frontend/menu;
- gameplay;
- radial selector capture;
- key-capture screen.

Opening key capture must release all gameplay semantic states. D-pad never
becomes a generic joystick or keyboard direction while the radial selector
owns it. Menu A/B/Start must not leak into gameplay actions on a transition.

### 5. Persistence

Store user keyboard choices in a patch-owned file or section, separate from
`keys.cfg`, using stable command names. Install verified defaults only when
that store does not exist. Save atomically and keep one backup before replacing
an existing file.

The installer may repair the stable internal `keys.cfg`, but an update must not
overwrite an existing patch-owned user binding table.

## Menu implementation recommendation

Do not expand the retail 11-row arrays in place. Replace the keyboard controls
screen with a patch-owned screen that reuses the game's presentation and
input boundaries where safe.

Use pages rather than free pixel scrolling for the first implementation:

- a fixed number of visible rows keeps selection and hit testing deterministic;
- Page Up/Page Down, wheel and on-screen previous/next controls change pages;
- one capture row is active at a time;
- duplicate assignment is rejected or requires an explicit swap;
- `Apply`, `Cancel` and `Restore Deathtrap Native 50 defaults` are always
  visible.

The original controller-redefinition entry should be hidden or replaced with
a read-only controller-layout page. No editable gamepad bindings should be
shown in this phase.

The exact public keyboard command list should be approved before UI work. It
should contain useful modern actions and omit retail debugging/obsolete
entries such as resolution hotkeys, camera-lock experiments, Groovy/Funky and
the old camera-type toggle.

## Safe implementation order

1. Add a deterministic semantic-command model and tests without changing
   production mappings.
2. Route the existing fixed XInput layout through that model and prove output
   equivalence for every gameplay/menu/radial context.
3. Add the independent keyboard binding store and DirectInput translation.
4. Add parser, duplicate/swap, defaults and atomic persistence tests.
5. Replace only the keyboard controls screen; hide editable controller setup.
6. Test the complete current controller matrix before testing any keyboard
   remap.
7. Test keyboard remaps one command family at a time and confirm that the
   controller output trace is byte-for-byte unchanged.

No step should modify the game's live action table. The rejected
`0.0.19`-`0.0.22` action-table experiments already showed that this can
deadlock combat or menu transitions.

## First page-mechanism prototype

The first implementation deliberately pages only the eleven retail base
actions. It proves the native screen lifecycle before patch-only commands are
added:

- nine action rows remain owned by the original renderer and key-capture
  routine in the first lifecycle prototype;
- compact `<<` and `>>` navigation controls replace the upper and middle
  joystick blocks in the right-hand panel rather than consuming action rows;
- the original localized action labels, selection highlight and key names are
  reused;
- inactive rows do not draw the misleading `NOT DEFINED` value;
- page changes are committed after the current input frame and redrawn at the
  beginning of the next native input frame. The redraw resolves
  `keyboard.pcx`, passes both the path and the live background surface to
  `+0x12A0`, then calls the complete screen renderer. Calling `+0xEE20` while
  the menu is alive is forbidden because it resets global text state;
  re-entering the whole menu routine is also forbidden because its teardown
  is not re-entrant and broke the Back control. Navigation remains latched
  until click/Enter release;
- changing pages commits the visible values to a separate eleven-action
  backing array;
- duplicate keys are resolved across pages rather than only in the visible
  native window;
- leaving the screen restores the complete eleven-action array before the
  untouched retail action converter and serializer run;
- every hook is guarded by exact Steam `Dungeon.dll` prologue signatures and
  fails closed to the original one-page screen.

In parallel, accepted XInput gameplay buttons now publish semantic commands
and are translated to the stable internal retail keys only at the DirectInput
boundary. The page prototype therefore cannot redefine controller jump,
operate, spell, run, side-step, first-person or pause behaviour.

After this two-page lifecycle is confirmed in game, the backing model can be
expanded to patch-owned commands and persisted separately from `keys.cfg`.
