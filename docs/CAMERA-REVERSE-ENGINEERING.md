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
