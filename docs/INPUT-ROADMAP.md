# Input modernization roadmap

## Phase 1: native mouse turning

- Preserve the original menu pointer and clicks.
- Feed relative horizontal mouse motion into the original character-turn
  actions.
- Keep the original follow camera and all collision logic.
- Map left click to the original primary melee attack.
- Verify sensitivity, inversion, focus changes and native-50 compatibility.

Version 0.0.18 proved the native bindings. Direct heading and live action-table
experiments in 0.0.19 through 0.0.22 were rejected because combat and menu
transitions could deadlock. Version 0.0.23 installs only static retail-format
bindings in `ASYLUM/keys.cfg` and had no input hook. Shift plus horizontal
mouse motion selects the game's own fast-turn action while running. Version
0.0.24 adds the separate observation-only wheel path described below.

## Phase 2: wheel weapon selection (implemented in 0.0.24)

- Capture wheel detents without replacing the game's mouse device.
- Resolve the original inventory/weapon selection operation.
- Select the previous or next usable weapon directly through that operation.
- Avoid simulated F-keys and avoid opening a selector UI for one frame.

The retail action table has no wheel source or next/previous weapon command.
The proxy therefore observes `DIMOUSESTATE::lZ` without modifying the state
returned to the game. It queues bounded detents, and the real render scheduler
consumes them only during active gameplay. Available slots are checked with the
same inventory lookup used by the retail selector (`Dungeon.dll+0x7BD30`), then
committed through its native close-combat selection path
(`Dungeon.dll+0x90610`). Synthetic render phases never consume input.

## Phase 3: cursor ownership (not required)

The retail/dgVoodoo path already hides and restores the cursor correctly, so
the overlay deliberately does not take cursor ownership.

## Phase 4: configurable response (research)

- Investigate sensitivity and inversion without direct transform writes or
  live action-table mutation.
- Keep the default response linear and unsmoothed. Add a curve only if later
  gameplay testing demonstrates a concrete need.
- Keep vertical mouse-look disabled unless a later camera investigation can
  preserve framing and collision visibility.

## Phase 5: XInput (implemented in 0.0.27)

- Dynamically support modern Xbox controllers without redistributing XInput.
- Drive the original keyboard/mouse actions with configurable deadzones,
  LB-modified native side-step keys, toggled two-axis first-person look and
  optional Y invert.
- Provide right-stick pointer control, A click and keyboard fallbacks in menus.
- Map D-pad to the four native inventory groups and right stick to eight direct
  slots, rendered radially with the retail icons, numbers and quantities.
- Require explicit A confirmation for consumables and allow B/release cancel.
- Add remapping and vibration only after this layout is gameplay-tested.

### Selected layout rationale

The game does not expose a normal third-person free-camera action, so permanent
right-stick turning would fight the follow camera and remove an important menu
mouse. The default left-stick tank movement is therefore retained, with LB as
an explicit side-step modifier. Right stick is context-sensitive: menu pointer,
first-person look, or radial selection. This matches existing community layouts
while keeping every game-state mutation on a retail input/selector path.

Research references:

- [Pimp Your Dungeon! Edition Guide](https://steamcommunity.com/sharedfiles/filedetails/?id=2662812352)
  demonstrates right-stick mouse control for this game's menus and
  first-person view, but its two keyboard-action layers for slots 1-8 are too
  complex for the default layout here.
- [Decent control set up](https://steamcommunity.com/sharedfiles/filedetails/?id=3503124705)
  confirms that the game's freelook/aim mode is useful while ordinary movement
  remains action-based.
- [Steam Input radial menus](https://partner.steamgames.com/doc/features/steam_controller/radial_menus?l=english)
  documents the hold, select and release interaction used by the native radial
  inventory bridge.
