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
- Miles/audio clocks (the optional music fix changes only Redbook track
  enumeration and MP3 routing at native source events);
- input application;
- doors, lifts, projectiles or scripted objects.

Contact-aware transform reconciliation prevents the player from alternating
between pre-collision and resolved positions while running into or alongside
geometry. Camera, player, enemies, weapons and UI use independently validated
render paths accumulated during the native-render investigation.

## Modern camera ownership

Version 0.0.126 gives the ordinary gameplay camera one final-position owner at
the native source tick. A bounded chase position/velocity follows the native
camera focus and produces the desired yaw/pitch orbit. Native room clipping
and render-mesh swept-sphere collision then constrain that request; inward
contraction is immediate and outward recovery is bounded. The accepted point
is submitted once through the retail configure routine so sector, floor and
orientation behavior remain native.

Synthetic render phases interpolate that accepted rigid transform only. They
do not carry an older camera point, latch a mesh face or run another follow
filter. Explicit authored reveals and first person remain separate owners. The
Arkham Asylum evidence and the exact Deathtrap transfer boundary are documented
in [`ARKHAM-ASYLUM-CAMERA-TRANSFER.md`](ARKHAM-ASYLUM-CAMERA-TRANSFER.md).

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
before launch. In untouched retail camera mode, normal horizontal motion maps
to `ACTION_TURN_LEFT/RIGHT`; Shift plus horizontal motion maps to
`ACTION_TURN_FAST_LEFT/RIGHT`. In modern third-person mode the DirectInput
proxy instead consumes only the physical X/Y deltas for camera orbit, while
buttons and wheel remain on their native paths. Menus, the selector and retail
first person always retain the original mouse stream. Left and right buttons
map to `ACTION_ATTACK_1` and `ACTION_PARRY`.

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

Version 0.0.128 feeds forward magnitude into the retail DirectInput joystick
poll at `Dungeon.dll+0x51500`. The game's own `JOY_*` bindings, deadzone/action
resolver, locomotion, animation and collision paths remain the movement
authority; W/A/S/D are not synthesized. The horizontal joystick axis stays
neutral because the retail turn action changes semantics between movement
states. Camera-relative third-person steering runs at the verified common
ground-state dispatcher `Dungeon.dll+0x82750` and submits one bounded
shortest-angle delta through the canonical dual heading writer at `+0x44DD0`
before the active state callback. Dedicated J/K bindings remain only
for LB/first-person side-step because the legacy joystick exposes a single
horizontal axis. Other controller buttons continue to use the established
keyboard/mouse bridge. The right stick controls the persistent modern camera
in gameplay and the relative-mouse pointer in menus.

The shipped `MovementTurnDegreesPerTick=30` limit corresponds to about 500
degrees per second at the original simulation rate. A half-turn therefore
converges in approximately six source ticks: fast enough to avoid a broad
running circle, but still bounded and submitted through the retail writer.

The installer also gives every directional jump a native-axis equivalent.
Camera-relative movement intentionally publishes only `JOY_VERT_FORWARDS`, so
`A + left stick` reaches `ACTION_JUMP_FORWARD` after the heading dispatcher has
steered the actor; native tank/first-person fallback retains forward, backward,
left and right jump actions. J/K equivalents preserve directional jumping while
LB or first-person side-step owns the horizontal input. Shift plus native
horizontal input likewise reaches the retail fast-turn actions.

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
action acknowledgement rather than hit detection.
Version 0.0.40 strengthens and lengthens the two envelopes after hardware
testing showed the initial conservative profile was barely perceptible.

Version 0.0.41 adds a simulation-event layer without changing simulation
state. Static analysis identified `Dungeon.dll+0x1C130` as the common damage
handler. The target's data pointer is at `target+0x2C`, and its signed health is
stored at `data+0x1030` in Q14 fixed point (`16384 == 1 HP`). The hook snapshots
health before and after the original handler and ignores calls that do not
produce a positive delta. Comparing the target with the live player pointer at
`Dungeon.dll+0x34F9D0` distinguishes damage taken and death.

Damage to a non-player target becomes confirmed-hit feedback only within 1250
ms of an engine-confirmed player downstroke or spell launch. This attribution
window keeps
ambient trap and enemy-on-enemy damage from driving the controller while still
covering the game's coarse 16.7 Hz simulation cadence. Event deadlines are
published atomically by the damage hook and consumed by the normal real-tick
XInput output path. Synthetic render passes therefore neither create nor age
rumble events. Engine-event hook failure is explicitly non-fatal to the stable
native-50 renderer and controller input layer.

Version 0.0.42 adds a second non-fatal engine hook at
`Dungeon.dll+0x1D620`. This is the retail melee attack-window evaluator, not
an input callback: it verifies the `ACTION_ATTACK_1` combat flag, reads the
current animation frame through the actor's model, and compares it with the
start/end bytes at `actor+0x10C -> descriptor+0x09/+0x0A`. A rising edge for
the live player therefore marks the weapon's active downstroke even on a
miss. The hook publishes one bounded full-motor envelope atomically; the
normal real-tick XInput path remains the only code that calls
`XInputSetState`. Confirmed damage can overlap this envelope independently.

Version 0.0.46 replaces the earlier input-adjacent approximations with two
post-validation gameplay events. The successful-block hook is
`Dungeon.dll+0x834F0`. Both retail callers first verify that the struck actor
is already in one of the game's block states and that the collision was
accepted, then `0x834F0` installs the block-impact callback and animation
`0x61`. Filtering that actor against the live player therefore identifies an
enemy strike landing on an already raised player block. LT input alone never
reaches this path.

Offensive magic feedback hooks `Dungeon.dll+0x1D210`, the retail projectile
factory used by spell IDs 15 through 21. It is accepted only when the live
player is the launching actor and the original function returns a non-null
projectile. Healing and utility actions do not create one through this path,
so they cannot masquerade as an attack spell. Both hooks publish lock-free
pending requests rather than wall-clock deadlines. The real input poll remains the sole owner of
`XInputSetState`, consumes each request, starts its complete envelope at that
moment, submits the motor command, and only then writes diagnostic telemetry.
This prevents a low-FPS frame or synchronous debug log from expiring a short
pulse before the next XInput poll.

Version 0.0.48 extends the same event-qualified design. The ranged feedback
hook is the retail projectile factory at `Dungeon.dll+0x1CEC0`; a request is
accepted only for the live player and a non-null returned projectile, so an
empty weapon or rejected fire action is silent. The common consumable
dispatcher at `+0x7B9C0` is wrapped with Q14 player-health snapshots and emits
healing feedback only when the original call actually raises health. Radial
selection publishes a short high-frequency detent only when its resolved
eight-way slot changes.

Landing feedback is observed exclusively at real scheduler endpoints. It
requires an engine contact-count transition from airborne to contact plus a
minimum measured vertical displacement, excluding ordinary wall and pipe
contacts. Large player-damage events reuse the already validated `+0x1C130`
health delta and add a longer two-motor impact envelope above the configured
HP threshold. None of these paths run in a synthetic render pass, and only the
single XInput owner submits motor commands.

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
