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

Version `0.0.82` corrects the central reverse-engineering error behind the
custom collision experiments. `Dungeon.dll+0x30910` is a traversability
predicate: non-zero means that at least one of its centre/six offset traces
reaches the focus sector, while zero means that the candidate is occluded.
The earlier wrapper interpreted the result in the opposite direction, which
is why even the degenerate `focus -> focus` query was logged as
`pivot_not_clear`.

The modern orbit now writes its candidate to controller `+0x1F4`, resolves
and writes its sector at `+0x200`, and invokes the complete retail mode-3
dispatcher. Retail `0x2F6D0` accepts a visible candidate through `0x2DEF0`;
an occluded candidate enters `0x2F750`, which calculates and publishes the
same alternate camera used by the stock game. The active path no longer
parses render meshes, runs a second spring arm, or forcibly overwrites the
controller history, live node and published matrix.

The final `0.0.82` trace identified why a collision-safe camera could still
shake at the lever-block corner. For an unchanged blocked orbit, retail
`0x2F750` selected a different valid alternate position on almost every source
tick. The trace cycled between endpoint sectors `1BDCDF7C` and `1BDCDE14`, and
the desired position jumped by hundreds of world units even though both focus
and requested orbit were constant. This is intentional randomized fallback
selection in the fixed-camera game, not a failure of `0x30910`.

Version `0.0.83` retains the first fallback endpoint that the native dispatcher
has generated and independently verifies it with `0x30910`. It stores the
endpoint relative to the camera focus, so the safe path follows the player
without becoming a stale world-space anchor. On later blocked ticks it takes
short visibility-verified steps toward the requested orbit; if a step is
blocked it retracts the retained arm in a verified short step. This gives the
camera a stable glide around protrusions instead of repeatedly invoking the
random alternate search. The cache is discarded immediately if its translated
path ceases to pass the native query.

The same version snapshots the mode-3 resolver/history block before the
untouched-retail arbitration probe. When that probe does not select an authored
interaction reveal, its state changes are rolled back before the real orbit
pass. The final camera therefore advances native history once per source tick,
while lever/switch reveals still receive the complete untouched retail result.

The final `0.0.83` runtime trace showed that retained fallback was still the
wrong abstraction. The fallback survived for over one hundred source ticks,
while the full mode-3 dispatcher transformed each clear submitted endpoint
into a different desired and resolved position. The orbit layer and the
fixed-camera alternate-placement policy therefore continued to fight.

Version `0.0.84` replaces the alternate endpoint with a deterministic radial
spring arm. A blocked `focus -> orbit` request is binary-searched with the
verified `0x30910` volume predicate for the farthest clear point on that same
ray. Contraction is immediate; release waits for consecutive clear samples
and advances at a bounded rate. The final integer endpoint is queried again,
so rounding or a portal transition cannot publish an unverified centre.

The clear endpoint is submitted directly through
`Dungeon.dll+0x2F380(controller, x, y, z, room_or_sector, 1)`. Static
disassembly confirms that this function calls `0x2F340` to refresh the desired
camera state and then invokes `0x2DEF0` exactly once. It does not enter the
`0x2F310 -> 0x2F6D0 -> 0x2F750` blocked-ray alternate search. Authored reveal
shots still use the untouched full dispatcher after the existing interaction
arbitration accepts ownership.

The rejected `0.0.85` experiment proved that the final native result cannot be
replaced by writing an independently validated position to controller history,
the camera node and the published matrix. Although that stopped the tested
lever block, it let the camera cross ordinary walls and floors and decoupled
translation from native orientation/static-camera state. The experiment was
reverted completely.

Version `0.0.86` recovers only the useful static-mesh classification and sweep.
`0x2F380` first produces the complete native position and orientation. If that
published position does not intersect a qualified large stable render mesh, it
is retained byte-for-byte. If it does intersect one, the resolver/history
block is restored transactionally and the mesh-safe target is submitted again
through `0x2F380`. Up to three bounded native passes may converge; failure
restores and republishes the ordinary `0.0.84` result. No controller history,
camera node or published matrix is directly overwritten.

The user run of `0.0.86` produced 544 prop-veto failures and no successful
correction. Every three-pass sequence republished exactly the same camera
position. This is explained by the native code rather than by another
collision threshold: `0x2DEF0` pushes the resolved point through the
four-sample ring at controller `+0x204`, while `0.0.86` restored that ring
before each retry. It therefore made the retry mathematically identical to the
first rejected pass.

Version `0.0.87` keeps one pre-contact transaction snapshot but no longer
restores it between prop-safe retries. It allows one ordinary native configure
followed by at most four shorter native configure passes, enough to replace
every entry in the verified four-sample position ring. Each published result
is swept again before acceptance. If the native result still intersects the
qualified mesh after the bounded history replacement, the original snapshot
and ordinary `0.0.84` result are restored. The path still performs no direct
writes to controller history, the camera node or the published matrix.

The `0.0.87` runtime rejected that mechanism as well. Across 714 exhausted
contacts, all five publications within each source tick were identical.
`0x2F380` does not progress the visible camera history when repeated before
the engine/source tick changes.

The user then identified the missing semantic distinction: the remaining
offending blocks translate or extend during gameplay, while stable static
blocks are handled reasonably. The render-mesh path had required own-object
bounds motion of at most 16 units between adjacent scene snapshots. A moving
trap could therefore be present in the render tree and still be excluded
before its triangles were tested.

Version `0.0.88` recognizes a large kinematic mesh by invariant render
resource, stable bounds radius and basis, plus equal world-matrix and
own-bounds translation. Motion beyond two integer units excludes snapshot
rounding noise. The existing two-axis 192-unit span rule continues to reject
thin levers. Once observed, the node/resource pair remains kinematic until a
scene reset, including after the block reaches its final stationary position.

Static meshes and native room geometry continue to keep the untouched single
`0x2F380` result. A positive swept-sphere contact with a latched kinematic
block is the sole exception: the shorter point is on a prefix of the already
native-resolved focus-to-camera segment and is committed in the same source
tick so the block can depenetrate the camera. This is not the rejected
`0.0.85` global publication model; clear tracking, static contacts, recovery
and authored shots never enter the direct-write path.

The immediate `0.0.88` user run disproved the gate. It identified 18 other
moving scene nodes but committed zero camera corrections. At the same time,
the post-native triangle sweep already reported 769 unsafe publications,
including 314 contacts with resource 13676 and 64 with 13678. At contact those
target block nodes had zero adjacent-snapshot bounds motion, so movement
history could not tell them from stable props and suppressed every safe point.

Version `0.0.89` instead adopts the two-phase architecture demonstrated by the
open TombEngine camera: room/LOS collision is resolved first, then collidable
items and static meshes are handled as a separate camera push-out phase.
Deathtrap Dungeon's `0x2F380` remains the sole room/wall/floor/orientation
resolver and is called once. Its actual published endpoint is then swept
against the qualified render meshes. A positive contact alone authorizes the
shorter point on that already native-resolved radial segment.

Unlike 0.0.85, no clear or ordinary camera tick is overwritten. Unlike the
older pre-configure mesh pipeline, the sweep observes any lateral/vertical
shift made by `0x2F380`, so recovery is revalidated after native publication.
Moving bounds are no longer discarded; same resource and stable radius are
still required, player nodes are excluded, and the two-axis 192-unit test
continues to ignore thin lever geometry.

The user-visible `0.0.89` result established the next defect: blocks were
recognized, but the camera was repeatedly pushed rather than held at contact.
The trace shows why. A post-native commit reduced the radius to 657.9, then the
pre-configure spring saw no native-room obstruction and released through
721.9, 785.9, 849.9, 913.9 and 977.9. The native publication then crossed
resource 13676 again and the post pass committed it abruptly to 768.0.

Version `0.0.90` promotes the already verified scene-mesh sweep into the hard
spring-arm query. The full desired orbit is tested before
`ResolveThirdPersonSpringArm`; its mesh-safe endpoint is combined with the
native room endpoint by nearest radius, and either contact keeps the spring's
blocked state active. This prevents clear ticks and outward release while the
same desired ray remains obstructed. The post-native sweep remains necessary,
but only to reject a lateral or vertical `0x2F380` shift that crosses the mesh
after the spring has selected a safe radial endpoint.

The `0.0.90` run confirms that persistent mesh ownership improves block
contact, but exposes two native-volume edge cases. Four consecutive
`pivot_not_clear` samples returned `clip_failed`; because the untouched retail
callback had already run, each early return left its fixed-camera candidate
published for that frame. At the end of the run, a thin lever was not
classified as a mesh blocker (`blocked=1/0`, resource zero), but the native
volume boundary alternated between safe radii 182 and 311. The spring followed
each outward sample by 64 units and immediately contracted on the next inward
sample, producing a stable 182 -> 246 -> 182 loop.

Version `0.0.91` addresses the measured failure modes without broadening
direct camera writes. If the focus footprint itself is temporarily blocked,
the native clipper samples farther points on the same complete ray and uses
the farthest proven-clear point as the binary-search lower bound. If no such
sample is available, the preceding modern-camera result is translated by the
focus delta and republished through the ordinary `0x2F380` plus scene-mesh
pipeline, rather than exposing the retail fixed-camera frame. A still-blocked
boundary must also provide three consecutive outward samples before the
spring releases; inward contraction remains immediate.

The `0.0.91` run proves both changes execute: the only clip failure was
preceded by `camera_native_spring hold` and ended with `held=1`, while the old
182/311 hard-boundary alternation no longer released the spring radius.
However, the final lever-area contact exposed a separate presentation cycle.
The compact housing resource 12708 (bounds radius 180) remained a qualified
mesh blocker even though the adjacent handle resource 12709 was correctly
excluded. The spring stabilized near radius 38, but `0x2F380` alternated an
unsafe 211-unit publication with a 65-unit mesh-safe endpoint. There were 183
post-native commits to resource 12708 and 732 temporal-chord guards; 167
commits selected the exact same safe target. Camera snapshots therefore
alternated by about 147 units while the player and target were stationary.

Version `0.0.92` separates small-prop filtering from large-mesh presentation
ownership. A scene node must now have a bounds radius of at least one complete
camera diameter (192 units) in addition to the existing two-axis extent rule.
This ignores resource 12708/12709 as compact lever geometry while retaining
the observed 310-, 551- and 1136-unit blockers. For every remaining qualified
mesh contact, a presentation latch records the post-native safe endpoint,
normalizes both existing history snapshots once on acquisition, and applies
the current safe target to every newly captured camera snapshot. It persists
through one missing sample, clears after two complete clear rays, and clears
immediately for scripted cameras or scene-history resets.

The `0.0.92` run rejects the bounds-radius rule. It excluded the intended
lever housing resource 12708, but also excluded 106 other resources, including
visible geometry with bounds radii 185 and 190. The run ended with static-block
penetration and 93 D3D11 black-phase rejections. Resource 9997 exposed the
second, independent defect: its qualified 257-unit mesh twice reported an
initial swept-sphere overlap (`contact=0`). The radial spring interpreted that
topology as a safe radius of zero and published the camera at the focus,
placing the near plane on the blocking polygons.

Version `0.0.93` removes the bounds-radius exclusion and returns to the
intrinsic two-axis extent classification. Ordinary render-mesh contacts still
shorten the complete pivot-to-camera arm. An inward contact that already
contains the pivot is no longer represented as a zero-length radial arm.
Instead, the pivot is transformed into the mesh's oriented local bounds,
expanded by the 96-unit camera radius, and pushed through the nearest
horizontal face. Ties select the side of the previous camera endpoint for
continuity. The resulting non-radial point must pass the native room-volume
query before the contact-only publication and presentation latch may use it.
This follows the maintained Tomb-camera separation between room LOS and
post-room object depenetration without invoking Deathtrap's randomized
fixed-camera fallback.

The `0.0.93` run proves the initial-overlap pushout is not the cause of the
new long-lived black/intersecting view. At resource 13676 the pre-configure
mesh sweep selected the current safe point `-8198/-1540/16942`, but the native
history still published the older point `-8229/-1646/17607`. The presentation
latch incorrectly stored that older publication even though no post-native
mesh contact had validated it. Subsequent source ticks computed progressively
shorter safe points while every captured camera snapshot remained frozen at
the old absolute coordinate. Player/focus movement then increased the
camera-player distance until a later release produced a multi-thousand-unit
jump. This matches the user-visible internal room intersection and black
regions that clear after rotating away.

Version `0.0.94` makes the presentation contract follow the collision phase
that actually proved safety. A pre-configure mesh contact latches the current
native-volume-validated submitted endpoint; a positive post-native correction
latches its exactly committed final endpoint. The latch also records the
camera focus and translates its target by the focus delta during the one
intentionally tolerated missing-contact sample. It can therefore neither
retain an unverified history point nor become a stale world-space anchor while
the player moves. Native walls, floors, orientation, overlap pushout geometry
and authored-camera arbitration are otherwise unchanged.

The `0.0.94` run confirms that fix: moving-block targets advance with the
focus, the latch releases normally, and the former long-lived frozen camera
does not recur. The remaining screenshot is a separate near-pivot topology.
At a grazing angle on resource 13676, the safe radial radius contracts through
41, 23, 3 and 1.4 units. Adjacent initial-overlap samples choose nearest-face
pushouts of only 9, 17, 41 and 42 units. All are geometrically outside the
96-unit expanded surface, but they place the camera centre effectively on its
pivot and expose an unusable near-plane/visibility view.

Version `0.0.95` uses the already established 120-unit minimum usable camera
distance as a topology boundary rather than clamping through the blocker. If a
radial contact or nearest-face depenetration would fall below that distance,
the expanded object OBB supplies the other horizontal face in the direction
of the requested orbit. When the pivot is already outside one expanded face,
that supporting coordinate remains fixed and the escape path slides along the
outside of the box. A contained pivot selects the requested-side face directly.
The endpoint must remain within the requested arm and still passes the native
room-volume validation before publication.

The complete `0.0.95` run confirms that the old near-pivot collapse is gone,
and the user accepted the remaining block behavior as an adequate checkpoint.
Its final no-mouse running trace exposes an independent native-room recovery
cycle. Orbit input is exactly zero and yaw/pitch remain fixed at
`-46.49/33.60`, while the native volume repeatedly contracts the arm from
about 1400 units to 762, 553, 542 and 665 units. The arm then regrows by 64
units on each source tick and reaches the next adjacent wall/BSP segment at
full length, producing another immediate contraction. This is neither a
retail-camera takeover nor a focus-anchor error; it is a safe but overly fast
native-only extend/retract sawtooth.

Version `0.0.96` preserves the authoritative immediate inward result. It
records whether the current contraction is owned only by the native
room/portal query or by an exact render-mesh contact. While the player is
moving and orbit input is idle, native-owned outward samples require eight
consistent source ticks and then recover by 12 units per tick. Manual orbit,
stationary recovery and render-mesh blockers retain the normal 2/3-tick,
64-unit profile. Thus the change cannot allow a new wall crossing: only the
already-clear outward direction is delayed.

The `0.0.96` run rejects that policy. Its one-tick locomotion classification
switches the recovery step between 12 and 64 units 20 times in 313 spring
records. The slower profile also keeps low negative-pitch contractions alive
long enough for the published point to reach 73 units below the player root.
Most importantly, the trace shows `0x2F380` repeatedly publishing one exact
world coordinate while focus and submitted move. Its average at `+0x20C` and
four samples at `+0x218` are an absolute-world history ring, not offsets from
the live focus. This explains the apparent fixed-camera catches without any
script takeover or orbit input.

Version `0.0.97` restores the 0.0.95 spring timing. Before the one normal
`0x2F380` call, an input-idle modern-camera tick translates that complete
contiguous history ring by the exact current-minus-previous focus delta.
Translation is limited to ordinary deltas at or below 512 units; teleports and
manual orbit do not use it. No published matrix is forced. Native room, floor,
sector and orientation processing therefore remains downstream and
authoritative while its smoothing state becomes focus-relative.

The `0.0.97` trace confirms that the history rebase removes the main
world-anchor regression, but it also isolates a later discontinuity. Across
619 `camera_native_mesh_pushout` records, consecutive samples with an unchanged
focus-relative orbit still contain published-position changes of 500--1,052
units laterally and up to 292 units vertically. Several happen while both
native and mesh obstruction flags are clear. These are valid-placement branch
changes inside `0x2F380`, not changes in spring radius, input or authored-camera
ownership.

Version `0.0.98` leaves those exact native endpoints intact and smooths only
the captured 50-Hz presentation camera. The previous displayed endpoint first
follows the live focus delta, then advances toward the native target with
separate horizontal and vertical speed bounds. Before presentation, the old
endpoint and candidate focus rays are checked by `0x30910`; the old-to-new
temporal chord is checked by the same predicate and by the qualified render
meshes. An unsafe previous endpoint causes an immediate native cut, while an
unsafe intermediate chord holds the previous safe endpoint. Manual orbit,
script takeover, teleports and the exact dynamic-block presentation latch
bypass the follow layer.

The first `0.0.98` run rejects the one-tick manual-input gate. The fresh log
contains 146 `camera_follow state=SMOOTH` records, no `HOLD` records and one
real `HARD_CUT`, so a blocked presentation chord is not the reported freeze.
Instead, yaw/pitch continue to change from physical mouse packets while many
follow sequences repeatedly restart at `smooth=1`. DirectInput relative
motion is accumulated asynchronously, and a source tick with no new packet
does not mean that manual orbit has ended. The renderer therefore alternated
exact manual rotation with position/rotation follow on the empty ticks. Near
a dynamic block, the same alternation occurred around presentation-latch
release boundaries and looked like a stuck orbit followed by a jump.

Version `0.0.99` gives manual orbit explicit temporal ownership. Render-only
follow remains disabled for four original 60-ms source periods after the last
mouse or stick orbit sample. Every new packet restarts that quiet window.
Collision, the native wall/floor result, mesh sweeps, overlap escape and the
dynamic-block presentation latch are unchanged.

The `0.0.99` run confirms that the remaining black/stuck sequence is inside
mesh contact, not presentation follow. Its version-tagged segment contains
208 latch acquisitions but only 38 clear releases. There are 170 direct owner
switches without a clear interval; adjacent targets differ by as much as
1093 units, with repeated 900-unit switches between separate nodes using
resource 13676. Every switch created a new generation and normalized both
presentation-history snapshots, turning competing valid faces into visible
hard jumps.

The same run proves that the post-native safety pass can undo the earlier
0.0.95 near-pivot escape. Nine exact mesh commits are below the established
120-unit usable distance: 88.5, 36.2, 17.5, 16.2, 21.3, 0, 108, 15 and
10 units. At the 10-unit case, the pre-configure phase had already selected a
792-unit expanded-OBB escape, but `0x2F380` shifted the publication onto a
shorter intersecting segment. The post-native sweep then rejected its own
longer escape because it lay beyond that shifted segment and committed the
10-unit fallback at the player pivot.

Version `0.0.100` enforces the existing usable-distance invariant in the
post-native phase. A positive mesh correction below 120 restores the submitted
endpoint that already passed the complete mesh-arm and native-room checks;
the near-pivot endpoint is never committed or latched. Presentation contact
also becomes a stable manifold: when a different mesh node reports contact,
the focus-relative existing target is retained if it remains outside every
qualified expanded mesh/triangle volume and passes the native room query.
Only a genuinely invalid old target authorizes a new latch generation and
history cut.

The `0.0.100` run confirms both protections, but exposes a delayed-publication
release loop. All 81 successful minimum-distance restores kept exact commits
at or above 120 units, and the earlier multi-node owner-switch storm is absent.
However, the latch released 88 times; 73 releases reacquired the same resource
within 8--10 log records, including 72 repetitions on resource 12613.

The controller trace explains the delay. After a contact commit, the published
matrix can remain at the safe latch point for two source ticks while
`0x2F380` advances `controller+0x1DC` toward an unsafe mesh point. On the
second tick, the old release test observed only the still-safe published
matrix, counted a second clear sample and disabled the latch. The pending
resolved point became published on the next tick, hit the same mesh and
reacquired the same target.

Version `0.0.101` snapshots the raw resolved position immediately after the
single `0x2F380` call and before any contact-only exact correction. While a
mesh presentation latch is active, that pending native candidate is swept
against the same qualified render meshes. A hit resets clear evidence and
retains the existing focus-relative target, provided that target still passes
the native room query, minimum-distance rule and current mesh occupancy test.
Only two samples for which both the current publication and pending native
candidate are clear may release the latch. A pure state test covers the
observed clear/unsafe/clear sequence.

The first `0.0.101` run did not exercise the new predictive HOLD path, but it
did isolate a separate floor failure. At the end of the run the player root
was `Y=-1800`, while the desired orbit reached `Y=-1913` and the accepted
native/mesh publication remained at `Y=-1836` (with a worst observed sample of
`Y=-1872`). Both collision layers therefore accepted a camera centre below
the actor's floor contact. This is not a mesh-latch release failure: the
desired spring arm itself has no lower safety envelope.

Version `0.0.102` restores that invariant before either collision query. The
desired endpoint may fall at most 240 units below the controller focus and,
when the preceding render snapshot belongs to the same nearby player, remains
at least 96 units above the captured player root. The clamped desired endpoint
still passes through the complete native room predicate, deterministic
spring-arm resolver and scene-mesh sweep; no final camera matrix is written
directly.

The same review found a scene-boundary ordering defect. `CaptureScene`
previously applied the old mesh presentation latch before
`HookRenderPresentWait` compared scene roots. On a root change, that modified
snapshot became the new interpolation history even though the exact native
frame was rendered, allowing the first synthetic frame in the new location to
inherit an old-room camera. Version `0.0.102` captures raw state, detects the
boundary first, clears render-only latch/follow state and seeds the new history
without applying any custom camera transform.

The `0.0.102` runtime confirmed that the floor envelope works, but isolated
three presentation-ownership failures. First, the mesh presentation latch
changed only camera translation while the native controller continued to
change orientation during manual orbit. That combination geometrically rotates
the view around a retained collision point instead of the player. Second,
render-only follow could trail a collision-contracted spring arm until its
carried endpoint became invalid, then take the logged `previous_blocked`
hard-cut path. Third, the D3D11 guard rejected corrupt midpoint phases but
accepted every exact phase, including same-root collision frames with the same
large newly-black signature.

Version `0.0.103` separates those owners. A complete manual-orbit gesture,
including DirectInput packet gaps, bypasses the translation-only latch while
the exact native/contact path remains active. Presentation follow does not add
a second delayed camera endpoint while the native spring arm or mesh contact
owns contraction. Exact-phase black-region rejection is enabled only during
an active modern-camera collision; loading screens, authored reveals and
ordinary dark rooms remain exact native presentation. Present diagnostics now
include the source tick and distinguish `midpoint` from `exact_camera`.

The `0.0.103` runtime disproves exact-frame rejection as a safe presentation
strategy. It rejected 276 native exact frames and 564 midpoint phases. Near the
stair contact, `exact_camera` rejection became nearly continuous through tick
1531, visibly freezing the complete rendered game even though simulation and
camera updates continued. Version `0.0.104` removes exact rejection entirely.
The corruption detector is again fail-open for every native exact frame and
may suppress only synthetic midpoint phases. Source-tick correlation remains
in midpoint diagnostics. The two independent `0.0.103` ownership changes are
retained; notably, the same run recorded zero presentation-follow hard cuts.

The `0.0.104` door trace then exposes a deterministic native-history
two-cycle. With a stationary focus at `-1251/400/14740` and qualified
pre-contact on resource 12613, the final safe submitted endpoint remains
`-1682/400/15171`. On alternating ticks, however, `0x2F380` publishes
`-1527/552/15261`; post-native mesh collision restores the submitted endpoint
and rewrites history. The next tick publishes the restored endpoint without a
post-native hit, so no rewrite occurs and the intermediate enters the history
again. Camera probe therefore alternates the two positions by exactly 235
units for 18 consecutive samples. This is neither authored-camera takeover nor
scene transition; both diagnostic counts are zero.

The same contact generation retains an older target `-776/592/14845` across
adjacent node ownership because it remains independently safe. Once manual
input grace ends, that stale face can reappear 639--850 units from the current
contact result.

Version `0.0.105` treats qualified pre-contact plus an existing latch as one
continuous owner. When the submitted endpoint is minimum-distance usable and
clear of current scene objects, a post-native clear intermediate still pins
the submitted point into the native position history. During manual orbit, a
usable incoming face may replace the retained adjacent-node target; the latch
is still not rendered until input grace ends. Native walls/floors, first
contact without an established latch, unsafe endpoints and authored cameras
do not enter this pin path.

The `0.0.105` pillar run confirms that the clear-half pin works, but exposes
the other half of the same ownership cycle. The pre-native complete-arm sweep
continuously selects resource 11432, while the position emitted by `0x2F380`
periodically intersects adjacent resource 12613. The post-native phase then
commits a different valid escape on 12613 and contracts the spring to that
second solution. On later ticks the pre-native 11432 endpoint is pinned, but
the presentation latch retains the older 12613 target as an independently
clear adjacent-node face. The exact camera and visible camera consequently
have different owners; as the player moves, the latch remains stuck to the
pillar-side target.

Version `0.0.106` makes the established pre-native contact authoritative for
the complete continuous-contact transaction. Its submitted endpoint has
already passed the full requested-arm mesh sweep and native room validation.
It is therefore committed on both post-native-clear and post-native-contact
ticks. If the post phase temporarily selected another mesh escape, spring
radius and release state are restored to the submitted endpoint too. The
presentation latch transfers to that same pre-native resource and endpoint
inside the existing contact generation, so the switch neither retains the
wrong face nor creates a history-normalizing cut. First contact, native-only
walls/floors, unsafe endpoints and authored cameras retain their prior paths.

The `0.0.106` run removes the adjacent-resource ownership defect completely:
208 continuous pins succeed, none fail, and the presentation latch records no
`RETAIN` owner switches. The remaining pillar jolt is inside resource 11432's
near-pivot escape itself. With focus and player stationary, the same expanded
OBB alternates between opposite axis-0 faces at `-1379/400/14725` and
`-421/400/14725`, a 958-unit exact jump. The face was selected from the
instantaneous requested orbit direction, so crossing an angular tie could
teleport the camera to the other valid side without contact ever clearing.

Version `0.0.107` uses the preceding accepted source camera as the reference
for both contained-pivot and grazing near-pivot usable-face selection. A
continuous contact therefore remains on its current expanded-OBB face until
the desired arm becomes radially usable and releases naturally around the
object. The previous point is only a side selector; every generated endpoint
still passes the existing arm bounds and native room validation. Ordinary
radial contact, walls/floors, spring timing, mesh classification and authored
camera ownership are unchanged.

The `0.0.107` run rejects fixed-face retention. During resource 12613 contact,
the requested 1400-unit orbit travels around the player, but 248 successful
continuous pins keep returning exactly `-167/400/15246`. The focus remains at
`-545/400/14868`, the pre- and post-native phases agree on the same resource,
and the latch has no retain/release cycle. The camera is therefore stuck by
the geometric escape point itself: retaining a face normal retained one
absolute point rather than a surface on which the orbit could move.

Version `0.0.108` replaces contained-pivot face-normal selection with a
continuous horizontal ray exit. In OBB-local space it finds the first expanded
face reached by the requested orbit direction and continues outward on that
same ray until the existing minimum usable distance is met. Orbit direction
is preserved, so the endpoint moves continuously around faces and corners.
If the pivot already lies outside a horizontal face, this operation refuses to
route it back through the box and falls back to the preceding supporting-face
slide. Native room validation and the requested-arm distance bound remain
unchanged.

The final `0.0.108` trace exposes a separate delayed-publication loop at
resource 12613. With focus and orbit stationary, the camera repeatedly follows
the same three-source-tick sequence: post-native contact commits the safe
`-580/400/14804` point; two apparently clear samples release the presentation
latch and expand the spring from 140 to 204 units; then the already queued
raw desired position `-371/400/14830` is published inside the resource and
reacquires the latch. The trace contains 107 acquisitions and 104 clear-ray
releases. The pre-native requested arm remains clear throughout, so this is
not a ray-exit or object-classification failure.

Version `0.0.109` validates every pending native position before treating a
post-configure tick as clear: the raw desired point, resolved point, cached
history average and all four position-history samples. If any of them still
intersects a qualified scene mesh, the existing focus-relative safe latch
target is retained and committed back to the complete native history. This
keeps spring state, exact publication and presentation on one endpoint until
`0x2F380` naturally produces a fully clear pending path. The existing clear
release then resumes; no extra timing threshold is introduced.

The final `0.0.109` trace identifies a different persistent post-only contact.
At focus `-657/400/14859`, the requested orbit and contracted submitted point
move continuously around the player and the complete pre-native mesh sweep
reports no blocker. Nevertheless, the native resolver republishes its old
near-pivot point `-401/400/14814`; the post-native pass classifies it against
resource 12613 and commits that exact same point on every source tick. Camera
translation is therefore fixed while orientation follows the changing orbit,
which appears as rotation around an invisible off-player centre.

Version `0.0.110` extends established continuous-contact ownership to this
post-only stale-history case. After first contact creates a qualified latch,
the final submitted point is rechecked with a complete focus-to-camera mesh
sweep. If that arm is clear and the point already passed native room-volume
validation, it replaces the stale post-native point in exact publication,
spring state and presentation. First contact still uses the collision-derived
escape, and a genuinely blocked submitted arm cannot take ownership.

The `0.0.110` elevator run confirms the post-only anchor is no longer the main
failure, but shows severe radial contractions on mesh-corner transitions.
While running around the lift, adjacent resources 12619 and 12613 can change
ownership as the requested orbit turns. Individual source ticks contract from
`671.8` to `131.2`, `343.5` to `129.2`, and `331.8` to `131.4`. Each new radial
arm is genuinely clipped, but the preceding focus-relative camera arm may
still be completely clear; discarding it creates the visible camera-to-player
pop and subsequent spring expansion.

Version `0.0.111` adds a verified corner detour for established mesh contact.
When a new mesh-blocked orbit appears but native room geometry is clear, the
preceding exact camera point is translated by the player's focus motion. It is
retained only if its distance is usable, it does not exceed the requested arm,
the native seven-trace volume query accepts it and a fresh complete scene-mesh
sweep is clear. That point then enters the existing unified commit and latch
path. A wall/floor obstruction or an actually blocked preceding arm continues
to contract immediately.

The short `0.0.111` rerun shows that this detour works 15 times but starts too
late in the remaining failure. At the first resource-12613 encounter, the
camera is already committed to radius 126.6 or 241.2 before an established
latch exists. On the next tick it contracts to 121.7 or 162.1. Subsequent
`camera_mesh_corner_detour` records then preserve that already collapsed
radius correctly, so the visible camera still remains at the character's back.

Version `0.0.112` adds a first-contact tangent slide. The blocking triangle's
world-space plane is known from the exact mesh sweep. The preceding camera
direction is projected onto that plane, producing the continuity-aligned
tangent with the preceding radius clamped to the desired arm length. The
complete candidate arm must pass both the scene-mesh sweep and native
seven-trace volume query. A valid tangent immediately owns exact publication,
history, spring state and latch even on first contact. Only the tangent aligned
with the previous direction is attempted; the opposite tangent is not used, so
failure cannot teleport the camera across the player. If no valid slide exists,
the existing collision-derived contraction remains authoritative.

The fresh `0.0.112` run exposes a different lifetime error in the preceding-
arm detour. At the end of the run the desired 1400-unit orbit continues moving
around stationary focus `-1103/400/14738`, but five consecutive resource-11432
contacts publish the identical verified previous point
`-1379/400/14738` at radius 276. Earlier runs contain longer repetitions on
resources 12619 and 12613. The obstruction tests are succeeding; the problem
is that their safe result has no geometric progress rule and therefore becomes
an indefinite camera anchor.

Version `0.0.113` treats the previous point only as a contact-acquisition seed.
Once the presentation latch proves continuous mesh contact, it captures the
desired orbit displacement between the preceding and current source ticks,
projects that displacement onto the exact blocking triangle plane, and adds
the tangential step to the preceding clear camera arm. This is the usual
velocity projection used for surface sliding: it advances continuously with
orbit input and cannot select an unrelated opposite tangent from the complete
desired arm. The candidate is still bounded by the requested distance and
must pass both a complete scene-mesh arm sweep and the native seven-trace
volume query. If it fails either test, the already verified previous point is
the fallback; first contact retains the `0.0.112` continuity projection.

Version `0.0.114` restores the verified retail first-person transition rather
than reviving the rejected custom head camera. Physical Tab remains bound to
`ACTION_1ST_PERSON_VIEW`; R3 toggles an injected Tab-down state. The
authoritative controller mode at `controller+0x27C == 4` is now recognized as
gameplay by the XInput camera watchdog, so it cannot be mistaken for a menu
after mode-3 callbacks stop. While mode 4 is active (or an R3 transition is
pending), the DirectInput proxy returns physical mouse axes to the game, the
right stick supplies bounded native relative-mouse deltas, and modern orbit
input is zero. Returning to mode 3 resumes the preserved modern orbit.

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

## Verified native player-heading path (0.0.116)

Static analysis of the supported `Dungeon.dll` identifies a narrow movement
integration point that does not mutate the live action table or player
position:

- the ordinary locomotion callback at `0x7E530` selects one of its native turn
  values through `player+0x154` and calls `0x44EA0` at the sole xref
  `0x7E5BD`;
- `0x44EA0` first calls `0x44DD0`, which adds the selected delta to the Q10
  heading at `[player+0x10]->+0x1C` and mirrors heading plus the native offset
  into the engine render/collision orientation;
- its second step at `0x44E30` derives native body lean from the same selected
  turn value;
- the engine's own shortest-angle helper at `0x93D60` confirms a 1024-unit
  full turn and the `[-512, 511]` wrapped-delta convention.

Version `0.0.116` hooks only `0x44EA0`. While camera-relative XInput movement
is active, it temporarily substitutes a bounded desired-turn delta through the
already selected `player+0x154` source, calls the complete original function,
then restores that source. The player coordinate, speed, animation state,
collision solver and action table are never written. The hook validates the
live UI owner and restricts the source pointer to the player object's bounded
input-field range; any failed validation falls back to the preceding tank
mapping.

## 0.0.116 runtime result and 0.0.117

The first gameplay run calculated camera-relative target headings correctly,
but logged zero successful `xinput movement heading` calls. The symptom was
that only one left-stick direction moved forward: that direction happened to
already match the character's current heading. The other directions released
both retail turn actions while also withholding forward movement outside the
configured arc. Consequently the player never entered the locomotion state at
`0x7E530`, and the unconditional call from that state to the hooked `0x44EA0`
gateway could not occur.

Version `0.0.117` keeps exactly one native turn action asserted according to
the sign of the desired-heading error until the target is reached. The
camera-relative intent is published before those actions. Once the locomotion
state calls `0x44EA0`, the existing hook still substitutes only the bounded
Q10 turn source for the duration of the complete native heading-and-lean call;
position, animation, collision and action storage remain retail-owned.

The same run showed right-stick camera motion after the controller selector
opened. Selector routing already published inactive/zero orbit input, but the
orbit response filter retained its preceding angular velocity and decayed it
over later source ticks. `0.0.117` treats inactive stick ownership as an
immediate filter reset. Normal right-stick release still uses the configured
response curve, while selector and first-person ownership cannot leak an old
stick impulse into third-person yaw or pitch.

## 0.0.117 result and shared heading gateway in 0.0.118

The `0.0.117` run still contains zero successful `xinput movement heading`
records. Its synthetic A/D trigger therefore remained an unmodified retail
tank turn and made the character uncontrollable. The target-heading stream is
valid; the hook still did not own the native mutation.

Further xref analysis corrects the integration point. `0x44EA0` is reached
only from the locomotion state at `0x7E530`. Turn-in-place and many other
states instead call the heading-only wrapper `0x44E90` from RVAs including
`0x5F6AA`, `0x5F72A`, `0x7E998` and `0x83516`. Both wrappers converge on
`0x44DD0`, and that function alone reads `player+0x154`, updates
`[player+0x10]->+0x1C` and mirrors the engine-owned collision/render heading.

Version `0.0.118` moves the temporary selected-source substitution to
`0x44DD0`. The live-player identity check remains in place, so enemy and other
actor calls pass through untouched. The complete native heading update remains
the writer; the hook restores the selected source immediately after it
returns. The later lean calculation in `0x44EA0` remains retail-owned.

The `0.0.118` gameplay run again contains zero successful heading records.
The camera-relative target stream is active, but the validated substitution
never owns the player call. The asserted A/D actions consequently remain
ordinary tank steering and produce the reported zigzag. Version `0.0.119`
disables `CameraRelativeMovement` in both the shipped INI and compiled default;
the heading hook is not installed. This restores the stable `0.0.115` left-
stick mapping while retaining the independent selector orbit-filter fix.

## 0.0.120 movement-controller identity and input-state result

The subsequent read-only pass resolved the failed ownership check without a
new runtime probe. The live UI/gameplay pointer is the outer player entity,
while movement callbacks receive the controller stored at `entity+0x114`.
Current and archived callback logs show those as distinct addresses (for
example `0x042705D8` and `0x1C074710`). The old `HookPlayerTurn` compared them
directly, so every invocation necessarily failed validation before it could
write the selected source.

Static input tracing also corrects the preceding turn-in-place conclusion.
`0x871B0` converts the retail action table into controller flags: walk
forward/backward become `0x8/0x10`, run becomes `0x20/0x40`, side-step becomes
`0x100/0x200`, and ordinary/fast turn become `0x2/0x4` plus a signed value at
controller `+0x148`. Idle dispatch at `0x68390` enters locomotion for the
forward flags, but enters dedicated turn-in-place states for `0x2/0x4`. Their
callbacks `0x68970` and `0x68C20` add fixed 32/64-unit heading steps directly
and do not call `0x44DD0`. By contrast, locomotion callback `0x7E530` always
reaches `0x44EA0 -> 0x44DD0`, even when its retail turn source is zero.

Version `0.0.120` therefore resolves and validates
`outer entity -> +0x114 movement controller`, reads heading and `+0x154` from
that controller, and uses a hysteretic two-phase mapping. Large reversals use
the native turn-in-place state. Once inside the configured forward arc, both
A/D actions are released, W alone enters locomotion, and `0x44DD0` receives the
bounded camera-relative delta. The forward phase is not abandoned until the
error exceeds the configured arc by 20 degrees, preventing boundary chatter.

The first `0.0.120` cardinal-direction run then proved a separate input-axis
mismatch. With orbit yaw 180 degrees, physical backward produced target
heading approximately 0 while the live actor was also heading 0, so backward
alone entered forward locomotion. Native root-motion records independently
show that heading 0 advances world +Z, ruling out a 180-degree actor-heading
basis correction (which would also exchange left and right). Version `0.0.121`
therefore inverts only the camera-relative left-stick Y component before the
camera basis is applied. `CameraRelativeInvertY=0` is available for controller
mappers that already expose the expected sign. Controller identity, state
hysteresis and the native locomotion hook are otherwise unchanged.

## 0.0.122 single bounded locomotion path

The following run showed that the remaining endless rotation occurred only
when the requested heading began outside the forward arc. That is precisely
the branch which asserted a retail A/D action and entered `0x68970` or
`0x68C20`; those callbacks bypass `0x44DD0`, so they also bypass the desired
heading clamp and can rotate past the target repeatedly while input is held.

Version `0.0.122` removes the A/D turn-in-place branch instead of retuning its
threshold or guessing its sign. Every camera-relative direction now asserts W
with A/D released, forcing the already validated
`0x7E530 -> 0x44EA0 -> 0x44DD0` path. `HookPlayerTurn` remains the sole heading
substitution, clamps each source-tick delta to `MovementTurnDegreesPerTick`,
and stops naturally when the wrapped desired-heading error reaches zero.
Position, root motion, animation, collision, walk/run state and action storage
remain native-owned.

## 0.0.123 runtime invalidation and stable fallback

The `0.0.122` run disproved the premise that asserting W makes `0x44DD0` a
recurring per-source-tick steering gateway. Across fourteen separate
camera-relative activations, the log recorded only the initial sparse hook
traffic; sustained native root motion continued without repeated controlled
heading calls. Since A/D was deliberately released, every requested stick
direction consequently ran along the actor's existing forward heading.

Version `0.0.123` restores `CameraRelativeMovement=0` in both the shipped INI
and compiled default. This returns the previously verified native tank mapping
without changing the modern camera, Start dispatch, R3 first person, selector
ownership or vibration. The experimental hook is not installed while the
option is disabled. Further camera-relative work is gated on read-only
identification of the recurring sustained-locomotion heading writer rather
than another synthetic key-state build.

## 0.0.124 native joystick path and recurring locomotion steering

The retail controller path is now traced end to end. `Dungeon.dll+0x51500`
polls a `DIJOYSTATE`, returns X/Y in the configured `0..0x4000` range and packs
16 buttons. `+0x5EC40` converts the axes into
`JOY_HORIZ_LEFT/RIGHT` and `JOY_VERT_FORWARDS/BACKWARDS`; `+0x5EB20` then feeds
those inputs into the same action table used by locomotion. The legacy engine
digitizes the axes after its deadzone, but this remains its genuine joystick
route rather than a keyboard approximation.

Runtime movement probes also show that `Dungeon.dll+0x7E530` is the recurring
forward-locomotion callback which produces root motion on sustained runs. On
every invocation it selects `controller+0x130` or `+0x138` as the walk/run turn
source and calls the canonical `+0x44EA0 -> +0x44DD0` heading writer. Version
`0.0.124` hooks this recurring callback, temporarily places the bounded
camera-relative delta in both native sources, calls the complete original
callback, and restores them. XInput movement enters through a synthetic native
joystick state and W/A/S/D remain released. Player position, speed, animation,
collision, action storage and heading publication remain retail-owned.

The first `0.0.124` run proved that the joystick bridge itself works, but the
character still ran forward for every requested direction. The session
contains 137 camera-relative target activations and 57 observed calls to
`+0x7E530`, yet only one successful `locomotion_heading` record. Re-resolving
the UI owner from inside the locomotion callback was therefore not a stable
identity gate.

Version `0.0.125` captures the exact movement-controller pointer alongside the
input target and compares the recurring callback directly with that captured
identity. Heading and turn sources are then read from the callback's own
controller without another UI-owner lookup. A 250 ms runtime watchdog also
fails closed to the real native joystick tank axes if no locomotion steering
is confirmed; a failed camera-relative attempt can no longer leave every stick
direction mapped to forward. All diagnostics now share one timestamped file
per process launch under `logs/`.

## 0.0.126 Arkham Asylum camera ownership transfer

Read-only extraction of the installed Arkham Asylum GOTY `BmGame.u` produced
the actual `BmGame.R3rdPersonCamera` class. It separates chase
position/velocity and bounded smoothing from collision-distance history,
multi-direction zoom probes, input assistance and authored camera states. The
important transferable result is ownership: it smooths the followed target,
not a second independently retained final camera point.

Deathtrap's active ordinary camera previously allowed native history, spring
radius, mesh correction, old-arm/tangent detours, a render mesh latch and a
render-only follow filter to disagree. Version `0.0.126` removes the last four
owners from the active path. A bounded chase target now feeds the current
yaw/pitch request, then one source-tick solve contracts immediately, recovers
outward at a bounded rate, calls native configure once and applies an exact
post-native correction only for a positive qualified render-mesh hit. Detailed
evidence and transfer limits are in
[`ARKHAM-ASYLUM-CAMERA-TRANSFER.md`](ARKHAM-ASYLUM-CAMERA-TRANSFER.md).

## 0.0.127 deterministic native-axis movement steering

The `0.0.126` runtime finally makes the `0.0.125` failure deterministic. A
single run repeatedly enters camera-relative intent, fails the 250 ms
locomotion-steer watchdog and changes to physical native tank axes during the
same stick hold. The log contains successful `locomotion_heading` calls as
well as multiple `no_locomotion_steer` failures. Therefore `0x7E530` is not a
valid universal per-hold steering boundary, regardless of controller identity.

Version `0.0.127` removes that hook and watchdog from the installed path. It
retains the proven native DirectInput joystick poll. Each input sample computes
the desired camera-relative heading and live shortest-angle error. That error
drives the native horizontal turn axis; the native forward axis is multiplied
by nonnegative course alignment. An input behind the character consequently
turns in place, then moves forward as alignment improves. No camera-relative
direction can emit native backward or switch semantic model while held.

## 0.0.127 result and shared ground-state dispatcher in 0.0.128

The `0.0.127` runtime rejects native horizontal-axis feedback as a steering
controller. Raw stick normalization and `JOY_HORIZ_LEFT/RIGHT` mapping are
correct, but the held turn axis does not update heading with one stable law:
`0x68970` and `0x68C20` apply fixed state-owned steps, while forward
locomotion uses `0x7E530 -> 0x44EA0 -> 0x44DD0`. Consequently a desired course
could keep moving forward, reverse apparent turn response, or fail to converge.

Static registration analysis identifies the missing common boundary.
`0x44EC0` stores an outer state dispatcher at controller `+0x2EC`; ordinary
ground states register `0x82750` there. That dispatcher refreshes actions with
`0x57760` and then invokes the current callbacks at `+0x2F0/+0x2F4`. The
forward state `0x7E530`, 32-unit turn state `0x68970`, and 64-unit turn state
`0x68C20` all sit underneath this same boundary.

Version `0.0.128` therefore leaves native horizontal input neutral and uses
native vertical forward input only. Immediately before the active ground-state
callback, the `0x82750` hook computes one bounded shortest-course delta and
submits it through the original `0x44DD0` writer. That writer advances
`[controller+0x10]->node+0x1C` and mirrors heading plus the native accumulator
to the engine-owned actor/collision publication. Non-ground and scripted states
that do not use `0x82750` receive no forced heading update.

## 0.0.129 verified longitudinal sign

The first `0.0.128` run validates the shared dispatcher: every logged target
converges without the former endless native turn state. It also isolates one
basis error. With a nearly fixed camera, opposite full-scale Y samples select
targets approximately 512 units apart, but the user's physical up/down result
is reversed. Version `0.0.129` sets `CameraRelativeInvertY=1`. This negates only
the longitudinal stick component before the camera-space heading calculation;
screen-left/right, native forward magnitude and the dispatcher writer are
unchanged.

## 0.0.130 stable source-tick chase integration

The post-`0.0.129` stability audit isolates a deterministic oscillation before
collision. `StepCameraChase` described its continuous system as critically
damped but advanced it with a semi-implicit Euler step. At the shipped 60 ms
source period and 2 Hz response the discrete system has approximate poles
`+0.6755` and `-0.7520`; the negative pole alternates position error and
velocity for a fixed target. The old one-dimensional 0-to-100 step begins
`56.849, 52.503, 81.712, 77.271`, proving that ordinary open-space follow can
produce A/B source poses without a wall or scene mesh.

Version `0.0.130` replaces only that integrator with a backward-Euler solve of
the same critically damped position/velocity system. Its denominator
`1 + 2*omega*dt + omega^2*dt^2` remains positive for the native step. Speed and
acceleration limits are applied independently per axis instead of by one 3D
vector budget, so horizontal course correction no longer delays vertical
follow. A sampled target reversal stops the obsolete velocity at the boundary,
and target crossing settles exactly rather than overshooting.

The standalone test now requires monotonic fixed-target convergence, no
opposite velocity after a 180-degree target reversal, bounded lag behind a
constant-velocity target, and identical vertical response with and without a
large simultaneous horizontal error. Mesh collision, spring recovery, retail
`0x2F380` history, temporal chord selection and D3D presentation validation are
deliberately unchanged so the first runtime result identifies this phase alone.

The `0.0.130` runtime validates that isolation. For ordinary adjacent samples
with player motion between 8 and 150 units and zero right-stick input, relative
camera motion changed as follows versus the final `0.0.129` run:

- median `41.0 -> 3.0` units per source tick;
- p95 `222.3 -> 87.6`;
- p99 `701.6 -> 226.7`;
- adjacent opposite-direction deltas above 20 units `173 -> 30`.

The temporal camera-chord guard fell from 102 unique source ticks to four even
though its code was unchanged. The prior chase integrator was therefore a
confirmed upstream cause of both exact source-pose instability and unsafe
synthetic chords.

## 0.0.131 rejected native position-history seeding

The same `0.0.130` run isolates the next owner. Across 222 records where both
native and scene-mesh obstruction flags are clear, submitted-to-published
distance still has median 148.5, p95 429.9 and maximum 495.3 units. Separating
the path gives median 110.8 for submitted-to-desired shaping and 103.4 for
desired-to-published history publication. Stable custom chase output is still
smoothed a second time by retail absolute-world history.

Version `0.0.131` tested transactional writes to the cached average at `+0x20C`
and four samples at `+0x218` before the one `0x2F380` call. The runtime rejects
the hypothesis. All 433 diagnostic calls reported `history_seeded=1`, but the
177 clear-flag records retained submitted-to-published median 177.9, p95 431.9
and maximum 509.8. The preceding `0.0.130` values were 148.5, 429.9 and 495.3.

Instruction-level reinspection finds the missed reset. `0x2F380` first calls
`0x2F340`; that immediately calls `0x2DC40 -> 0x2DC80`. At `0x2DCAF`,
`0x2DC80` passes controller `+0x204` to `0x2DE10`, which resets the ring's
current and oldest pointers to its first slot. It does not make prewritten
samples active. Only afterwards does `0x2F380` call `0x2DEF0`; `0x2EFFF`
inserts the current native placement into the now-empty ring and publishes
that one active sample. Pre-seeding bytes before `0x2F380` is therefore a
no-op, and `0.0.132` removes it completely.

The clear flags in the overlay describe its pre-native radial and scene-mesh
queries; they do not assert that retail room/sector placement must equal the
submitted point. Submitted-to-published distance can no longer be labelled a
persistent second smoother without a same-stage measurement. The earlier
Phase 2 interpretation is withdrawn.

## 0.0.132 pivot-relative supporting-axis hysteresis

The `0.0.131` trace does expose a separate deterministic contact problem.
Unidentified scene-mesh resource `10017` has 49 sweep records, including 21
contained/near-pivot non-radial escapes. Those 21 samples select axis 0 seven
times, axis 1 nine times and axis 2 five times, with nine adjacent supporting-
axis changes. Its render-node bounds move by as much as 105.1 units per source
tick. The log contains no semantic object name, so this proves an animated or
transform-changing scene node, not specifically a gameplay block. Near an OBB
corner, transform or ray changes can exchange the two numerically nearest
faces and move the camera between different escape solutions.

Version `0.0.132` stores only node/resource/axis identity for this exceptional
contact. For the same current object, the prior axis remains eligible while a
competing exit improves the escape distance by no more than the 96-unit camera
volume radius. A larger improvement, a ray which no longer reaches that face,
a changed object, or return to ordinary radial/clear contact releases the
preference immediately. The endpoint is never cached: it is rebuilt on the
current requested ray from the current focus and current OBB transform. This
preserves dynamic-block motion and orbit response and cannot recreate the
absolute-point anchor rejected in `0.0.107`. Native walls/floors, ordinary
radial spring contraction, thin-object classification and authored-camera
arbitration are unchanged.

## 0.0.133 contact-aware spring recovery

The `0.0.132` runtime trace showed that supporting-axis persistence works for
several stationary render resources but does not by itself eliminate global
camera reversals. The spring-release counter could still accumulate across a
different blocker or a regressing sequence of measured safe distances.

Version `0.0.133` attaches blocked outward evidence to a launch-local blocker
key. Native room geometry uses a reserved key; scene-mesh contacts mix the
render node and resource handles. A key change or clearance regression restarts
confirmation. Clear-space recovery begins only after four complete source
ticks, while every inward safety contraction remains immediate.

## 0.0.134 post-native scalar release gate

The final `0.0.133` sequence isolates a cross-stage A/B loop. Native room
geometry permitted the arm to grow from 580.1 through 644.1 to 708.1. Retail
`0x2F380` shifted the 708.1 submission onto scene-mesh resource `12613`, whose
post-native correction returned the published arm to 580.1. The pre-native
sweep did not see that resource, so native blocker confirmation accumulated
again and repeated the cycle every four source ticks.

Version `0.0.134` feeds post-native acceptance back into recovery without
retaining a position. It stores only a verified radius and launch-local blocker
key. After stable samples it configures one bounded larger radius as an
internal probe; the scalar ceiling advances only if the resulting retail
position passes the post-native mesh sweep. A rejected probe is committed back
inside the same source tick, before presentation snapshots are captured.

## 0.0.135 contained-escape publication ownership

The `0.0.134` run validates ordinary post-native recovery, including a complete
267.9-to-1399.5 sequence of accepted bounded probes. At contained/near-pivot
contact with resource `12613`, however, the current-transform OBB escape near
619 units was still submitted through retail history. The post-native branch
then intermittently classified the older shifted point as an ordinary radial
contact near 128 units. The two stages alternated their publications.

Version `0.0.135` still calls retail configure exactly once, but after native
room validation it directly commits the current source tick's OBB escape as
the final translation. The endpoint is not stored; the next tick recomputes it
from the live pivot, requested ray and object transform. Post-native contact
remains diagnostic on this exceptional path and cannot seed a radial gate from
the stale intermediate retail-history point.

## 0.0.136 post-native gate lifetime

The `0.0.135` trace records seven successful contained-escape authoritative
commits and no failures, removing the preceding 619/128 cycle. A smaller loop
remained where resource `12613` established a post-native ceiling near 313,
the current native obstruction clamped it near 269, and the gate immediately
declared that temporary limit complete. The next tick rediscovered the same
post-native contact.

Version `0.0.136` does not release a scalar post-native gate merely because it
reached a shorter pre-native safe radius. The complete desired arm must be
pre-native clear and its verified ceiling must reach the desired distance.
This preserves one contact lifetime across the native and scene-mesh stages
without retaining any world-space endpoint.

## 0.0.137 pivot-relative synthetic phases

The `0.0.136` trace validates the corrected contact lifetime: the scalar gate
has one legitimate OFF event, relative-camera median improves from 57.7 to
13.6 units and direction reversals fall from 26 to 14. The remaining temporal
guards are no longer spring ON/OFF events. They arise when two independently
safe exact endpoints are joined by a Cartesian interpolation chord that enters
a scene mesh.

Version `0.0.137` captures the live mode-3 focus in each exact scene snapshot.
For modern third person it interpolates focus, shortest-path yaw, pitch and
radius, then reconstructs the synthetic camera around that pivot. The current
native room-volume predicate and stable render-mesh sweep validate the complete
synthetic ray. If no safe intermediate exists, both synthetic phases use the
current exact safe translation while retaining interpolated rotation and all
ordinary actor/UI interpolation. The old previous-at-1/3/current-at-2/3
translation split remains only as a fallback for non-modern cameras or missing
pivot data.

The runtime test rejects translation-only fallback as incomplete. The old
Cartesian guard disappears, but the D3D detector still rejects 132 synthetic
Presents. At ticks 765 and 909, the guarded phases use the current accepted
origin while retaining an interpolated orientation; both phases introduce new
black cells. Version `0.0.138` copies the complete current world and local
camera transforms when the pivot-relative arm is unsafe. This preserves one
coherent accepted view for both synthetic phases without storing another
collision endpoint.

The `0.0.138` trace separates native and render-mesh failure cleanly: 1,122 of
1,128 guarded phases are native-clear, while 1,098 name render resource 12613.
Version `0.0.139` therefore attempts the missing intermediate collision solve
instead of immediately cutting to the exact-current transform. It shortens the
synthetic pivot ray against native geometry, clips it against stable scene
meshes, and then reruns both full-ray validations. The result exists only on
that synthetic render call; source-tick collision radius, blocker identity and
published controller history are untouched.

The `0.0.139` runtime reduces unresolvable synthetic fallbacks from 1,128 to
97 phases. It resolves 420 phases locally and reduces D3D rejects on fallback
ticks from 252 to 10. The remaining 206 rejects include 72 records on ticks
with neither a synthetic clip nor fallback, so the last-exact partial-black
comparison is now a separate presentation question. Version `0.0.140` records
each rejected midpoint against the following exact sample of the same tick,
including persisted and recovered collapsed cells. Camera output and rejection
policy remain unchanged for this diagnostic run.

The audit records 4,406 midpoint-collapsed cells, of which 3,425 recover in the
following exact frame. The clearest midpoint-only failure is not missing render
visibility: synthetic scene clipping accepts radii of 4.2--113.7 units on 644
of 696 clip records. At ticks 654--669 a 173--216 unit interpolated arm is
reduced to 4.5--8.1 units and passes both point/ray predicates, placing the view
at the followed pivot. Version `0.0.141` rejects any phase-local result below
the existing 120-unit `kThirdPersonMinimumCameraDistance`; it does not invent a
new presentation margin or change source-tick contact state.

The `0.0.141` audit shows that safe geometric endpoints alone are insufficient
for synthetic presentation. Midpoint-only black cells recover at 91.3% on
clipped phases and 84.5% even on clear pivot-relative phases, but at only 21.7%
when the complete exact-current camera is used. The cache path at `0x3860`
explains the split: owner stamp `+0x00` normally prevents its camera-dependent
tail from running again after the exact pre-capture refresh. Version `0.0.142`
temporarily makes that stamp differ from the unchanged engine frame and calls
the original routine after installing the synthetic transform. The mode-3
callback returns through its duplicate-source-tick guard; `0x3A980`, `0x3AC00`,
the published matrix copy, `0x38E80` and `0x3B0C0` rebuild render state. The
exact scene is restored and the same cache path is rerun before leaving the
synthetic transaction.

The `0.0.142` runtime disproves the assumption that this same-frame replay is
render-isolated. It introduces continuous visible player jitter even though
the camera-owner stamp reaches the current engine frame and the existing
player-state rollback sees no covered-field mutation. State below `0x38E80` or
`0x3B0C0` therefore escapes the known transaction. Version `0.0.143` removes
all forced `0x3860` calls and restores the `0.0.141` presentation path. Future
midpoint cache work must call a narrower verified render-only leaf or map and
restore the complete mutated cache state first.

The `0.0.143` runtime confirms the player-render jitter disappears after that
replay is removed. It also shows that synthetic modern-camera transforms are
the incompatible side of the boundary: same-tick midpoint-black recovery is
89.6% for clipped synthetic phases and 84.4% for clear pivot-relative phases,
but only 4.9% for the complete current exact transform. Version `0.0.144`
therefore presents the modern camera atomically from the current exact source
snapshot in both synthetic phases. Scene actors and animation remain
interpolated; no camera callback or cache writer is replayed.

Runtime `0.0.144` shows that cache compatibility alone is not sufficient for a
normal synthetic phase: a current-tick camera viewing an interpolated player
root produces visible player-relative shake and reduced smoothness. Version
`0.0.145` restores pivot-relative camera interpolation and retains
exact-current only as the existing unsafe-phase fallback.

The run also exposes the source of long-standing stepped arm expansion. After
each accepted 64-unit post-native probe, the caller reset the clearance counter
and waited three more 60 ms source ticks. An accepted probe has already passed
the native endpoint validation and the complete post-native scene-mesh pass,
so `0.0.145` preserves release readiness after acceptance and checks the next
bounded step on the immediately following source tick. Rejection/contact still
resets readiness to zero.

Runtime `0.0.145` validates the corrected cadence: the player remains visually
stable, pull-back is smooth, and five logged recovery sequences advance by
consecutive checked steps to `desired_clear`. Two candidates are rejected
by the scalar gate and native endpoint validation reports no failure. This does
not establish that other contact topologies are cycle-free.

The same run reproduces a distinct player/flag/wall squeeze on resource
`12613`. One contact generation commits `-70/400/15148` 39 times with stationary
focus while requested orbit rotates, proving a fixed geometric anchor rather
than scalar release jitter. Other passages switch the same resource's escape
axis from 0 to 2 and back, with radius discontinuities `203 -> 561` and
`679 -> 207`. Sixteen axis-2 escape targets are also changed by native wall
clipping before the exceptional pre-native commit, so the scene-mesh face and
published wall-safe point are not the same solution.

The defect is in the outside-pivot fallback of
`CameraMeshExpandedBoundsPushout`: `PushCameraToUsableExpandedBoxFace` retains
the pivot coordinate and changes only one discrete face coordinate selected
from the preceding camera. It does not actually slide with requested orbit.
The next implementation must preserve only the safe supporting coordinate and
derive the tangent coordinate from the current request, with the previous
camera used only to choose a sign at the zero-direction singularity. This is a
stateless geometric correction, not another contact threshold or retained
camera endpoint.

Version `0.0.146` implements that stateless construction. A pivot outside
exactly one horizontal expanded-OBB face keeps its support coordinate on the
safe side, but its tangent coordinate comes directly from the current requested
orbit. When the requested tangent approaches zero, the support coordinate moves
outward along a continuous minimum-radius arc; it never selects an unrelated
opposite face. A contained pivot uses the true first ray exit without the old
preferred-axis override. The resulting endpoint still passes the existing
scene-mesh and native wall validation, and no spring/release timing changes.

Runtime rejects `0.0.146`: the user reports increased jitter. The final
captured failure does not invoke its new outside-face path at all. With focus
fixed at `-606/425/14603` and orbit fixed at `541/1135/14979`, camera probes
repeat `-440/535/14829 -> -505/492/14740 -> -443/537/14831 ->
-509/491/14738`. On one half of the cycle, post-native resource `12613`
contact contracts a roughly 302-unit retail publication to 179--183 units and
commits it exactly. On the following apparently clear configure, the scalar
gate remains at that same short radius but no exact commit occurs; a queued raw
desired/history point later becomes visible at roughly 302 units. The next
post-native pass clips it again. This is the deterministic delayed-history
two-cycle previously characterized in `0.0.104`, not spring release or new OBB
geometry.

Version `0.0.147` removes the rejected `0.0.146` slide and closes that specific
ownership gap inside the already-established post-native scalar gate. After a
currently clear configure it examines the native resolved point, raw desired
point, cached history average and all four position samples. If any pending
point intersects a qualified scene mesh, release readiness cannot advance. A
non-probe tick commits the current submitted endpoint; a rejected probe rebuilds
the preceding verified ceiling on the current focus-to-orbit ray. The endpoint
must meet the 120-unit minimum and independently pass the native volume and
complete scene-mesh arm queries before it may normalize controller state,
history, node matrices and the published camera. Thus the writer is current-
tick and focus-relative; no previous world-space endpoint is retained.

The `0.0.147` runtime confirms all 108 pending-history normalizations succeed
and removes the preceding exact-source A/B sequence. Its final failure is a
different presentation ownership contradiction. Focus, player and input are
stationary. Every exact source tick publishes the pre-native near-pivot escape
`-205/400/15283` at radius 725.7. The post-native sweep of that same unchanged
point hits resource `12613` triangle 10 at radius 143 and commits
`-588/400/14833` at radius 134.8. The exceptional pre-native branch then
immediately commits the 725.7 point again. Both 1/3 and 2/3 pivot-relative
presentation phases repeat the same 725.7-to-134.8 collision clip, while the
exact phase returns to 725.7. The visible cycle is therefore short/short/long
inside every source interval; it is neither history delay nor spring release.

Version `0.0.148` gives the newer post-native evidence veto power over the
exception. If retail published the submitted endpoint unchanged and its full
focus-to-camera sweep selected a different mesh-safe point, the submitted OBB
escape is explicitly disproved and cannot overwrite that correction. When
retail instead publishes a different delayed-history point, the result does
not test the current submitted escape and the `0.0.135` stale-history exception
remains intact. This rule compares exact endpoints and stage provenance; it
introduces no distance threshold, retained target or release delay.

The `0.0.148` trace proves the veto fires 109 times, but exact probes still
alternate `-143/400/15221` at radius 489.4 with `-274/400/15042` at radius
267.6. On the intervening ticks retail initially publishes the preceding short
history point, so the current long submission is not directly tested and the
exception commits it. The following tick tests and rejects it again. The
underlying state error is earlier: selection unconditionally disables an
active post-native scalar gate whenever a non-radial escape is available, even
when both diagnostics identify the same node/resource blocker.

Version `0.0.149` gives the verified scalar gate priority over a non-radial
escape with the identical launch-local blocker key. Its endpoint is rebuilt
from current focus/orbit and the verified ceiling, so this adds no retained
position. Existing bounded post-native probes remain the only route outward.
A different blocker key, including changed native-wall participation, releases
that ownership and permits a new escape immediately.

## Exact collision-pose ownership (0.0.150--0.0.152)

Version `0.0.150` makes rejected post-native probes transactional, and
`0.0.151` keeps simultaneous pre- and post-native blockers in one constraint
set. The latter removes the persistent exact-source two-point loop, but its
runtime exposes a distinct pose split. With player `-712/0/14368` and focus
`-712/400/14368`, the published translation remains at
`-517/622/14372` for dozens of source ticks while the requested orbit completes
full revolutions and the camera matrix basis changes every tick. The pivot is
not lost in controller memory. A contact-only exact commit replaces node and
matrix translation, while the angles and basis still describe the different
position produced by native history. The view consequently appears to rotate
around an invisible off-player point.

Static disassembly identifies the missing owner precisely. `0x2F380` reaches
`0x2DEF0 -> 0x2E950 -> 0x30790`. `0x30790` calls the pure leaf
`Dungeon.dll+0x30730(camera_position, look_target, angles)`, stores the three
results at camera node `+0x18/+0x1C/+0x20`, and the ordinary camera-cache tail
then runs `0x3A980 -> 0x3AC00` before copying node world matrix `+0x9C` to the
published camera matrix. The earlier `0.0.142` failure came from replaying the
whole `0x3860` cache path, including camera callbacks and downstream render
caches; it does not prohibit use of this narrow orientation leaf inside the
existing callback.

Version `0.0.152` therefore extends only
`CommitImmediateSpringArmContraction`. For an already-authorized exact
scene-mesh contraction it calls `0x30730` with that exact committed position
and the current focus, then writes translation and the resulting node angles
as one source-tick pose. The untouched cache tail constructs matching local,
world and published bases. No cache replay, second configure call, retained
world endpoint, new collision threshold or change to clear/native/authored
camera ticks is introduced.

The `0.0.152` runtime rejects the assumed look target. Static reinspection of
the complete caller shows that `0x30790` receives a resolver-selected target
from `0x2DF60/0x2E950` and may apply authored corrections before it invokes
`0x30730`; the modern focus is not a proven substitute. The direct leaf call
ran 542 times and amplified the fixed-boundary failure while leaving the
underlying translation/history topology unchanged.

Version `0.0.153` removes only that angle writer and changes three proven
collision transactions:

1. When the pivot lies in a camera-radius-expanded mesh OBB and a triangle
   reports `initial_overlap`, compute the exact ray interval inside the OBB.
   Discard only that contained interval, then sweep the remaining segment
   against the actual triangles. A clean exit is not a collision; a later
   re-entry remains an ordinary blocking contact.
2. Before an exact contact commit, compare the complete controller position
   transaction, camera-node translation, local/world translations and
   published translation with the accepted target. An already normalized
   boundary returns `UNCHANGED` and does not rewrite the same collision state.
3. A post-native outward probe advances its scalar ceiling only to the radius
   which was actually published and passed the post-native mesh sweep. A
   submitted-but-delayed 64-unit request cannot release the gate before its
   native publication exists.

No world-space camera endpoint is retained. Native room, floor, sector and
authored-camera orientation ownership remain in the original path.

The verified x86 `0.0.153` DLL SHA-256 is
`2FEE79A4898F4112CB94DC049A7558CE12FC3AEA2F1A0542AD042CCB2034EC25`.

## 0.0.153 runtime correction and exact native pose capture

The 0.0.153 log disproves its primary runtime diagnosis. Pivot-exit executes,
but the reproduced fixed origins are ordinary `overlap=0` near-pivot escapes.
For stationary focus `-597/400/14730`, exact publications remain fixed for
21--52 source records while the desired orbit covers 2,100--3,470 units.
Resources 12613 and 11432 both exhibit the pattern. The post-native sweep uses
the previous accepted camera as its outside-pivot reference, so its old
expanded-face fallback recreates the absolute anchor even though the current
request changes.

The same trace identifies a separate recovery-state defect. A speculative
gate probe can publish substantially inside its previous verified ceiling
without a scene-mesh hit. The conservative inward publication is valid, but
retaining three clear ticks lets the next tick immediately probe outward and
turn the delayed placement into a visible return jerk.

Version 0.0.154 therefore:

- replaces only the outside-one-face fallback with a current-orbit tangent
  slide and continuous minimum-radius support arc;
- resets post-native clear evidence when the native publication regresses;
- hooks the pure `0x30730` leaf as a read-through capture boundary during
  `0x2F380`. Its actual second argument is saved for that configure call. An
  exact correction then invokes the original leaf with the corrected position
  and captured resolver-owned target, installing the resulting angles with
  the corrected translation.

This differs from rejected 0.0.152: the patch does not substitute the modern
focus for the native target. It also differs from rejected full-cache replay:
no callback, scene cache or render-cache tail is called a second time.

The verified installed x86 `0.0.154` DLL SHA-256 is
`3A0936BF42AD7CEE80AAE7FE9E245263C207EC41FE439E96932B18C5E3DBB4EC`.
The release build and both executable tests passed; the installer CRLF test
also passed. Runtime validation is still required for the flag and narrow
corner cases.

## 0.0.154 runtime: rollback state became the anchor

The final 0.0.154 trace proves that preserving the exact current publication
was correct only as a one-tick visual result. Copying it into the resolver's
future desired, average and complete four-sample history was not rollback: it
destroyed every new tangential sample. The trace contains 290 such
normalizations, zero idempotent transactions and 30 runs of a fixed published
position while the requested orbit continues moving.

Version 0.0.155 separates those time domains. Unsafe pending history defers
gate release and keeps the current mesh-clear publication visible, but remains
owned by the next native resolver tick and is not rewritten. A pending sample
that later becomes current is still checked by the existing post-native scene
mesh pass before presentation. Exact full-history commits remain available
for actual new contractions; they are no longer used merely because a future
sample differs from the safe current publication.

The verified installed x86 `0.0.155` DLL SHA-256 is
`387FAC51C52D5F252A2B0DD2DDC3D4F28256F53481475E84DFA820E13C096745`.

## 0.0.155 runtime timing and 0.0.156 field ownership

Runtime disproves the final 0.0.155 assumption. A deferred unsafe desired
position is not harmless future state: `0x2F380` consumes it on the following
source tick. In the completed trace, 142 of 146 deferrals become a mesh commit
on the next camera record. Current publications therefore repeat in pairs and
then jump by as much as 387.2 units even with a stationary focus/orbit.

The confirmed position map is: resolved `controller+0x1DC` (index 0), desired
`+0x1F4` (index 1), cached average `+0x20C` (index 2), and the four 12-byte ring
entries beginning at `+0x218` (indices 3--6). Version 0.0.156 sweeps all seven
but treats index 0 as read-only current publication. Each colliding future
index carries its own address, original point, mesh-clipped point and resource
diagnostic. The replacement is written only after native volume and repeat
mesh validation; failure holds only that address at the verified current
publication. This eliminates the two rejected extremes: full history collapse
from 0.0.154 and unsafe future preservation from 0.0.155.

The verified installed x86 `0.0.156` DLL SHA-256 is
`592047A0871BCC6F3E423C672784DC4FD2B0A6E859D150C17C73ED294D9568C5`.

## 0.0.156 runtime and the actual pre-history writer

The 0.0.156 log contains 152 targeted three-field repairs with zero write
failures. Nevertheless, all 152 next native configure calls recreate an
initial publication exactly equal to the preceding unsafe candidate and all
152 enter the mesh-commit path. Final defer-to-next-final jumps >=20 fall from
102 to three, so targeted sanitation is a useful idempotent boundary fallback,
but it does not own candidate creation.

The exact instruction path is now confirmed. `0x2F380` calls `0x2F340`, whose
first operation is `0x2DC40`; `0x2DC80` then initializes five history objects,
including the four-sample position ring at `controller+0x204`. `0x2DEF0` calls
`0x2E950 -> 0x2EDC0`. At `0x2EFFF`, `0x2EDC0` invokes
`0x2DE30(controller+0x204, controller+0x1F4)`. The returned cached average is
copied to the camera node at `0x2F015..0x2F025`, immediately before endpoint
sector resolution and the native orientation/publication tail.

Version 0.0.157 detours the generic `0x2DE30` but changes its input only when a
thread-local modern `0x2F380` scope is active and the ring address equals that
scope's controller plus `0x204`. The already-native-resolved candidate is
scene-clipped before insertion. Native-volume and endpoint validation are
repeated after any scene correction. Every other call uses the original
arguments, and validation failure deliberately falls through to the existing
post-native exact correction.

The installed/build/dist x86 `0.0.157` DLL SHA-256 is
`D4FB37A0E3F8A875378451C342DA97CDB79B7BEE85B2DC0A32EB01D0659E4CB0`.

## 0.0.157 runtime: two camera clocks and 0.0.158

The completed 0.0.157 x2 session proves that the remaining flag jitter has
two owners. All 78 synthetic samples are phase `0.500`; long runs reject every
midpoint pivot ray and select `current_transform_both`. The world and player
remain at the midpoint while the camera advances to the future exact endpoint,
producing an early jump followed by a hold. Fixed focus/orbit source records
also publish changing positions, so presentation interpolation is not the only
source.

A zero `ConfiguredSubframes()` value also returned from initialization before
resolving `Dungeon.dll` or installing any game hook. XInput, modern camera,
first person, selector and event support therefore accidentally depended on
x2/x3. Version 0.0.158 always initializes the supported gameplay patch and
uses `g_subframes` only to gate synthetic passes.

For a modern configure, the endpoint already accepted by the native room
volume and current scene-mesh spring arm is revalidated at `0x2DE30` and
supplied to the exact `controller+0x204` position ring. Native sector
resolution and its resolver-owned look target/orientation remain downstream;
position history no longer creates a second spring-arm endpoint. The old
post-native scalar gate is not fed back while this deterministic insertion
hook is active, while positive post-native correction remains fail-closed.

Presentation builds a read-only phase scene from the already interpolated node
hierarchy and bounds. It interpolates focus/yaw/pitch/radius, then composes
native room clipping with the complete mesh solver, including the proven
contained-pivot exit, against phase-local transforms. If no valid
minimum-distance phase pose exists, it holds the preceding complete source
pose until exact presentation instead of using the future exact pose at the
midpoint. Runtime validation is pending.

The installed/build/dist x86 `0.0.158` DLL SHA-256 is
`539CF94C508D19F4C1337C621310CD87A91B87E10F8101BE2A405B6978A9BF85`.

## 0.0.158 F11 isolation and 0.0.159 source-rate presentation

The completed 0.0.158 x3 session is
`<game-directory>\logs\deathtrap-native-20260801-211416-811-pid32636.log`.
It contains four direct F11 disable/enable pairs. While synthetic passes are
disabled the stationary flag case stops producing camera changes. Immediately
after every enable, two valid source positions begin alternating once per
engine tick. In the final sequence those positions are approximately
`-459/497/14774` and `-627/617/14777`; 710 phase clip records repeatedly
contract alternating 235/304-unit input radii to 166 units against resource
12613. This is a presentation-to-source feedback loop, not an under-damped
scalar spring.

The same runtime also invalidates 0.0.158's unconditional position-ring
replacement. Its pre-history hook could substitute the submitted endpoint
without a positive scene-mesh contact. That bypassed the retail ring's ordinary
wall and room-volume shaping and allowed camera travel through walls.

Version 0.0.159 restores the narrow 0.0.157 ownership rule: only a positively
intersecting qualified mesh candidate is eligible for pre-history replacement,
and the replacement is validated against native volume before insertion. The
post-native scalar gate remains available. Synthetic camera presentation no
longer interpolates polar orbit state or runs a phase collision solve. It keeps
the preceding complete exact matrix and translates it only by the interpolated
focus delta. Native room visibility plus phase-local endpoint occupancy can
reject that carried origin and select a complete exact fallback, but no
synthetic result becomes a new radius, yaw, pitch or source-history owner.

The installed/build/dist x86 `0.0.159` DLL SHA-256 is
`9D5605FA3BFCB3CFF5656A4288974EAC9B717D46529C273B4548C4C949A88839`.

## 0.0.159 runtime rejection and 0.0.160 camera-cache deferral

The completed 0.0.159 x3 session is
`<game-directory>\logs\deathtrap-native-20260801-212704-368-pid34160.log`.
The phase endpoint-occupancy guard fires 410 times. Almost all records are
native-clear but `endpoint=0`, and selection alternates between complete
previous holds and current exact fallbacks. The conservative expanded OBB is
therefore unsuitable as a presentation-origin predicate and directly explains
the new continuous whole-level judder.

The stationary flag interval still has one player position but cycles three
exact camera positions while interpolation is enabled, including
`-585/547/14551`, `-585/550/14564` and `-524/504/14541`. F11-disabled intervals
remain visually stable. The remaining ON/OFF difference precedes snapshot
interpolation: x2/x3 explicitly ran the complete native camera-cache update
before midpoint capture, whereas x1 lets the original exact renderer invoke it
at its retail position in the frame.

Version 0.0.160 updates only the scene cache before capture. It saves the
camera-owner frame stamp and temporarily writes the current engine frame so
synthetic renderer calls skip the camera cache entirely. Immediately before
the one exact original renderer, it restores the saved stamp. Native mode-3,
position history, collision and camera-dependent cache work then run once at
the same boundary as x1. After exact presentation the live camera world/local
matrix and focus are recaptured into the source history. Every early return
restores the stamp before entering the original renderer.

The 0.0.159 phase OBB endpoint predicate is removed. A carried focus-follow
pose is held only when the read-only native room-volume query rejects it; scene
mesh ownership remains exclusively in the exact source camera path.

The installed/build/dist x86 `0.0.160` DLL SHA-256 is
`8C2D6F35949F3F900461A555F6524187FB76AD592C86EEC4AEB7B9F992468ED7`.

## 0.0.160 runtime rejection and 0.0.161 four-tick source cycle fix

The completed 0.0.160 session is
`<game-directory>\logs\deathtrap-native-20260801-213651-902-pid2904.log`.
Camera-cache deferral removes all `camera_source_rate_guard` events but does
not remove the flag defect, so early cache timing is not its root. Source-rate
camera presentation also makes the 50 Hz actor/world move against a 16.7 Hz
view and visibly judders the complete scene.

The exact log exposes a deterministic four-tick source loop. With stationary
focus/orbit, `camera_pre_history_mesh` hits resource 13725. The clipped candidate
fails validation, but the hook accepts the separate submitted endpoint with
`submitted_fallback=1`. The post-native scalar gate then advances clear ticks
1, 2 and 3 before the same fallback contact repeats. Published positions cycle
between approximately `-412/515/2290` and `-317/449/2216`. This occurs before
synthetic camera presentation.

Version 0.0.161 removes submitted fallback authority from the position-ring
veto. Only the actual scene-mesh-clipped candidate may replace a ring input,
and only after native-volume plus endpoint validation. Failure passes the
original candidate through for the post-native exact safety path.

x1 now captures one read-only exact `SceneSnapshot` after the original renderer
completes. These snapshots populate the same older/previous mesh history used
by the source spring arm, so disabling interpolation no longer disables prop
and moving-block collision. Camera probes also remain available at x1.

x3 returns to exact-endpoint pivot-relative interpolation. Focus, shortest yaw,
pitch and radius interpolate coherently; only native room-volume clipping may
shorten a synthetic arm. The rejected phase scene-mesh/expanded-OBB solver and
0.0.160 source-rate hold are absent.

The installed/build/dist x86 `0.0.161` DLL SHA-256 is
`CF31ABCBF375564247DED43871EA9D46B6EC3DEDBE1DC55FD6A70B6FC4DC6FA0`.

## 0.0.161 runtime: contradictory boundary predicates and 0.0.162

The 0.0.161 flag/door trace keeps focus, requested orbit and input fixed but
alternates the native camera translation every source tick between
`-588/626/14899` and `-588/556/14741`. Resource 12613 clips the former to the
latter. The clip reports surface distance 368.1, camera radius 96 and safe arm
360.1, which is the intended eight-unit contact backoff.

`ValidateCameraPreHistoryMeshReplacement` then repeated the triangle sweep but
called an endpoint predicate that also classified expanded-OBB containment as
occupied. Resource 12613's OBB contains empty room space around its render
triangles, so the backed-off candidate failed pre-history while the post-native
real-mesh pass accepted and committed the identical point. This is the exact
cause of `FALLBACK_POST -> exact commit -> clear/defer -> FALLBACK_POST`.

The 0.0.162 endpoint predicate checks direct sphere-to-render-triangle distance
only. The complete focus-to-endpoint sweep immediately before it still rejects
real surface entry; the final endpoint check guards integer rounding, not
conservative bounds occupancy. The pre-history hook can consequently insert
the same safe point that post-native would otherwise commit one tick later.

The dead render presentation latch/follow implementation and the 0.0.156
post-history scan/repair implementation are removed. Neither had an active
presentation call site, and the latter could not prevent `0x2F380` from
recreating a candidate before the next `0x2DE30` insertion. Post-native exact
commit and scalar release gate remain as fail-closed handling for native
lateral/vertical shifts not visible before configure.

Installed/build/dist x86 `0.0.162` SHA-256 is
`4A0121D2C2205E8FA9CE202E7C6B2FEBB517846EFC7D2DE7B5FC02F3E3C26AB3`.

## 0.0.162 runtime: retail history is not collision evidence

The remaining `post_native_mesh_gate` assumed that the radius published after
`0x2F380` measured the newly submitted probe. The runtime trace disproves that
assumption. With fixed focus, zero orbit input and no current native or scene
obstruction, the four-position history continued publishing older radii. The
gate fed those values back into its verified ceiling for as many as 215
consecutive diagnostic records and created a self-sustaining probe/regress
cycle.

The architectural distinction is now explicit: the scoped `0x2DE30` insertion
hook is a point at which a current native candidate can be collision-tested;
the position published after history averaging is a temporal output. It may be
post-validated for safety, but its lag relative to the current request cannot
prove a new obstruction. Version 0.0.163 removes the scalar post-native gate.
A positive post-native real-mesh sweep still exact-commits the safe point and
updates the ordinary spring-arm state; a clear sweep creates no separate
memory or feedback transition.

Installed/build/dist x86 `0.0.163` SHA-256 is
`D9AABAA44A4AB9982C0DD732CF5C13E164CF5938FBDD2169D4769ACE0CEA2E34`.

## 0.0.163 trace: two current scene constraints

After removal of the ghost radius gate, the final trace alternates two genuine
scene-query results. Resource 13676 produces a non-radial near-pivot escape at
radius 741; the configure-scoped candidate and post pass detect resource 13679
and contract to radius 129. `CameraPreNativeEscapeOwnsFinalTarget` was still
allowed to overwrite that second result whenever the retail publication did
not equal the submitted escape. That test confused history lag with absence of
current collision evidence.

Version 0.0.164 treats both scoped pre-history replacement and post-configure
sweep as current-call constraints. A different accepted target makes the
pre-native escape incompatible regardless of which point retail history
publishes. The expanded-OBB solver also preserves orbit pitch: after choosing
a horizontal face it restores vertical displacement at the same normalized
horizontal progress. The prior horizontal-only output was responsible for
instantaneous drops from requested `-440` to focus `-1400`, and from roughly
`1129` to `400`.

Installed/build/dist x86 `0.0.164` SHA-256 is
`0FC6D6D6F43779116B02B54ED131E06F949E6EC8184A1111BB6DCD1A90724554`.

## 0.0.164 trace: flag scale and falling-focus reset

The completed `0.0.164` run is
`<game-directory>\logs\deathtrap-native-20260801-222836-230-pid42228.log`.
At the stationary door position, qualified resource 12613 alternates an
approximately 916-unit escape with a 540-unit configured target. The resource
is the narrow flag geometry: its measured span is approximately
`309x1036x309`. The current one-camera-diameter threshold is only 192, so the
flag is deliberately admitted as a volume blocker despite having no camera-
scale transverse mass.

Version 0.0.165 requires two intrinsic mesh axes to span two camera diameters
(384 units). This is a geometry-class rule, not a resource exclusion. The
known 180x900x180 lever, resource 12613 and small housings become nonblocking;
walls and large static or moving blocks remain qualified.

The falling jolt is independent of collision. From camera-probe ticks 52--60,
player Y changes by exactly -400 per source tick while camera Y repeats
approximately `-799/-169/-250`, `-781/-166/-249`, and `-785/-165/-247`.
Those records contain no scene contact. `chase_focus` accumulated more than
900 units of error against the fast moving target and was then reset directly
to it every third tick. The existing 2500-unit single-tick focus-jump rule
already detects real teleports. Version 0.0.165 removes only the accumulated-
error reset, preserving the bounded backward-Euler chase during continuous
falls.

Installed/build/dist x86 `0.0.165` SHA-256 is
`B5D13CB513B27B6CEE0879D3C39B8580F5F054A875BDCD06A42E73897A57E982`.

## 0.0.165 trace: failed speculative history candidate

At the final stationary flag position, the modern pre-configure endpoint is
stable at `-548/414/14768`, while the retail `controller+0x204` ring insertion
candidate is repeatedly `-715/544/14828`. The scoped scene veto detects
resource 12613, but candidate-derived validation cannot resolve the complete
nearby constraint set. Passing the original candidate to post-native safety
creates a four-position loop through resources 12613 and 11432.

Version 0.0.166 adds a transactional failure path at the same verified ring
boundary. The original submitted endpoint is not trusted by identity. It is
run through `ValidateCameraPreHistoryMeshReplacement`; replacement is allowed
only when the validated result exactly equals the submitted integer endpoint.
This preserves native wall/room shaping for ordinary candidates and differs
from 0.0.161, whose broader submitted fallback could change the endpoint and
alternate ownership.

The v0.0.165 size threshold is reverted from 384 to 192 units because it did
not remove the measured resource and could omit thin closed doors. No resource
ID, flag asset or door asset is special-cased.

The first installed x86 `0.0.166` SHA-256 is
`6BF68DABAA36574228175DE75A2F042E0EF1F68E2EAFF695C20EE3D6140B03AE`.
Latest build/dist SHA-256 is
`0FC4C08438B5021AA244B1245146A2AB83E2AE6B0C344335348D9F23F828C3AF`;
it adds only explicit submitted-validation coordinates and awaits installation
after the running game exits.

## 0.0.166 runtime: zero-motion exact-commit latch

The completed run is
`<game-directory>\logs\deathtrap-native-20260801-230007-111-pid41168.log`.
At the final corner the focus is `-9095/-1400/16044`. Orbit input changes the
requested camera through positions including `-9576/-253/16687`,
`-9324/-253/15274`, `-8374/-253/15691` and `-9814/-255/15681`, proving that
mouse/controller input and orbit integration continue to run. Publication,
controller desired and controller resolved nevertheless remain fixed at
`-8844/-884/16304`.

The post-native sweep repeatedly reports resource 13676, initial overlap plus
near-pivot escape, with both its input and result equal to that exact fixed
coordinate. `CommitImmediateSpringArmContraction` then rewrites all camera
history slots with the same old point. The persistent spring radius is 9 even
when the current pre-native arm becomes clear, because every redundant commit
resets the release evidence. Resource 12613 shows the same mechanism at the
flag; this is not a flag-specific mesh classification error.

Version 0.0.167 classifies a post-native result within two units of the current
integer publication as `IDEMPOTENT_NOOP`. It retains contact diagnostics but
does not commit, update the supporting-face preference, alter spring state or
conflict with a current validated pre-native escape. The log exposes the rule
as `camera_post_native_mesh action=IDEMPOTENT_NOOP` and
`camera_native_mesh_pushout ... exact=0 idempotent=1`.

Build/dist SHA-256 is
`9C854ECD5276BE186272015D09C461DD6EDD3A2F7B8047C2A4C5CE48415FA4F7`.

## 0.0.168 renderer-qualified scene nodes

The final stationary orbit in
`<game-directory>\logs\deathtrap-native-20260801-231502-040-pid31532.log`
has a fixed focus at `-12940/-1437/17123`. Each full circle produces the same
27-unit spring contraction against resource 12708 and then an immediate clear
return to the requested 1400-unit arm. The user confirms that this part of the
room contains no visible object within camera range, so the repeated contact
is not a conservative response to visible clutter.

Static analysis identifies a missing node-eligibility rule. In the supported
binary, `Dungeon.dll+0x3B93F` tests node flags at `node+0x24`. Bit
`0x02000000` branches around the resource draw call at `0x3B95F` but continues
to the ordinary child traversal at `0x3B9BD`. It therefore means that this
node's own render resource is not submitted, independently of whether its
transform, resource handle and cached bounds remain in the scene tree.

`NodeTransform` now captures the raw flags. Every scene-mesh sweep, endpoint
test and temporal-chord query rejects a node whose own resource the renderer
skips. Children remain individually eligible, matching the retail traversal.
The exclusion is logged once per node as
`camera_mesh_renderer_skip ... flags=........ reason=NO_OWN_DRAW`. No resource
ID, room coordinate, triangle count or general object-size threshold is
special-cased.

The x86 build, spring-arm state test, DirectInput proxy smoke test and
installer CRLF test pass. Installed/build/dist SHA-256 is
`1972C919616133E29EF1FD9B16D8A9829E5F759AABB746B5473F0C223573CDDF`.

## 0.0.169 stationary orbit: cross-axis input, not collision

The completed v0.0.168 run is
`<game-directory>\logs\deathtrap-native-20260801-233141-807-pid6972.log`.
The final stationary circles contain no scene sweep, native-volume contraction
or post-native correction. Player and focus remain fixed. The physical gesture
was horizontal, but the probe records right-stick values `-32768/-11969` and
later `-32768/-11137`. Independent per-axis deadzones leave a curved vertical
input around `-0.079`; over successive source ticks it drives pitch to `-35`
degrees. The requested endpoint then hits the floor envelope on every tick,
and the retail resolver's published Y varies by about 25 units with yaw.

Version 0.0.169 applies a continuous proportional axial lock before the
third-person response curve. A minor component within 25 percent of the
dominant axis becomes zero. Beyond that cone it is restored continuously and
equals the original component at a true diagonal. This changes neither the
dominant orbit speed nor mouse input and prevents a held cardinal gesture from
accumulating unintended pitch/yaw. The percentage is configurable as
`XInput/RightStickAxisLockPercent`.

The x86 build, spring-arm state test, DirectInput proxy smoke test and
installer CRLF test pass. Installed/build/dist SHA-256 is
`1C3194A60E7E2ECDD61046526A3BC2E54E119FDD6E3C69F7102FB1DA549A7989`.

## 0.0.170 clear-orbit positional ownership

The completed v0.0.169 run is
`<game-directory>\logs\deathtrap-native-20260801-235908-214-pid21704.log`.
The axial lock works: the final horizontal gesture reaches the orbit as
`-1.000/0.000`. Visible stutter nevertheless remains both in the first room
and on the bridge. The final bridge circles isolate it from input, scene
collision and synthetic-phase rejection. With fixed focus
`-939/400/-21776`, the modern solver submits a 1400-unit circular orbit at
constant Y `1099`, while the retail configure path publishes Y `739`, changes
horizontal radius cyclically and reverses yaw by roughly two to three degrees
for one source tick at repeatable angles.

Static disassembly narrows the old v0.0.158 failure. `0x2F6D0` calls
`0x30910`, and `0x30910` returns clear as soon as any one of its centre-plus-six
room traces through `0x4E760` succeeds. That visibility rule is appropriate
for the stock fallback-camera selector but is not proof that the complete
modern camera footprint lies outside a wall. It explains how unconditional
submitted-endpoint ownership could pass the old validation and still show
wall/floor penetration.

Version 0.0.170 leaves the ordinary native predicate and every constrained
collision path unchanged. On a full-radius source tick with no qualified
scene contact, it additionally resolves the same centre and six 30-unit focus
offsets and calls `0x4E760` directly, requiring all seven traces to succeed.
The endpoint must also pass the current scene-triangle test. Only then does the
already verified exact transaction publish the modern endpoint after the one
ordinary `0x2F380` call; native sector bookkeeping and its captured look target
still determine orientation. A missing sector, failed trace, contracted arm,
scene contact or exhausted solve leaves the complete retail result untouched.

The x86 build, spring-arm state test, DirectInput proxy smoke test and installer
CRLF test pass. Installed/build/dist SHA-256 is
`B0A28F1B5D2FE6FB70CD7160A9F200FDE6FEFF74E68CBA48ED0E60BDCD6800E1`.

## 0.0.171 endpoint footprint and sector coherence

The completed v0.0.170 run is
`<game-directory>\logs\deathtrap-native-20260802-002443-733-pid39140.log`.
The user reports two remaining behaviours: in narrow spaces the camera can
cross a wall, while stationary circles in an open area retain repeatable
presentation hitches. The log separates them. During the final circles the
focus is fixed at `-12899/-1450/16356`; every sampled source tick has
`submitted == after_desired == after_resolved == published`, full radius,
`blocked=0/0`, `mesh=0/0/0` and `exact=1`. The modern source orbit is therefore
already circular. The visible discontinuities are the 126 rejected synthetic
Presents, including repeatable midpoint-only `partial_black` regions that
recover in the exact frame.

The v0.0.170 strict predicate checked the seven 30-unit offsets around the
*focus*. Those are the retail `0x30910` visibility samples, not the 96-unit
camera sphere used by scene collision. It could consequently authorize a
camera centre whose own footprint straddled a wall. Version 0.0.171 retains
all seven focus traces and additionally resolves and traces the endpoint
centre plus six cardinal points at radius 96. Failure of any focus or endpoint
sample leaves final ownership with the ordinary native resolver.

Static disassembly also closes a state-transaction gap. Retail `0x2F028`
passes the camera node translation and `controller+0x200` seed to `0x06130`,
then `0x2F041` writes the returned sector to `camera_node+0x100`. The modern
exact commit previously replaced controller history, node translations and
published matrices but retained the sector of the displaced native candidate.
It now resolves the committed endpoint and writes both the endpoint-sector
seed and camera-node sector in the same verified transaction. Synthetic
passes likewise resolve `camera_node+0x100` from their actual interpolated
translation immediately before rendering and restore the exact snapshot's
sector afterward. No camera-cache callback is replayed and no synthetic
sector survives the render transaction.

The x86 build, spring-arm state test, DirectInput proxy smoke test and
installer CRLF test pass. Installed/build/dist SHA-256 is
`4BB179E196E6C668B240835290D56E506B0CA8DD9CDA7FA671DF87781B069F9C`.

## 0.0.172 full-footprint spring and pre-native scene ownership

The completed v0.0.171 run is
`<game-directory>\logs\deathtrap-native-20260802-004112-811-pid11040.log`.
It contains no `camera_midpoint_sector` change or failure. The synthetic
sector hypothesis therefore does not explain the remaining final-circle
hitches. The source trace instead records the same qualified mesh on every
revolution: resource 12708 is a 24-triangle, radius-180 object. Its sweep
contracts the desired arm from about 1400 to 1378, but the later native
fixed-camera resolver replaces that already safe endpoint and changes its Y
coordinate by roughly 156 units. The post-native mesh sweep then sees no
contact at the displaced point, so neither collision owner restores the
original modern solution.

Version 0.0.172 does not hide resource 12708 or add an object ID/size filter.
When the desired scene sweep has selected a contracted endpoint and the
post-native sweep has no conflicting contact, the actual rounded endpoint is
rechecked with the complete native footprint, minimum radius and current scene
occupancy. It then exact-commits as `PRE_NATIVE_SCENE`, preventing the retail
tail from becoming a second radius/height owner.

The ceiling penetration has the complementary cause. Prior versions used the
retail any-one-of-seven `0x30910` result for spring contraction and reserved
the all-trace endpoint footprint for full-radius exact ownership. Thus an
orbit could be treated as unblocked when only one offset ray cleared the
ceiling. All modern source, binary-clip, validation and synthetic-phase room
queries now use the complete focus-plus-96-unit-endpoint footprint. The weak
retail predicate remains documented but no longer authorizes a modern camera
position.

The x86 build, spring-arm state test, DirectInput proxy smoke test and
installer CRLF test pass. Installed/build/dist SHA-256 is
`9F25C74A6BDDD5435917DC999BB27B6BABE524ADEB93153074AE3428FC197A54`.

## 0.0.188 owner-target convergence and retail mode 4

The scripted owner pointer at `controller+0x1B8` is broad, but its associated
`controller+0x180` bit `0x20` has a precise narrower meaning. In
`Dungeon.dll+0x2E9CA..0x2EA9D`, an active owner updates the requested camera
target. When the residual distance is above 110, the engine advances toward it
and sets bit `0x08`; once the residual is at most 110 it sets bit `0x20` and
clears `0x08`. The no-owner branch clears `0x20`. Thus `0x20` is verified
owner-target convergence, not camera mode 3, a cutscene flag or controller
input. Combined with a recent explicit operate sequence and stationary player,
its rising edge identifies delayed owned reveals that do not change owner
inside the former 1500 ms window.

Retail first person uses dispatcher mode 4. Synthetic presentation must not
run mode-4 eye transforms through the modern third-person pivot/minimum-radius
validator: rejection freezes both inserted phases at the previous full matrix.
Mode 4 instead follows the generic translation/quaternion interpolation path.

## Animated head attachment probe (v0.0.194)

The custom first-person eye cannot be made body-relative by tuning another
constant against the player root or `camera_focus`. Live v0.0.192/v0.0.193
evidence shows those anchors remain stable while the rendered back, head and
arms move through them. The player subtree captured for render interpolation
already contains the animated joint world matrices, but packed assets and the
retail binary expose no reliable textual head-bone name.

Disassembly of the retail mode-4 wrapper at `Dungeon.dll+0x31190` shows why it
cannot supply the missing mount. The wrapper mutates body visibility before
calling the inner pose routine at `+0x31270`; the inner routine updates camera
angles from mouse input and forms its eye from the coordinate reached through
`controller+0x114`, plus fixed trigonometric offsets (and a fixed vertical
subtraction). It is actor-relative rather than a verified head-joint transform.

The v0.0.194 `HeadJointProbe` is therefore source-snapshot-only. It enumerates
descendants of the resolved player render node, filters a generous upper-body
neighbourhood, and logs the 24 highest/central candidates every ten source
ticks. Address, parent, depth, child count, resource, flags, local/world
translation, player-relative translation and bounds metadata are retained.
The camera output itself remains behaviourally identical to v0.0.193; the next
implementation must be selected from a short animated capture rather than
from another static height guess.

## v0.0.194 live skeletal result and v0.0.195 mount

The capture is
`<game-directory>\logs\deathtrap-native-20260802-143017-803-pid23120.log`.
It contains 58 sampled source ticks and 1,399 head-probe records across idle,
forward/reverse locomotion, falling/room transitions and melee animation.

The resolved player render node is `05DDFA18` for this process only. Its
stable central upper-body path is:

`player -> 05DDF8E8 -> 05DDDA08 -> 05DDF7B8 -> 05DDD8D8 -> 05DDF688`.

`05DDF7B8` has three children (left arm, right arm and central neck branch).
`05DDD8D8` is the zero-transform central anchor. `05DDF688` is the head: depth
5, one child, local `0/71/34`, bound radius 76. Its descendants
`05DDF558 -> 05DDF428 -> 05DDF2F8 -> 05DDF1C8 -> 05DDF098` form the descending
braid. During streaming the head resource changes from `0xF40` to `0x1048`
without changing address, topology, local translation or bound, proving that
neither resource ID nor allocation address is an acceptable persistent key.

v0.0.195 scores the topology and local/bound invariants, then reads the chosen
head and player world matrices live at the mode-3 source update. The previous
snapshot supplies only the verified topology and focus-to-root relationship;
current root translation comes from current `camera_focus`. The head-relative
animation offset is therefore retained without inheriting skeletal rotation.
Collision origin, exact publication and subsequent x2/x3 endpoint
interpolation all use the head-mounted source pose.

## v0.0.195 live rejection: neck translation is animation (v0.0.196)

The live log is
`<game-directory>\logs\deathtrap-native-20260802-144017-984-pid29268.log`.
It records 88 successful immersive publications but 215
`valid=0 reason=HEAD_JOINT` failures, followed by visible
`fallback=MODERN_THIRD_PERSON` transitions. At tick 1110 the verified head is
still `05E00688`, local `0/71/34`, radius 76. Its resource-free parent
`05DFE8D8` is no longer local identity: it is `28/0/14`; at tick 1120 it is
`-32/0/23`. When it returns to zero at tick 1130, publication succeeds again.

Thus the parent is a real animated neck/head anchor, not a static dummy whose
translation may be validated as zero. The persistent identifier remains the
head's own invariant local transform plus radius, depth, child count,
resource-free parent and three-child grandparent. v0.0.196 retains all those
checks and permits the parent to animate. No latch or last-known node is
needed, and no failure-driven camera-mode cut should remain in these movement
states.

## v0.0.196 live pitch cutoff and v0.0.197 correction

The live log is
`<game-directory>\logs\deathtrap-native-20260802-144838-924-pid36792.log`.
It contains zero `HEAD_JOINT` failures, confirming continuous skeletal
resolution during locomotion. At the end, however, custom first person emits
repeated unexplained fallbacks above roughly +60 degrees, returns successfully
at pitch `59.86`, then repeats below -60 and returns at `-59.76`.

The source is `BuildImmersiveHeadMountedPose`, not collision or camera-mode
arbitration. It rejected `hypot(camera_forward.x, camera_forward.z) < 0.5`.
That value is `abs(cos(pitch))`, so a normalization guard intended for a zero
vector rejected the complete 60..75-degree portion at both ends. v0.0.197 uses
an epsilon (`1e-6`) appropriate to true degeneracy. The configured limits are
only +/-75, so runtime horizontal length never falls below about 0.259.

## v0.0.198 second-character and input-ownership defects (v0.0.199)

The mixed-character run is
`<game-directory>\logs\deathtrap-native-20260802-190049-288-pid34584.log`.
Red Lotus publishes continuously from head node `05DED688`. After the player
object changes from `05DEDA18` to `05DAE8B8` (Chaindog), selecting F10 emits
`valid=0 reason=HEAD_JOINT` followed by the modern-third-person fallback.

The resolver incorrectly treated Red Lotus's one-child head as a universal
skeleton invariant. That child is the first of five braid segments; Chaindog
has no braid and his head is a leaf. Version 0.0.199 accepts zero or one child
while retaining the verified local translation, bounds, depth, resource-free
neck parent and three-child chest grandparent. A resolution failure in the
normal compact preset now records one candidate set per player object rather
than requiring the heavy periodic probe.

The same run proves an independent ownership leak. After F10 selects
`MODERN_THIRD_PERSON`, `dispatcher_heading` continues to drive the old target
with magnitude 1.0. DirectInput keyboard samples are fresh, but the shared
camera-relative heading intent is persistent atomic state. Version 0.0.199
releases that intent, magnitude, controller and start timestamp on every
immersive exit. Heading alignment is also published only after a valid head
endpoint commits, so a fail-closed third-person fallback cannot lock native
keyboard steering.

Finally, physical DirectInput mouse Y and the head-mounted look-at publication
have opposite effective pitch signs in the live view. Version 0.0.199 reverses
only the mouse contribution while custom head view is selected. Accepted
third-person mouse orbit, right-stick pitch and the `InvertY` option remain
otherwise unchanged.

## v0.0.199 measured Chaindog profile (v0.0.200)

The v0.0.199 acceptance log is
`<game-directory>\logs\deathtrap-native-20260802-192252-139-pid5676.log`.
It confirms the keyboard-exit and physical-mouse fixes, but Chaindog still
fails head resolution. The new one-shot production probe captures all 23
upper-body descendants at tick 1227.

The unique central head candidate is `05E97B18`: depth 5, zero children,
resource `0xF21`, local `-1/100/21`, relative world `13/514/-14`, radius 90.
Its parent `05E96358` is a resource-free neck anchor and its grandparent
`05E97C48` is the three-child chest branch. No other logged node satisfies
that complete structure. Version 0.0.199 correctly allowed the leaf topology
but retained the female-derived local-Y ceiling 90, rejecting Chaindog's
measured Y=100.

Version 0.0.200 raises only that geometry ceiling to 105. Tests now use the
actual female `0/71/34`, radius-76 braid head and male `-1/100/21`, radius-90
leaf head, plus rejection immediately beyond the observed envelope. Pointer
addresses and resource IDs remain diagnostic evidence rather than runtime
keys.

## v0.0.200 action-frame rejection (v0.0.201)

The v0.0.200 acceptance log is
`<game-directory>\logs\deathtrap-native-20260802-192930-171-pid18256.log`.
Chaindog's standing head view now succeeds, but attacks and jumps alternate
between valid immersive publication and `HEAD_JOINT` fallback.

The one-shot candidate capture at tick 194 identifies the same male head node
`05DE2688` with unchanged depth 5, zero children, resource-free neck parent,
three-child chest grandparent and radius 90. Only its local pose changed from
the standing `-1/100/21` to `-1/99/13`. The v0.0.200 Z>=15 gate therefore
rejected the actual head at Z=13. This proves local translation is animation
state, not identity, for Chaindog; merely widening its range would continue
the same failure mode on unobserved attacks or on Red Lotus.

Version 0.0.201 removes local XYZ from head identity completely. Resolution
uses only stable bounds and topology, and it succeeds only when exactly one
candidate matches. It no longer scores ambiguous nodes by closeness to a
female rest pose. The existing live candidate sets for both characters each
contain exactly one structural match, so attacks, jumps and other animation
poses cannot change the selected node while a genuinely ambiguous future
skeleton still fails closed.

## Native side-step experiment (v0.0.202, invalidated)

Static disassembly closes the lateral-speed path without guessing at actor
coordinates. The input resolver at `Dungeon.dll+0x871B0` maps action 7/8 to
controller flags `0x100/0x200`. The shared ground dispatcher reaches
`+0x5F4F0/+0x5F520`, which install the left/right side-step state with a
direction value of `-1/+1` at controller offset `+0x19C`.

Both states schedule `+0x5F650` every two source ticks. The initial v0.0.202
interpretation treated its signed `150/50` arguments to
`+0x442F0/+0x443B0` as translation velocities. Complete callee disassembly
invalidates that interpretation: both functions converge on `+0x44280`, whose
tail adds the resulting values to render-node fields `+0x1C/+0x18`.
`+0x1C` is the already verified Q10 actor heading. These are angular response
channels, not the root-position/collision writer. Scaling them cannot make
side movement materially faster.

Runtime also exposes the independent design failure: J/K enters a dedicated
side-step state and excludes the W/S locomotion state. Mapping A/D to J/K can
therefore express either lateral movement or forward/back movement, never a
combined vector. Version 0.0.202 is retained only as historical evidence and
is replaced completely in 0.0.203.

## Immersive vector locomotion through native root motion (v0.0.203)

The common `Dungeon.dll+0x82750` ground-state dispatcher already encloses the
retail action resolver, active locomotion callback, animation root motion and
collision publication. Sustained forward/back callbacks observed underneath
that dispatcher mutate the live render root during the `+0x810A0` dynamic
update stage. This is the native movement transaction; no overlay transform
write is required.

Version 0.0.203 leaves A/D neutral in immersive view and keeps W or S as the
native locomotion driver. A purely lateral request supplies W. Immediately
before the original dispatcher runs, the patch saves the visible body heading
and uses the canonical `+0x44DD0` dual render/collision writer to give the
native transaction the requested camera-relative movement heading. For S,
the temporary actor heading is rotated by half a Q10 turn because backward
root motion travels opposite the actor course. After the original dispatcher
has completed action resolution, animation, movement and collision, the same
canonical writer restores the saved eye-facing body heading before the tick is
published.

Keyboard publishes the complete digital W/S+A/D vector. XInput publishes the
complete circular left-stick vector but drives the retail joystick's vertical
axis with its magnitude and the appropriate forward/back sign. Pure lateral
input therefore receives ordinary forward walking/running speed, diagonals
remain simultaneous, Shift and the existing stick run hysteresis retain their
normal meaning, and no player coordinate or action-table field is authored by
the overlay.

## Cached-basis root-motion boundary (v0.0.204)

The v0.0.203 gameplay log invalidated its final assumption. The requested
temporary Q10 heading was present, but the measured root delta remained
`0/0/276`: `+0x44DD0` does not rebuild the matrix consumed by root motion in
that transaction.

Complete static tracing of the active walk callback `+0x602A0` reaches
`+0x451E0`, which calls `+0x32430` at exactly `+0x45232` and `+0x45273`.
That writer transforms local X/Z through the cached horizontal basis at node
`+0xD0/+0xD8/+0xE8/+0xF0`, then adds the result to the world root. Version
0.0.204 patches only those two player-animation callsites. It converts the
original local root delta to world space, preserves its exact magnitude,
chooses the requested camera-relative world course, solves the same 2x2 basis
back to local X/Z and calls the original writer. Native animation, root writer
and subsequent collision remain authoritative; there is no direct coordinate
write and no temporary actor-heading transaction.

## Complete player animation-root transaction (v0.0.205)

The v0.0.204 gameplay run proved that `+0x451E0` is not the complete root
transaction: A/D produced only a weak lateral component while the player kept
moving primarily forward. The two patched calls scale the animation delta by
approximately `19/128` or `42/128`. The full, unscaled animation-root delta is
applied separately by `+0x84230`, whose `+0x84268` call invokes the same
`+0x32430` writer with the live player render root.

Version 0.0.205 treats `+0x84268/+0x45232/+0x45273` as one signature-checked
player-only callsite set. All three inputs use the same requested world course
and live-basis inverse. Consequently pure A/D has no retained forward
component, while W/S+A/D diagonals redirect the complete native walk/run root
magnitude rather than only its small supplemental fraction.

## Actor/collision and root-vector transaction (v0.0.206)

The v0.0.205 live run invalidates the assumption that redirecting all three
animation-root writes is sufficient. The routed samples and callback-local
root aggregation are predominantly lateral, yet the complete source motion
still advances primarily along the native forward course. Static ordering
explains the discrepancy: `+0x82750` invokes the actor/collision resolver
`+0x57760` before the later animation callbacks publish their root deltas.
The collision half therefore consumes the eye-facing forward course while the
root half is redirected sideways, and native synchronization converges the two
back toward forward movement.

Version 0.0.206 combines the previously separated verified mechanisms. Before
the original `+0x82750` transaction, `+0x44DD0` temporarily publishes the
requested native W/S course to both render and collision headings. The three
cached-basis root callsites then publish the actual requested world course.
After the original transaction returns, `+0x44DD0` restores the preserved
eye-facing body course. Backward W/S state uses the opposite temporary
collision course while retaining the requested root world direction. No
coordinate, speed or collision result is written by the overlay.

## Correlated immersive movement truth probe (v0.0.207)

The v0.0.206 aggregate was not sufficient to validate visible A/D movement.
`stage_810A0` accumulated as many as 120 source ticks and could combine W, S,
A and D intervals, while the sampled transaction and root-route messages were
not tied to the final root endpoint of the same source tick. A predominantly
lateral aggregate therefore did not prove that the user's A/D interval was
the lateral contributor. Live observation confirms that A/D still appears as
ordinary forward locomotion with only a very weak lateral drift.

Version 0.0.207 deliberately changes no input, movement, animation, collision
or camera behaviour. At the complete `+0x810A0` boundary it records one
compact `immersive movement_truth` sample on an input transition and every
fifteen active samples. Each record contains the physical keyboard/stick
vector, requested course, restored body course, exact render-root delta, its
projection onto requested and body-forward courses, and simultaneous cached
object/bounds deltas. This makes the next short run capable of identifying
whether direction is lost inside the dispatcher, by a later native writer, or
between the render root and the authoritative gameplay object.

## Complete movement-stage root ownership (v0.0.208)

The correlated v0.0.207 run identifies the missing forward writer. During an
A hold, sampled `+0x810A0` calls redirected approximately `-5/-1` units in
X/Z, but successive source endpoints advanced about `-90/+570` over fifteen
ticks. D showed the mirrored result. The weak redirected contribution was
real, but most forward displacement came from other active movement states.

Static enumeration finds twenty direct calls to the common `+0x32430`
local-to-world root writer, not three. Runtime callback probes specifically
show `+0x60840`, `+0x7E530` and `+0x7E980` contributing root displacement
outside the short `+0x82750` transaction used by v0.0.206. Therefore adding
another guessed callsite cannot make the implementation complete.

Version 0.0.208 replaces the three rewritten calls with two signature-checked
hooks: the complete `+0x810A0` movement stage owns the temporary requested
actor/collision course, and the common `+0x32430` writer redirects every
horizontal root contribution made for the verified live player node during
that exact thread-local stage. Calls for equipment, other actors and every
call outside active F10 locomotion pass straight through. Direct collision
projection writers such as `+0x68390` remain native and are not redirected.

## Hidden between-tick root writer probe (v0.0.209)

The v0.0.208 run proves that both full-stage hooks are active and that every
root change observed inside `+0x810A0` follows the requested A/D course.
Nevertheless successive stage-entry endpoints still advance roughly 35--40
units per tick along body-forward, while the stage itself contributes only
4--5 lateral units. None of the 95 instrumented calls inside `+0x57CD0`
changes the root after `+0x810A0`; the remaining writer therefore runs after
the gameplay DLL update returns and before its next invocation.

Version 0.0.209 changes no locomotion behavior. After `cache_1CCC0` has copied
the accepted root, a one-shot read-only page watch remains active until the
next `+0x810A0`. Its vectored handler records the first instruction that
writes one of the three root coordinates, restores the original protection
and permanently disables the probe for the process. Non-root writes on the
same page are single-stepped and re-armed without being logged. This yields
the actual hidden writer address instead of expanding movement ownership into
unrelated host/render code.

## Original-frame movement phase probe (v0.0.210)

The v0.0.209 run is conclusive about the symptom but not the writer. During
pure A/D, `+0x810A0` still contributes only 4--5 units almost exactly along
the requested lateral course, while successive stage-entry roots advance
about 570 units over fifteen samples along the old forward course. No
`immersive hidden_root_writer` record appears. Because v0.0.209 did not log
whether the vectored handler and page protection were successfully armed,
absence of a hit cannot identify an instruction or ownership boundary.

Version 0.0.210 changes no input or locomotion behavior. It records one
same-frame `immersive phase_truth` transaction around the original
render/present scheduler call and divides its total player-root delta into
three disjoint intervals: scheduler entry to `+0x810A0`, the complete
`+0x810A0` stage itself, and stage exit to scheduler return. Records occur on
input changes and every fifteen active samples. The protected-page diagnostic
also emits one `hidden_root_watch status` record for armed or failed setup.
Together these records establish the real phase boundary before any further
movement ownership is changed.

## Outer gameplay-update and head-entry direction (v0.0.211)

The v0.0.210 run proves the page watch was armed successfully. It still
records no watched write. Every sampled original render/present call has
`stage_seen=0` and zero player-root delta, so neither source interpolation nor
the exact renderer owns the missing forward displacement. The movement stage
runs earlier in the gameplay update.

Version 0.0.211 moves the diagnostic boundary to the outer `+0x57CD0` update.
It partitions each active update into previous-tail-to-entry, entry-to-
`+0x810A0`, the complete stage, and stage-to-tail deltas. All four sampled
render-node addresses are logged as well, because a changing player render
node would explain why an armed watch on the preceding node sees no write.

The same run independently confirms a head-view transition defect. F10 kept
third-person radial yaw `173.79` degrees even though the live body heading
`521/1024` requires a behind-body radial yaw near `3.16` degrees. After the
user manually rotated to `2.43` degrees, view and body courses converged. On
head-view entry v0.0.211 therefore converts the live Q10 body heading to its
equivalent radial yaw. It does not alter pitch or ongoing mouse/stick input.

## Interpolation cache refresh is part of movement (v0.0.212)

The v0.0.211 outer-update trace removes the last ambiguity. The render node is
identical at every boundary. Entry-to-stage is zero, `+0x810A0` contributes
the requested lateral `-5/0/0` or `+4/0/-1`, and the post-stage interval
consistently adds about `+2/0/+39` along the old body-forward course.

Static ordering places the overlay-hooked `+0x80520 -> +0x80600` scheduler
between callsite probes 11 and 12. The original `+0x80600` routine was already
proved root-neutral by v0.0.210. The remaining writer is the overlay's early
`RefreshCurrentRenderCaches` call: its explicit `g_scene_cache_update`
publishes the large animation-root contribution before synthetic capture. At
x2/x3 that refresh replaces the later stamped retail refresh, so the
contribution is legitimate source motion, but it previously executed outside
the immersive course transaction.

Version 0.0.212 runs only this early scene-cache update through the same
validated live-player heading/root-routing transaction as `+0x810A0`, then
restores body heading before camera-cache work. It does not author coordinates
or change magnitude. `update_truth` remains enabled for the validation run.
Head-view entry now also resets inherited third-person pitch to neutral zero;
v0.0.211 proved a retained `55` degrees caused the reported upward opening.

## Immersive lateral handedness (v0.0.213)

The v0.0.212 user run accepts both the neutral head entry and full-speed
strafe, but reports A/D mirrored. The endpoint trace is unambiguous: A
(`keyboard=-1/0`) produces total `+34 X`, while D (`keyboard=+1/0`) produces
`-36 X`. W/S remain visually correct. Version 0.0.213 mirrors only the lateral
component at the shared immersive keyboard/stick intent boundary. Root
magnitude, longitudinal input, camera orientation and non-immersive controls
are unchanged; diagonals inherit the corrected side automatically.

## Head-to-third-person yaw handoff (v0.0.214)

The v0.0.213 user run accepts keyboard movement and immersive entry. On exit,
third person reuses the final head yaw unchanged. Live records show, for
example, head yaw near `-1.07` degrees followed by a third-person orbit at the
same angle. Those modes consume the shared angle from opposite sides of the
focus, so the unchanged value places the third-person camera in front and
reverses the apparent view.

Version 0.0.214 adds exactly one half-turn when transitioning from custom head
view to modern third person. The conversion is deterministic and covered for
zero, wrapped half-turn and invalid input. Head movement, camera input and all
other mode transitions remain unchanged.

## Production movement diagnostics cleanup (v0.0.215)

The user accepts the complete v0.0.214 immersive camera and locomotion result.
Version 0.0.215 changes no camera or movement behavior. It stops installing
the 95 direct gameplay-loop callsite probes used by v0.0.207--v0.0.214 and
stops flushing their `movement_truth`, `update_truth`, page-watch and periodic
callback statistics into normal support logs.

The two measured dynamic collision-projection callsites and the vtable bridge
remain installed because they feed exact player-contact handling rather than
reverse-engineering telemetry. Their dispatcher now bypasses unrelated
callbacks even when compact `DebugLog=1` support logging is enabled.
