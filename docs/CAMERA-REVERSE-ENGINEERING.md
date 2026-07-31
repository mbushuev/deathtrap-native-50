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
