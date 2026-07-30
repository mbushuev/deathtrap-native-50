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

Runtime `0.0.65` traces exposed a pull-in staircase through the mode-3 shaping
path. Later static analysis corrected the initial interpretation: `0x2E800`
is angular/vector offset shaping, not the authoritative collision result, and
`0x2E950` applies another bounded position-history stage. The actual retail
camera visibility predicate is `0x2F6D0`: it resolves the sectors of
`controller+0x1F4` and the live focus at `controller+0x264`, then calls
`0x30910`. That function sends a centre trace plus six offset traces through
the room/portal traversal at `0x4E760`. Version `0.0.75` therefore removes the
`0x2E800` return hook and invokes the verified predicate read-only for the
complete desired spring arm.

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

Runtime traces from `0.0.69` showed why an exact centre ray is still not a
camera collision volume. On the same static resource the nearest hit jumped
between faces (for example, 623, 393, 488 and 669 world units), and the ray
temporarily missed the mesh when it crossed a triangle edge even though the
camera near volume already overlapped the prop. Version `0.0.70` replaces the
final point-ray query with an exact swept sphere against the cached render
mesh. Each triangle is tested as the union of its offset face slab, three edge
capsules and three vertex spheres; the initial sphere is also checked against
the triangle closest point. The broad-phase sphere remains only a rejection
test. This supplies continuous contact across polygon boundaries and prevents
edge tunnelling without treating the empty part of an object's bounding
sphere as solid.

The first `0.0.70` run verified the geometry but exposed a separate ownership
loop. Player translation invalidated the contact anchor every source tick, the
arm extended by 30--48 units, and the same mesh immediately contracted it
again. At the same time, the raw native room resolver produced changing
inward endpoints during unobstructed follow motion; treating every such sample
as authoritative caused long radius staircases. Version `0.0.71` retains a
render-mesh contact through a 24-unit forward band and does not release it
merely because the player translated. Exact mesh contacts still pull in
immediately. Native-only endpoints no longer bypass the retail limiter on one
sample: the pre-damping value is retained for analysis and must pass the
existing multi-tick stable-surface confirmation first.

Version `0.0.75` replaces the accumulated endpoint/anchor heuristics with a
conventional pivot-to-endpoint spring arm. The pivot is the engine-maintained
camera focus at `controller+0x264` (previous focus at `+0x258`, delta at
`+0x270`), not the actor's ground/root coordinate. Every source tick queries
the full desired arm. Native room geometry is clipped by the verified
`0x2F6D0`/`0x30910` volume predicate; stable visible props are then clipped by
the existing swept render-mesh sphere. Pull-in is immediate, while release is
bounded and begins only after two complete clear samples. No query extends
past the desired camera endpoint and no historical endpoint owns collision.

The render-mesh sweep also distinguishes penetration from depenetration. If
its sphere initially overlaps a triangle, a short forward closest-point probe
blocks only motion deeper into that convex surface. Motion away from or
tangential to it is allowed, so an edge/capsule exit cannot become a one-way
trap that pins the camera inside a lever block. The pure spring-arm state and
this overlap-direction rule have standalone tests in
`camera_spring_arm_test`.

Version `0.0.76` added read-only telemetry at every stage of this path. The
problematic lever housing is conclusively identified as scene node
`0x060343A8`, render resource `12708` (24 triangles in that run). The swept
sphere hits triangle 11 and computes a safe endpoint before the camera centre
crosses its face. This rules out a missing resource and an incorrect
broad-phase classification. It does not prove that the protected sphere covers
the full visible near-plane footprint; the later `0.0.78` trace shows that the
reduced 64-unit radius does not.

The same trace exposed a separate temporal defect in the retail resolver. On
first contact the hook submitted the safe endpoint `-11144/-1500/16483`, but
the resolved and published positions remained at the previous unobstructed
endpoint `-10126/-1687/16709` until the next source tick. Static analysis of
`0x2DC80`/`0x2DE10` then identified the four-position smoothing history at
controller `+0x204`: its cached average is at `+0x20C`, and four 12-byte Vec3
samples begin at `+0x218`. This old history, rather than collision discovery,
was the source of the one-frame penetration and the following correction
jolt.

Version `0.0.77` tested a narrowly gated same-tick contraction, but its first
runtime trace disproved the assumption that the shorter endpoint returned by
`0x2F380` remained safe. For resource `12708`, the swept endpoint was
`-11151/-1414/16481`; the controller changed it to
`-11221/-1290/16646`. The latter is only about 174 units from the object's
`-11113/-1427/16648` bounding-sphere centre (radius 180), so the engine had
moved the camera off the tested segment and back inside the lever housing.

Version `0.0.78` commits the exact swept-sphere endpoint on every positively
detected contact. It atomically updates the resolved/desired positions, the
four-sample history, the live camera-node translation and the already-
published matrix. It deliberately rejects the post-`0x2F380` positional
shift while retaining the orientation that function calculated. Unobstructed
tracking, spring-arm release and authored camera reveals never enter this
path.

Version `0.0.79` addresses the remaining black sector during outward recovery
without adding a release delay. In the final `0.0.78` contact with resource
`12708`, the committed camera centre was only about 66 units from triangle 8's
lower edge. That is enough for the 64-unit centre sphere to report clear on the
next samples, but not enough to keep a visible near-plane corner outside the
mesh. The narrow phase therefore restores the 96-world-unit camera volume
used by `0.0.70` through `0.0.73`. The exact same-tick endpoint commit from
`0.0.78` remains in force, so the native post-configure path cannot move this
larger validated volume back across the surface.

The `0.0.79` runtime disproved radius as the remaining explanation: exact
source endpoints continued to pass the mesh sweep while the black corner was
still visible. The missing path was the presentation interpolator. It linearly
blended the camera-node translation at 1/3 and 2/3 between independently safe
source endpoints. During an orbit direction change this chord can cross lever
resource `12708`, despite both pivot-to-endpoint spring-arm rays being clear.

Version `0.0.80` validates that temporal chord against the same parsed render
triangles and 96-unit camera volume. If blocked, synthetic translation selects
one of the two already collision-resolved endpoints (previous at 1/3, current
at 2/3); its orientation remains interpolated. This does not alter controller
state, source endpoints, spring-arm release or game simulation. Diagnostics use
`camera_temporal_chord_guard`, which is intentionally separate from the
source-tick `camera_mesh_sweep` records.

The `0.0.80` runtime then exposed a second post-validation mutation during
outward recovery. With resource `12708` nearby, the validated endpoint
`-11015/-1376/16390` was submitted to `0x2F380`, but controller `+0x1DC` and
the published camera became `-11160/-1361/16235`. This happened on a clear
tick while the spring arm was still contracted, so the contact-only exact
commit did not run. Version `0.0.81` keeps publishing the validated radial
endpoint for the complete contracted/releasing lifetime. Normal full-radius
tracking and authored camera reveals still use the retail path unchanged.

The final `0.0.81` capture ruled out the earlier synthetic-frame failure. The
bad frame was exact (`FG OVR Off`), and the camera stayed at the same published
translation for many source ticks. Its centre was outside the 96-unit swept
sphere, but the published orientation was not radial: the camera forward axis
differed from the pivot-to-camera ray by approximately 37 degrees. In camera
space, resource `12708` sat near the side of the view footprint even though it
was clear of the centre ray.

Version `0.0.82` decodes the final published right/up basis and sweeps an
oriented 4:3 camera footprint (eight side/corner samples plus the existing
centre sphere) against stable render triangles. A pre-configure pass feeds the
spring-arm state, while a post-configure pass catches orientation changes made
inside `0x2F380` and commits any additional contraction immediately. Logs now
identify `footprint=1`, its right/up sample offset, and any
`camera_footprint_post_config` retraction.

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
