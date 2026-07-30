# Camera reverse-engineering notes

The modern-camera branch deliberately separates discovery from camera writes.
The stable renderer is left unchanged while the true retail camera owner is
identified.

## Reusable tools

`tools/analyze_dungeon_camera.py` validates the supported Steam `Dungeon.dll`
by SHA-256 before disassembling it. It requires Python, `pefile` and Capstone.

```powershell
python -m pip install pefile capstone
python tools/analyze_dungeon_camera.py "<game>\Dungeon.dll" --rva 0x3860 --size 0x80
python tools/analyze_dungeon_camera.py "<game>\Dungeon.dll" --xrefs 0x1F11C0
python tools/analyze_dungeon_camera.py "<game>\Dungeon.dll" --disp 0x27C
```

The helper is read-only. Runtime field discovery is performed by the optional
camera probe in `DINPUT.dll`.

## Verified cache path

The renderer's camera cache update is `Dungeon.dll+0x3860`:

1. compare the owner stamp at `owner+0x00` with the engine frame counter;
2. invoke the dynamic callback stored at `owner+0x18`;
3. update the scene node stored at `owner+0x10`;
4. copy the node world matrix at `node+0x9C` to
   `Dungeon.dll+0x1D4110`;
5. finish the camera-dependent render caches.

`Dungeon.dll+0x1F11C0` also points at the retail camera manager used by view and
visibility helpers. The runtime probe captures this manager, the live context
owner, callback RVA, camera node, published matrix and bounded field deltas.

The first controlled `0.0.49` capture verified that the context owner and the
retail manager are the same object, and that both reference the same scene
node. Its callback is consistently `Dungeon.dll+0x30E30`. The node world
matrix at `node+0x9C` and the published matrix at `Dungeon.dll+0x1D4110`
change together. Moving the right stick during normal third-person play does
not change either matrix; moving the same stick after entering first-person
does. This proves the input path is healthy but is not bound to third-person
camera motion.

## Verified retail controller

Static analysis of callback `Dungeon.dll+0x30E30` identified the high-level
camera state machine at `Dungeon.dll+0x1044A0`. Version `0.0.50` therefore
extends the read-only probe across its complete bounded state (through
`controller+0x29F`) and records these high-value members explicitly:

- flags at `controller+0x180` (`Dungeon.dll+0x104620`);
- target/node owner pointer at `controller+0x19C`;
- first-person angular accumulators at `controller+0x1AC/+0x1B0`;
- cached camera values at `controller+0x1DC..+0x1EC`;
- camera mode byte at `controller+0x27C`;
- dynamic callbacks at `Dungeon.dll+0x104640/+0x104644`.

`Dungeon.dll+0x31270` reads retail input deltas, clamps each to `-32..32`,
accumulates them into `controller+0x1AC/+0x1B0`, and writes those angles to
the live camera node at `node+0x18/+0x1C`. `Dungeon.dll+0x30D30` maintains
camera endpoints at `controller+0x258..+0x26C` and their deltas at
`controller+0x270..+0x278`. The orbit implementation must use these retail
controller values and collision path rather than patching only the final
published matrix.

## Verified mode-3 desired-position path

The `0.0.50` trace confirmed that `controller+0x27C` is byte `3` in ordinary
third person and byte `4` in first person. The older global byte at
`Dungeon.dll+0x104679` is not the authoritative camera mode.

The mode dispatcher calls `Dungeon.dll+0x2F310` for mode 3. One branch computes
a desired camera point from the live player-coordinate pointers at
`controller+0xFC/+0x100/+0x104`, then calls:

```text
Dungeon.dll+0x2F380(controller, x, y, z, room_or_sector, update_flags)
```

`0x2F380` forwards the desired point through `0x2F340` and one call to
`0x2DEF0`. However, the other active mode-3 branch calls `0x2DEF0` directly and
therefore bypasses `0x2F380`; runtime `0.0.51` traces proved that this is the
branch used in ordinary gameplay at many camera locations.

Both branches eventually reach `Dungeon.dll+0x2DF60`, but the local vector it
receives is primarily used for sector lookup. Runtime `0.0.52` traces showed
the requested orbit yaw changing through a full circle while the published
camera returned to a fixed endpoint. Therefore changing this local vector is
not sufficient.

Version `0.0.53` detours the mode-3 dispatcher itself at `0x2F310`. Once the
right stick engages orbit, it bypasses the old rail/fixed-camera pre-check and
submits the orbit target through the same `0x2F380` call used by the normal
free-camera branch. `0x2F380` then refreshes camera state and invokes the
original resolver exactly once. The camera node and published matrix remain
untouched, and the native collision, room clipping and smoothing pipeline
remains downstream.

Right-stick input is sequenced on real source ticks. Multiple synthetic 50 Hz
render phases can reuse the resulting orbit endpoint but cannot integrate the
stick twice. Version `0.0.54` adds a real-time exponential response filter and
extends pitch below the initial retail elevation. Opening a menu or selector
and entering first person now suspend the rig without discarding yaw, pitch or
radius, so the same view resumes when ordinary mode-3 gameplay returns.
Controller loss, an invalid controller and a large player teleport still reset
the state defensively.

Version `0.0.55` also tested the retail mode-4 dispatcher callback at
`Dungeon.dll+0x31190` as a possible eye-pose helper. Runtime testing disproved
that assumption: even when the controller's visible mode field remains 3, the
callback mutates persistent body-visibility/controller state and may leave the
player model hidden after returning. It is therefore not a safe render-only
camera primitive.

Version `0.0.56` removes that call. The custom head view keeps the verified
mode-3 orbit rotation and changes only the captured camera-node translation to
the player origin plus a configurable eye height and small forward offset.
The same translation is phase-published through the existing synthetic-frame
camera path, while all player nodes and gameplay state remain untouched.

Version `0.0.57` restores the exact top-level scripted-camera ownership check
from `0x2F6D0`: `controller+0x1B8` points to an owner whose byte at `+0x8`
uses bit `0x80`. While that bit is active, the hook calls the complete retail
mode-3 dispatcher and suspends (rather than destroys) the persistent orbit.
This returns lever/reveal pull-away shots without globally returning to the
old fixed camera. SELECT now cycles modern third-person, custom head and
unmodified retail policies. Head mode additionally requires authoritative
mode byte `controller+0x27C == 3`, live gameplay and no scripted owner.

The old 320 ms positional blend was removed because its straight path from the
trailing endpoint to the eye endpoint necessarily crossed the character mesh,
causing intermittent black frames. Camera-policy changes now use a safe cut;
ordinary phase interpolation resumes at the new valid endpoint. Vertical input
in head mode uses the head-mounted convention independently of trailing-arm
pitch. Finally, the resolved position at `controller+0x1DC..+0x1E4`, produced
by the existing `0x2DEF0` resolver, feeds spring-arm contraction on the next
source tick. Contraction is immediate and extension is gradual; no framebuffer
or screen-space collision heuristic is involved.

Version `0.0.60` corrects two ownership mistakes exposed by the `0.0.59`
runtime trace. Entry into the hooked mode-3 dispatcher is authoritative; the
value of `controller+0x27C` after the retail callback is not. Fixed/rail
branches rewrite that byte to `0` or `1` while still executing the same
mode-3 path, so the old redundant post-callback test disabled orbit in most
room-camera zones. The modern endpoint is now accepted across every branch of
the verified mode-3 dispatcher.

An active `controller+0x1B8` owner is likewise diagnostic evidence, not a
sufficient reason to surrender the camera: ordinary fixed-camera zones set
the same flag. A retail reveal now requires a recent explicit interaction plus
independently moving native camera output while the player is stationary.
Physical mouse X/Y is intercepted in both DirectInput delivery models
(`GetDeviceState` and buffered `GetDeviceData`), accumulated only by the
orbit camera and removed from the events returned to the original gameplay
bindings. Mouse buttons, wheel, menus and selectors remain native.

Version `0.0.61` retires runtime camera arbitration for the single-camera
prototype. The retail callback remains only to produce a valid room and
collision candidate before the persistent third-person endpoint is submitted.
Mouse ownership is derived from native gameplay validity rather than from the
presence of an XInput controller. Native collision contraction remains in use,
but outward spring-arm probing is disabled at rest to eliminate resolver
sawtooth jitter.

Version `0.0.62` restores only a bounded authored-reveal exception. An
operate input from XInput or the physical DirectInput keyboard opens a short
arming window; native camera travel is accumulated only inside that window
and only while the player is stationary. This prevents ordinary fixed-camera
motion observed before the interaction from causing a false takeover. The
modern orbit remains the sole normal gameplay owner and resumes its preserved
orientation after the reveal.

Version `0.0.63` records that `controller+0x1DC..+0x1E4` contains both world
collision and normal angular damping. Radius feedback is therefore ignored
while orbit input is changing the ray and accepted only from a stable,
confirmed ray, with a small inward safety margin. Logs also showed that the
script owner may appear after a pre-reveal pause; an interaction now arms that
delayed owner and the retail camera is retained until its verified release.

Runtime `0.0.65` traces exposed the remaining pull-in staircase: the resolved
distance progressed through sequences such as `1400 -> 1169 -> 944 -> 746 ->
553 -> 363 -> 180`. Static analysis located the cause below `0x2DEF0`.
`0x2E800` produces the collision-resolved endpoint, after which `0x2E950`
limits movement relative to `controller+0x1F4` to `0x6E` world units per
source tick. Version `0.0.66` captures the endpoint on return from `0x2E800`
and bypasses that final limiter only when the endpoint is a finite, aligned
inward contraction of the custom spring-arm ray. It does not replace the
native collision query, and the existing contact manifold still governs
outward release.

The room traversal does not include every visible model. Static analysis of
the scene-cache update at `0x3AC00` shows that nodes with a render-resource
handle at `node+0x3C` receive an own-object world bounding sphere at
`node+0x80..0x8C`; child bounds are merged separately into the aggregate sphere
at `node+0x70..0x7C`. Version `0.0.67` therefore supplements, rather than
replaces, the native BSP query with a conservative spring-arm sweep through
stable own-object spheres. This covers visual props such as lever blocks that
the native camera query cannot see, without treating room aggregates or
animated actors as camera walls.

Runtime `0.0.68` confirms that render spheres alone are insufficient for a
modern spring arm. At the supported `-35` degree pitch, a 1400-unit arm can
place its endpoint below the player root before any sphere is encountered, and
a lever housing may publish a coarse sphere containing both the actor and the
camera. The camera now clamps its vertical endpoint above the captured player
root and retains a translated last-known-safe native endpoint when an
origin-containing prop has no usable segment entry. Diagnostics expose these
paths as `camera_floor_guard`, `camera_object_overlap` and
`camera_safe_restore`.

Version `0.0.69` replaces those coarse spheres as the final prop-collision
decision. Static analysis of `0x8B0D0`, `0x8B1E0`, `0x8C170`, `0x94C00` and
`0x94C60` recovered the native render-resource layout. The positive handle at
`node+0x3C` indexes the table at `0x10237130`; each resource exposes a polygon
count/table at `+0x18/+0x1C`, every 0x34-byte polygon stores its vertex count
and 0x10-byte reference array at `+0x28/+0x2C`, and each reference points to a
local-space vertex whose XYZ begins at `+0x04`. The implementation caches
triangulated read-only copies of those polygons and transforms them with the
node's live world matrix.

The spring arm now uses `node+0x80` only as a broad phase, then intersects the
player-to-camera ray with the actual visible triangles and stops 112 world
units before the nearest surface. Player descendants and ancestors, moving
objects and invalid resources remain excluded. This is a separate geometry
layer above the room cell/portal traversal in `0x4E760`; it covers stairs,
lever blocks and decorative meshes that are rendered but never registered as
retail camera obstacles. Diagnostics expose resource parsing as
`camera_mesh_cache` and accepted contacts as `camera_mesh_sweep`.

## Diagnostic run protocol

Set `CameraProbe=1` and `DebugLog=1` under `[Diagnostics]`. For a useful short
capture, perform each action for roughly two seconds:

1. stand still without touching either stick;
2. rotate the character with the left stick;
3. move the right stick horizontally in normal third-person gameplay;
4. enter first-person view and move the right stick horizontally and vertically;
5. leave first-person view and cross a retail room-camera transition.

The probe is sampled only on real source ticks, never on synthetic 50 Hz
passes. It is read-only and batches its file writes. The resulting
`camera_probe` records in `deathtrap_native_render.log` provide the offset
deltas needed to locate yaw, pitch, position, mode and collision-owned fields
before an orbit-camera prototype is attempted.
