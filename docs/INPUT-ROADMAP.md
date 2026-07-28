# Input modernization roadmap

## Phase 1: native mouse turning

- Preserve the original menu pointer and clicks.
- Feed relative horizontal mouse motion into the original character-turn
  actions.
- Keep the original follow camera and all collision logic.
- Map left click to the original primary melee attack.
- Verify sensitivity, inversion, focus changes and native-50 compatibility.

Version 0.0.16 implements this phase without adding another input clock.

## Phase 2: wheel weapon selection

- Capture wheel detents without replacing the game's mouse device.
- Resolve the original inventory/weapon selection operation.
- Select the previous or next usable weapon directly through that operation.
- Avoid simulated F-keys and avoid opening a selector UI for one frame.

## Phase 3: cursor ownership

- Capture and hide the system cursor only during active gameplay.
- Release it in menus, on focus loss and while task switching.
- Restore capture without a position jump after focus returns.

## Phase 4: configurable response

- Add horizontal sensitivity and inversion to `deathtrap_native.ini`.
- Add a small deadband and optional response curve if raw testing shows they
  are useful.
- Keep vertical mouse-look disabled unless a later camera investigation can
  preserve framing and collision visibility.

## Phase 5: XInput

- Add modern Xbox controller support, deadzones and remapping.
- Preserve the same original action system used by keyboard and mouse.
- Add vibration only after stable gameplay events have been identified.
