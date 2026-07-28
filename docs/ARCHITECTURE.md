# Architecture

## Separation boundary

```text
DD_CD.EXE / Dungeon.dll
  |-- imports DINPUT.dll --> our native overlay --> Windows x86 dinput.dll
  |
  `-- imports DDRAW.dll  --> unmodified dgVoodoo 2.86+
                               |
                               `-- D3D11 / DXGI swapchain
                                      ^
                                      `-- minimal Present observation
```

The overlay and dgVoodoo are siblings in the process, not a proxy chain. Our
DLL never loads a renamed `DDraw_dgVoodoo.dll` and never forwards DirectDraw.
No dgVoodoo path or binary hash is compiled into the overlay. Version 2.86 is
the minimum compatibility baseline; the currently validated build is 2.86.2.

## Native rendering

Deathtrap advances simulation at its original fixed cadence and normally
presents one unique frame at approximately 16.7 FPS. The patch captures the
previous and current render transforms and issues two render-only phases at
one-third and two-thirds of the interval, followed by the exact game endpoint.

The additional phases do not advance:

- the game scheduler or engine frame counter;
- collision and player-controller state;
- AI or enemy animation state;
- Miles/Redbook audio;
- input application;
- doors, lifts, projectiles or scripted objects.

Contact-aware transform reconciliation prevents the player from alternating
between pre-collision and resolved positions while running into or alongside
geometry. Camera, player, enemies, weapons and UI use independently validated
render paths accumulated during the native-render investigation.

## DirectDraw page restoration

Rendering a synthetic phase rotates Deathtrap's legacy DirectDraw page chain.
After presenting the phase, the patch performs a second backend Flip to restore
the page orientation expected by the next real engine frame. That restore Flip
must mutate the legacy page chain without becoming visible.

The D3D11 bridge therefore suppresses exactly the DXGI `Present` issued during
the restore operation and returns success to dgVoodoo. It does not replace the
dgVoodoo renderer or swapchain.

## Surface guard

A small 24-by-14 grid of four-pixel samples is read from native D3D11 phases.
The exact endpoint is retained as a reference. A synthetic phase is rejected
only when multiple visible reference cells collapse to black or the whole
surface becomes transiently black. The exact endpoint remains scheduled, so a
rejection cannot alter simulation state.

This guard is not frame generation and has no NVIDIA SDK dependency.

## DirectInput forwarding

The game imports `DirectInputCreateA` from legacy `DINPUT.dll`. Our DLL exports
the same seven public functions and forwards them to the Windows x86 system
library. A smoke test calls the forwarded factory and requires a real
DirectInput object before a build is packaged.

## Modern mouse action bridge

`Dungeon.dll` already polls relative DirectInput mouse deltas and exposes
native sources named `MOUSE_HORIZ_LEFT`, `MOUSE_HORIZ_RIGHT` and
`MOUSE_LBUTTON`. The retail binding file uses these sources for menus only.

The optional modern-mouse layer adds those existing sources to the native
`ACTION_TURN_LEFT`, `ACTION_TURN_RIGHT` and `ACTION_ATTACK_1` bindings after
the engine has evaluated its input table. It does not synthesize keyboard
events, move the camera directly or run input during render-only subframes.
The character turns through the original player controller; the original
follow camera consequently remains behind the character in the normal game
fashion. Existing menu mouse bindings remain untouched.

The engine may rebuild its action map after control redefinition. The input
hook therefore verifies the three injected bindings once per real input tick
and restores only a missing entry. This work never runs on either of the two
synthetic native-render phases.
