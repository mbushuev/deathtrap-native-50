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

## Native mouse bindings

`Dungeon.dll` already polls relative DirectInput mouse deltas and exposes
native sources named `MOUSE_HORIZ_LEFT`, `MOUSE_HORIZ_RIGHT` and
`MOUSE_LBUTTON`. The retail binding file uses these sources for menus only.

The installer adds those sources to the retail actions in `ASYLUM/keys.cfg`
before launch. Normal horizontal motion maps to `ACTION_TURN_LEFT/RIGHT`;
Shift plus horizontal motion maps to `ACTION_TURN_FAST_LEFT/RIGHT`. Left and
right buttons map to `ACTION_ATTACK_1` and `ACTION_PARRY`.

The DLL deliberately contains no runtime action-table hook. Earlier experiments
that rewrote the live action table were stable during gameplay but could
deadlock combat or the transition back to menus. Static bindings let the
retail parser, controller and menu lifecycle own turning, attacks and parry.

## Mouse-wheel inventory bridge

The retail action table has no wheel source and no next/previous weapon action.
Version 0.0.24 therefore patches only the system DirectInput mouse object's
`GetDeviceState` vtable slot and observes `DIMOUSESTATE::lZ` after the original
call succeeds. The returned state is never modified. Detents are bounded and
queued atomically; no `Dungeon.dll` function is called from the input thread.

At the next real `Dungeon.dll+0x80600` scheduler boundary, the queue is consumed
only if a live player/gameplay context exists and the selector UI is closed.
IDs 1 through 6 are filtered by the retail inventory lookup at `+0x7BD30`; the
always-present IDs 0 and 7 follow the same rules as the retail close-combat
selector. Selection is committed through the selector's native operation at
`+0x90610`. Loading/menu wheel input expires and cannot leak into gameplay.
Synthetic render phases do not poll or consume input.

## XInput category bridge

Version 0.0.34 splits XInput polling by ownership. Gameplay is polled once at
the real `Dungeon.dll+0x80600` boundary, while a lightweight frontend poll is
active from process startup through movies, loading screens and menus. D-pad
holds write only the retail selector mode
byte at `+0x1086FC`, whose values 1 through 4 already dispatch the original
close-combat, ranged, spell, and consumable rows. Direct selection uses the
retail commit paths at `+0x90610`, `+0x90740`, `+0x7BAF0`, and `+0x7B9C0`.
Availability is checked through `+0x7BD30` first for real inventory entries.
The PC ranged row has six inventory entries; radial slot 7 stays empty and slot
8 mirrors the dedicated chalk entry drawn by the retail F2 selector. Ranged,
chalk and consumable paths require explicit A confirmation while their D-pad
direction stays held. Chalk confirmation resolves the current gameplay owner
from `[game_root+0x114]` and calls `Dungeon.dll+0x458B0`, the same routine
invoked by the original F2+8 path. It does not emulate the separate C binding,
and synthetic render phases can neither consume nor repeat the call.

If a keyboard F1-F4 row was already left open, the first D-pad press closes
that latched native selector mode and transfers ownership to the controller
selector. This prevents keyboard-to-controller switching from permanently
blocking the radial menu.

The hook at `Dungeon.dll+0x772A0` receives the retail 12-byte inventory-slot
draw state (coordinates, icon, selected/available flags, slot number and
quantity). While a controller selector is open, it copies that state, changes
only the coordinates and selected flag, and calls the original renderer. This
creates the radial presentation from native game assets without copying icon
textures or feeding overlay graphics into interpolation. The D3D11 layer no
longer paints its old debug squares.

Base controller bindings are emitted as ordinary foreground keyboard/mouse
transitions. Dedicated J/K bindings call the retail side-step actions without
the Ctrl+W diagonal collision. The right stick reaches the relative-mouse path
only in menus or while the retail first-person mode is toggled. Menu-pointer
speed is independent from first-person sensitivity; first-person look uses a
static precision curve rather than temporal filtering, avoiding extra input
latency. The retail action parser remains the sole owner of movement, combat,
menus and collision.

Pause menus keep the live gameplay pointers, so Start maintains a small
controller-only context latch rather than guessing from those pointers. Menu
pointer deltas and A-button state are merged into both legacy DirectInput input
paths: immediate `GetDeviceState` and buffered `GetDeviceData`. The pending
delta is a replaceable single sample, preventing accumulated cursor jumps
across loading screens. The frontend poll never calls `Dungeon.dll` inventory
or gameplay actions; those remain owned by the scheduler thread.

## XInput vibration output

Version 0.0.39 resolves `XInputSetState` from the same dynamically selected
system runtime as `XInputGetState`. Vibration is an output-only controller
layer: it never writes game state and is updated only from a real input poll,
not from either synthetic render phase. A right-trigger threshold transition
starts a short high-frequency attack-action envelope; a left-trigger
transition starts a lighter low-frequency block envelope. Overlapping envelopes
use the stronger value independently for each motor.

The last motor values are cached, so `XInputSetState` is called only when the
output changes. Both motors are explicitly cleared when gameplay loses input
ownership, the radial selector captures controls, focus is lost, the gamepad
disconnects, or injected controller state is released. This first stage is
action acknowledgement rather than hit detection. Damage and confirmed-impact
rumble must later originate from a proven simulation event.
Version 0.0.40 strengthens and lengthens the two envelopes after hardware
testing showed the initial conservative profile was barely perceptible.

## Original text lifetime at the higher render rate

`Dungeon.dll+0x8F4C0` draws the transient on-screen messages and decrements the
lifetime stored at offset `+0x4C` in each of the three `0x50`-byte message
entries. The retail renderer calls it once per simulation endpoint. Calling it
again for the two synthetic phases would therefore age a message three times
per source tick and make it disappear roughly three times too quickly.

Version 0.0.35 includes the complete message queue in the synthetic-render
transaction. Each midpoint may draw the current text, but its mutation of the
queue is rolled back immediately. Only the exact endpoint is allowed to age
the message, preserving the retail duration without changing the stored
50-tick lifetime or slowing any other UI/gameplay timer.

Version 0.0.36 additionally replaces only the immediate used when a new
transient message is created. `Text/MessageLifetimePercent=300` changes the
retail 50-tick value to 150 ticks. The patch validates the original immediate
before writing it, and it does not scale selector, combat, input, animation or
other UI countdowns. Values from 100 through 1000 percent are accepted.

Level-script notifications are a second, independent system. The PST path at
`Dungeon.dll+0x78150` stores six `0x104`-byte entries beginning at
`0x101F0A90`; the lifetime is the dword at entry offset `+0x100`, initialized
to 27 ticks. Version 0.0.38 adds this entire ring, including its read/write
indices, to the synthetic-render transaction. It also applies the same
configured percentage to the validated 27-tick creation immediate (81 ticks
at 300%). This is the path used by prompts such as `You need the silver key`.
