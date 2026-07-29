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
