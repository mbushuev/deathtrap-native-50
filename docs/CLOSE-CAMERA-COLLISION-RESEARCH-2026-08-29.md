# Close-camera collision research

Date: 2026-08-29

## Decision

The distance-dependent final-position offset tested in `0.0.222` is rejected.
It changes an endpoint after the owned room and scene collision solve. Although
the shifted point is checked again against those two environment channels, the
check deliberately excludes the player render hierarchy. At a sufficiently
short collision radius, the endpoint can therefore move through the player's
body and remain between animated limbs.

The replacement must be a single coherent camera rig. Framing changes belong
before collision resolution, and character proximity must be represented by a
stable analytic volume rather than animated render meshes.

No new gameplay build should be published from this document alone. The first
runtime step is diagnostic-only and must measure whether each proposed shot is
valid before it is allowed to affect the visible camera.

## Runtime evidence from `0.0.222`

The completed run is:

```text
<game-directory>\logs\deathtrap-native-20260829-174818-869-pid38540.log
```

The close-framing layer was accepted while the radial camera distance fell to
approximately `188`, `88`, `83`, and finally `0` world units. Its maximum
120-unit world-vertical displacement then placed the camera inside the visible
character. This is consistent with the implementation, not a missed wall:

- the full-owned room sweep remained valid;
- the scene-triangle sweep remained valid;
- the scene sweep excludes the player and every ancestor/descendant node;
- the close-framing layer changed only the already solved endpoint;
- no analytic player volume participated in the final validation.

The player-tree exclusion itself remains correct. Including animated body,
weapon, braid, and projectile nodes in ordinary scene collision previously
made attachments and animation pose changes behave like world obstacles. The
missing primitive is a deliberately simple character exclusion volume.

## Why SpiderManModernFix behaves better

The SpiderManModernFix reference implementation examined during this research
has a different ownership boundary. `HookedCameraNormal` writes only the
retail camera pitch and yaw fields immediately before calling the original
`CCamera::CM_Normal`. The original game continues to own camera distance,
position, collision, and special-camera states.

That gives Spider-Man one positional solver. The patch supplies modern input
to the native camera instead of placing a second collision system after it.
The relevant project documentation explicitly records that retail
swing-distance and collision behavior remain active.

Deathtrap cannot copy that implementation directly. Its ordinary retail
mode-3 controller returns fixed/rail viewpoints, misses visible scene props,
and historically introduced position-ring oscillation. Full gameplay-camera
ownership is therefore still required. The transferable rule is narrower and
more important: **one system must own the complete final gameplay pose**.

### What the Spider-Man decompilation reference actually provides

The examined decompilation tree leaves `CCamera::CM_Normal()` as a stub in its
main `camera.cpp`. The generated `thps2-stuff/decls.h` records addresses, local
variables and type layout for `ReactToCollision`, `MoveToDesiredPos` and
`CM_Normal`, but does not contain their executable statements. It is useful
structural evidence, not a camera implementation that can be translated line
by line.

The reusable part is the implemented collision architecture in
`m3dzone.cpp`: Spider-Man builds one `SLineInfo`, traverses the zoned
environment and optional environmental-object list, then returns one nearest
line result. Camera-specific masks are selected before that common query.
This reinforces the single-query/single-result design rule, but it does not
provide Deathtrap-compatible geometry, sectors or object layouts.

## Deathtrap native gameplay-collision audit

Static analysis of the supported Steam `Dungeon.dll` proves that the old
label `actor/collision resolver` at `+0x57760` was too coarse. That function
is a dispatcher. It performs ground recovery, builds contact samples, invokes
a state-specific callback, applies follow-up corrections and publishes the
result. The underlying geometry is split across several query families; there
is no single Deathtrap equivalent of Spider-Man's `M3dZone_LineToItem`.

### Vertical support query

`Dungeon.dll+0x69910(point, sector, result)` is a three-argument wrapper over
`+0x699D0(point, sector, include_objects=1, result)`. It has approximately 48
call sites across player, enemy and object code, but its meaning is narrower
than a general 3D ray cast:

- `+0x69A90` finds the static support plane and height while traversing linked
  sector data;
- `+0x69DF0` asks `+0x66910` for sector objects whose collision descriptor
  contains mask bit `1`;
- `+0x69EF0` evaluates the collision resource returned by `+0x4A110` and
  selects an object support surface;
- `+0x699D0` keeps the higher static or dynamic result.

The leading result fields are now structurally identified:

```text
+0x00  selected support height
+0x04  dynamic object pointer, or zero for static support
+0x08  dynamic support-plane/contact pointer
+0x1C  static support-plane pointer, cleared for a dynamic winner
```

This query explains how moving blocks can support actors using their real
collision resources. It is not sufficient for a camera spring arm because it
projects vertically and does not return the first wall along an arbitrary
three-dimensional segment.

### Static segment and portal traversal

`+0x31AA0` and its worker `+0x31B30` clip a displacement through sector plane
lists. Plane intersections use the shared Q14 segment primitive at `+0x4E6C0`;
linked planes may return the adjacent sector. This is the native horizontal
world/portal half used by actor contact construction. It is related to, but
not interchangeable with, the camera visibility predicate
`+0x30910 -> +0x4E760`.

### Actor-volume and object response

The state callback `+0x54D00` is also not a leaf query. It initializes a
0x64-byte contact transaction and runs the ordered stages `+0x545E0`,
`+0x54720`, `+0x547C0`, `+0x54A50` and `+0x54B70`. Those stages consume the
actor's prepared contact samples and may directly change actor position,
ground state, animation state or callbacks.

The lower functions `+0x520C0`, `+0x524B0`, `+0x55AB0`, `+0x55D90` and
`+0x55E90` perform bounded sector/object tests and calculate collision
projections, but their public contract includes actor-owned sample arrays,
height ranges, state flags and output writers. Calling `+0x54D00` with a fake
camera actor would therefore be unsafe and would not be a read-only query.

### Consequence for the camera

The promising reusable asset is the collision geometry and sector-object
membership, not the complete actor resolver. A safe replacement investigation
must combine:

1. native static sector/portal planes;
2. dynamic objects selected from the sector lists by collision mask bit `1`;
3. the objects' real collision resources returned by `+0x4A110`;
4. an overlay-owned finite camera sweep that produces one nearest contact and
   never invokes actor state transitions.

This would replace render meshes as collision truth while preserving a pure,
idempotent camera solver. Render triangles can remain diagnostic comparison
data until the native resource parser is verified.

### Required diagnostic before behavioral integration

The next build must remain visibility-neutral. On a bounded sample of source
ticks it should record, for each candidate object already considered by the
scene sweep:

- scene node and render resource;
- native sector membership;
- collision descriptor flags and whether mask bit `1` is present;
- collision-resource pointer and bounded shape/plane counts from `+0x4A110`;
- current render-mesh hit distance beside the native collision-resource
  candidate distance;
- explicit rejection reason when no native collision shape is available.

No native actor callback, animation transition, object writer or camera pose
is changed in this stage. A short run around the flag, a thin door, a moving
block and one ordinary wall is enough to determine whether native collision
resources cover every problematic camera obstacle.

The diagnostic is now implemented behind
`[Diagnostics] NativeCollisionProbe=1`. It reproduces the two bounded
sector-list walks used by `+0x66910` rather than invoking that routine's
capacity-less 40-pointer output buffer. For each mask-bit-1 object it calls
the engine's `+0x4A110` collision-resource getter, validates the bounded group
layout, sweeps the 96-unit camera sphere against native convex plane groups,
and records the corresponding render-mesh result. Sampling is limited to once
per five source ticks, 256 unique objects and 24 changed/contact details per
sample. The accepted camera position is not read back from or written by this
probe.

### First native-resource capture

The first 1,506-source-tick capture produced 302 bounded samples and no
invalid read, truncated list or invalid collision-resource layout. Two native
mask-bit-1 objects were present in the tested dynamic-block area. Their live
resources alternated between the engine's transformed resource buffers but
kept stable object identities; the objects exposed one convex group each with
six and seven planes.

The comparison is decisive:

- 21 of 21 samples where both systems reported an exterior nearest contact
  agreed to the logged 0.1 world-unit precision;
- 24 native samples began inside an expanded convex volume at distance zero,
  while the one-sided render triangles either missed it or selected a farther
  surface;
- 51 render-mesh-only contacts occurred outside those native gameplay-object
  lists, including ordinary/static scene geometry.

Therefore the native resource coordinates, plane orientation and 96-unit
camera-radius expansion are validated. Native gameplay collision cannot
replace every existing camera source: static room BSP remains authoritative
for walls/ceilings, and render meshes remain necessary for props omitted from
the gameplay collision lists. The native convex resources should instead
replace render-triangle ownership only for their corresponding mask-bit-1
dynamic object subtrees. Their solid inside/outside classification can then
handle a contained focus idempotently, while the stable gameplay object
pointer—not the alternating resource buffer—is the blocker identity. The
player object must be explicitly excluded before behavioral integration.

## Primary-engine comparison

### Unreal Engine spring arm

Epic's `USpringArmComponent` keeps children at a target arm length, retracts
them when a probe collides, and springs back when clear. Collision uses an
explicit probe size and channel. The desired location and the collision hit
location are inputs to one arm update; a later unrelated endpoint offset does
not bypass that collision result.

Source:
<https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/GameFramework/USpringArmComponent>

### Unity Cinemachine Third Person Follow

Cinemachine constructs a small multi-pivot rig:

1. root/follow target;
2. shoulder pivot;
3. vertical hand pivot;
4. camera behind the hand.

Its official implementation resolves collision twice: root to hand with a
slightly enlarged camera radius, then collided hand to camera with the normal
camera radius. Collision therefore bends and shortens the rig while keeping
the target visible. It also keeps separate damping values for movement into
and out of collision.

Sources:

- <https://docs.unity.cn/Packages/com.unity.cinemachine@2.10/manual/Cinemachine3rdPersonFollow.html>
- <https://github.com/Unity-Technologies/com.unity.cinemachine/blob/main/com.unity.cinemachine/Runtime/Components/CinemachineThirdPersonFollow.cs>

### Godot SpringArm3D

Godot sweeps a finite shape from the pivot and recommends the camera near-plane
shape when possible. Its documentation explicitly describes a bare ray as
inaccurate for camera collision. The player may be excluded from world
collision, but that exclusion does not imply that an arbitrary post-solve
camera displacement through the player is safe.

Sources:

- <https://docs.godotengine.org/en/latest/tutorials/3d/spring_arm.html>
- <https://docs.godotengine.org/en/stable/classes/class_springarm3d.html>

## Transferable invariants

The Deathtrap implementation must preserve all of these invariants:

1. User yaw and pitch define the requested view and never become collision
   output.
2. Framing is represented by rig pivots before collision, not by changing the
   final camera position afterward.
3. Room planes and qualified scene triangles are evaluated by one candidate
   planner and produce one accepted source pose.
4. The camera is a finite volume. The existing 96-unit swept sphere remains
   the conservative environment primitive.
5. The animated player hierarchy is not environment collision.
6. A stable analytic player exclusion volume rejects any third-person camera
   centre that enters the character.
7. The accepted boundary is idempotent: submitting the same pose on the next
   source tick cannot manufacture a fresh correction.
8. An unexpected current collision contracts immediately. Character-motion
   prediction may begin the same contraction earlier, but only along a
   connected room path and never beyond the current exact safe distance.
   Recovery is slower and only advances toward a currently validated desired
   pose.
9. Candidate identity is latched with hysteresis. A left/right or close-shot
   alternative cannot alternate every source tick.
10. Presentation interpolation consumes accepted source poses and must validate
    its temporal chord. It cannot choose a new collision response.
11. Authored lever/reveal cameras and custom first person remain explicit,
    separate ownership modes.

## Proposed Deathtrap rig

The first implementation should remain centred rather than forcing a permanent
over-the-shoulder composition:

- **root**: the stable engine camera focus;
- **body anchor**: the player root plus a stable torso-height offset;
- **hand/framing pivot**: derived from the root, requested yaw/pitch, and a
  radius-dependent vertical arm; this replaces the rejected final world-Y
  shift;
- **camera**: placed behind the hand at the requested third-person distance;
- **look target**: a stable focus/torso point, never the collided camera point.

The solver evaluates root-to-hand and hand-to-camera segments through the same
room and scene collision channels. Lower close framing is allowed only when the
resulting complete rig is valid. There is no separate `close_framing_position`
afterward.

## Analytic player exclusion volume

The player volume must not be built from individual animated triangles. A
vertical capsule can be derived from data already available to the patch:

- lower endpoint from the live player root;
- upper endpoint from the structurally resolved head centre, with a stable
  root-relative fallback when the head resolver is temporarily unavailable;
- a fixed/tunable body radius inflated by the camera radius;
- bounded root/head coherence checks so a bad skeleton sample fails closed.

This volume is used only to validate camera centres and transition chords. It
does not push against arms, the sword, braids, or embedded projectiles.

A candidate inside the capsule is invalid. The solver must not depenetrate it
to an arbitrary nearest body point, because that would create another moving
orbit centre as animation changes.

## Tight-space policy

Some corners have no physically valid third-person point between the player
and the environment. The current radial solver can legally reach zero distance,
but publishing that point is not a usable shot. The response must be a
deterministic shot policy:

1. direct centred rig;
2. current latched tangent/side candidate, if already active;
3. symmetric left/right tangent candidates scored by usable distance and
   angular cost;
4. a near-head emergency pose outside the player capsule;
5. if the emergency pose is impossible, one safe cut to the best validated
   candidate rather than an interpolated path through geometry.

The emergency pose is not the user-selectable custom first-person mode. It is
a temporary third-person collision state with third-person controls and a
stable orientation. If the visible body cannot be kept out of the near plane,
render suppression/fading must be considered explicitly instead of letting the
camera enter the mesh.

## Staged implementation

### Stage 0: restore the accepted baseline

Remove or disable the rejected `0.0.222` post-solve close-framing layer. Do not
carry its accepted offset into the next design.

### Stage 1: diagnostic-only rig and capsule

For each source tick, log without publication:

- requested direct rig position;
- root, hand, camera, and look target;
- player-capsule endpoints and radius;
- direct room/scene safe distances;
- player-capsule clearance;
- best alternative candidate identity and clearance;
- whether no valid third-person shot exists.

One short run at a wall and one corner is sufficient to size the volume and
verify coordinate conventions. No visual behavior changes in this stage.

### Stage 2: player-volume rejection (tested and rejected)

The analytic capsule is enabled in the owned candidate planner. A candidate
that would put the camera between the character's legs or through the torso is
marked unusable. The first close-shot family preserves exact yaw and changes
only pitch in ten-degree downward steps. This provides the requested lower
near-wall composition without reviving the rejected side-orbit behavior that
could change camera-relative movement direction.

Runtime disproved this stage: a lower pitch merely selected another part of
the player model when the room contained no valid third-person endpoint. It is
retained here as experimental history and is not present in the final code.

### Stage 3: pre-collision framing rig

Replace final-position close framing with the root/hand/camera rig. Resolve
both segments and publish exactly one complete accepted pose.

### Stage 4: deterministic tight-space presentation (implemented in 0.0.222)

The follow-up run
`deathtrap-native-20260829-181800-164-pid34584.log` provides that proof. It
alternated 11 times into and 11 times out of `no_solution`, produced 91 bounded
inside-capsule samples, and found a valid lower-pitch candidate only once. The
lower candidates merely moved the visible intersection from the legs toward
the torso when the room contained no player-safe third-person endpoint.

The initial automatic animated-head endpoint and the complete pitch-only
candidate family were rejected and removed because collision must not silently
change the visible camera mode or user orbit. Third person now resolves only
one user-owned yaw/pitch ray against world geometry.

The analytic capsule is retained strictly for character presentation. While
the accepted third-person camera volume is inside it, the renderer temporarily
marks the complete player subtree with the engine's native half-transparent
packet flag and restores every original node flag after the pass. Release uses
64 units of clearance for three source ticks. Custom and retail first person
ignore this state and render the complete character and weapon opaque.

The same version integrates the validated mask-bit-1 native convex resources
into the owned scene sweep. A native volume owns its mapped render subtree,
uses the gameplay object pointer as stable blocker identity, permits a ray to
leave an initial convex overlap only if the requested segment reaches the
exit, and continues to block later entries. Static room BSP and render-only
props remain separate complementary authorities.

### Follow-up: brief narrow-object contacts

The first transparency acceptance run shows that the remaining abrupt motion
is not caused by the fade. Collision publication can still alternate between
an almost collapsed arm and the full requested radius on adjacent source
ticks. Native-contact records likewise show short object identities and clear
states around the same intervals.

A general size exclusion was already rejected in the 0.0.165/0.0.166 work
because it could omit a thin closed door. The first follow-up therefore kept
the original blocking threshold and made the 192--384 two-axis-span class
temporal. Runtime showed why that was visually insufficient: 13 brief contacts
were rejected, but 30 records from narrow render-only meshes reached three
ticks and became the same abrupt collision again.

The revised policy classifies authority as well as size. A narrow render-only
mesh is persistently nonblocking; a narrow object with a native closed gameplay
volume must report the same stable blocker key for three consecutive source
ticks. Hard room geometry and larger objects never wait, and any such obstacle
behind ignored clutter remains in the solve. Internal rechecks on the same tick
reuse the first decision. Automatic `camera_radius_event` records now identify
every remaining change of at least 160 units as room, scene, native, cut or
near-pivot output, so the next runtime result can be attributed without a full
development log.

The next bounded run identifies the apparent flag collision precisely.
Resource 12613 is a `21x1071x676` render-only sheet and therefore exceeded the
two-axis hard threshold despite negligible thickness. The general clutter
classifier now handles both narrow objects and thin render-only sheets; a
native sheet remains a confirmed gameplay obstacle.

Most repeated full/zero events are independently marked `room=1`, with no
scene resource or native object. They occur while the player focus is fixed.
The root is the constant 96-unit sphere at a focus already inside its final
wall margin: an inward yaw blocks at fraction zero and an outward yaw clears
the entire arm. The owned room query now uses a 288-unit linear radius ramp
from the focus. This is a swept camera frustum approximation rather than a
wall exception: the radius reaches the original 96 units before ordinary
third-person framing, every room plane remains closed to the camera centre,
and deterministic tests cover inward, tangential, outward and repeated
accepted-boundary rays.

The next run isolates a separate doorway problem: accepted room clearance can
still fall by roughly 800--1500 units in one source tick. Delaying that current
collision would be unsafe, so the spring arm now predicts the player's focus
up to six source ticks ahead. Prediction is admitted only when a zero-radius
room sweep proves that the player's centre can reach the predicted focus
through connected portals. The future camera ray is then swept with the same
96-unit radius ramp as the current ray. A shorter future clearance starts a
bounded 256-unit-per-tick contraction, while the current exact clearance is
still applied as an unconditional clamp. Thus an inaccurate or late forecast
can only reduce smoothing; it cannot permit wall penetration.

The predictive acceptance run records 98 forecasted radius events, with 96
contractions falling in the intended 160--260-unit band. It also exposes an
older topological fallback as the dominant remaining instability: 39 of 48
presentation cuts discarded a revalidated near endpoint below 288 units and
published the farther direct candidate. On the following tick that far point
was commonly blocked again, producing sequences such as `337 -> 1539 -> 339`.

That fallback is removed. The complete room/scene revalidation now owns the
published endpoint even when it stops before a portal and is shorter than the
direct candidate. Near-pivot state is evaluated from this final distance
rather than from the unrevalidated direct clearance. The accepted near point
is already collision-safe and repeating the same solve is idempotent, while a
one-frame jump across disconnected radial regions is not.

The following run confirms the fallback removal: its old signature falls from
39 events to zero, and no radius extension of 160 units or more occurs. The 45
remaining events are all contractions. Motion prediction bounds 29 of them,
but repeated contractions against the same room-plane key recur roughly every
51 source ticks while focus-motion prediction is inactive. This periodicity is
the orbit rotating through the same boundary.

Room prediction now also extrapolates the measured source-to-source yaw/pitch
change up to four ticks and at most 18 degrees. The projected orbit is swept
from the current exact focus through the same room graph and radius ramp. A
shorter future clearance feeds the existing 256-unit predictive contraction;
the current sweep remains the hard clamp. Pure tests cover the angular cap and
prove that translation without orbit rotation cannot activate this path.

The angular-prediction acceptance run removes most large cuts, but the compact
support log still reports 53 player-fade boundary transitions in 1652 source
ticks. The remaining visible close-space vibration is below the 160-unit large
event threshold. Inspection of the spring state identifies a general feedback
loop: after blocked-release confirmation, the arm advanced exactly to the
latest hard-safe sample. Small room-query quantization or focus/orbit motion
could put the next sample just inside that radius, forcing an immediate
contraction, followed by another confirmed release toward the boundary.

Blocked recovery now keeps a 48-unit radial cushion inside the current safe
sample. Inward collision remains exact and immediate; only outward movement
while an obstruction is still present uses the cushion. Clear-space recovery
is unchanged. This makes an accepted blocked position stable under small
boundary motion instead of creating a new collision on the next source tick.
The support log additionally emits `camera_radius_oscillation` after three
meaningful radius-direction reversals within twelve source ticks, including
the current room/scene authorities. The detector is diagnostic only and lets
a short acceptance run expose any remaining sub-160-unit A-B-A cycle.

The first run with this detector records ten rapid reversal windows. In the
problematic second half, 21 of 31 large radius events are attributed to angular
prediction, compared with nine from player-motion prediction and one current
hard collision. The camera was being tested while running along a wall. The
old angular predictor inferred rotation from two requested world-space rays
relative to the live player focus. Those rays also rotate when the filtered
chase focus lags behind a translating player, even if user yaw and pitch are
unchanged. This repeatedly saturated the forecast at its 18-degree cap and
alternated predictive contraction with 64-unit blocked recovery.

Angular prediction now consumes the actual source-to-source yaw and pitch
deltas integrated from mouse/right-stick control. It applies those deltas to
the current requested orbit and retains the same four-tick/18-degree bound.
Chase-focus translation and player animation therefore cannot create an
angular forecast. Translational doorway prediction remains a separate path.

The next acceptance run disproves chase-focus contamination as the dominant
cause in the tested sequence: angular prediction still owns 27 of 38 large
events and nine rapid reversal windows are recorded. Its future rays commonly
report only `0--140` units of clearance while the current ray remains hundreds
or more than a thousand units clear. The look-ahead therefore speculates the
spring into near-pivot mode, recovery advances by 64 units, and another future
angle contracts it again. A future visibility estimate must not own this
topological camera-mode transition.

Angular look-ahead is now accepted only when its predicted clearance remains
at or beyond the 288-unit near-pivot exit distance. It is disabled completely
while near-pivot mode is active. Current room and scene collision remains
unconditional and may still contract below that distance, so wall closure is
unchanged; only speculative future angles lose authority over the close-camera
state. Ordinary-distance angular anticipation and translational doorway
prediction remain enabled.

The guarded run confirms that separation: large angular events fall from 27
to one and rapid reversal windows from nine to one. The remaining ending
sequence is no longer predictive. One identical room blocker and the same
`89.5`-unit safe distance recur at ticks 792, 814, 838, 862, 886, 910 and 934.
Between contacts, ordinary recovery waits ten ticks and then reaches roughly
449 units in 64-unit steps before the same orbit face contracts it again. The
22--24 tick recurrence is longer than the generic clear window but shorter
than a stable exit from the constrained corner.

Near-pivot recovery now uses a 30-tick evidence window for both clear rays and
an outward-moving blocked boundary. Current inward collision remains instant.
Repeated wall contact inside one 22--24 tick orbit therefore resets evidence
without changing the accepted radius. Once the current path stays genuinely
clear for 0.6 seconds at the 50 Hz source rate, the existing bounded 64-unit
recovery resumes. Ordinary-distance recovery retains the shorter 10/8-tick
policy.

The following run contains no rapid reversal record, but confirms a slower
exit/re-entry tremor while running along a wall. Player-motion prediction still
retained authority that angular prediction had lost. At tick 403, current
clearance is 237 units while future-focus clearance is 12.9; at tick 445 the
pair is 149.8/52.3; at tick 830 it is 264.4/17.9. Those speculative distances
can enter near-pivot even though the current collision has not reached the
120-unit entry threshold. Later, after the 30-tick hold, 64-unit recovery steps
can build several hundred units of visible separation before a real contact
contracts the arm again.

The no-speculative-near-pivot invariant now applies to every predictor.
Player-motion and angular look-ahead are both discarded below the 288-unit
near-pivot exit distance and while that mode is active. Only current collision
can enter it. Once 30 clear ticks prove an exit, recovery advances at 16 units
per source tick until near-pivot is released; ordinary-distance recovery keeps
its 64-unit step. Thus doorway anticipation remains available outside the
close-camera topology, while wall-running cannot alternate that topology from
future samples.

### Stage 5: interpolation audit

Validate source-to-source camera chords against rooms, scene meshes, and the
player capsule. A blocked chord uses a safe cut or endpoint hold; it never
creates a third pose or feeds source collision state.

The first run after the no-speculative-near-pivot guard records no rapid
radius reversal at all and only three large radius events in 1,229 source
ticks. The remaining reported wall-running tremor therefore cannot be
attributed to the former spring-arm recovery loop from the compact log alone.
At nearly identical accepted radii, the animated player capsule repeatedly
crosses its transparency boundary; that observation is consistent with either
actor/focus presentation incoherence or ordinary animation moving through a
stationary camera, but does not distinguish them.

The ordinary per-launch support log now samples the actual player-root and
camera translations seen by the renderer in the last synthetic phase and the
exact phase. `support_presentation` records renderer-vs-published errors, the
source-tick player-root and focus steps, drift of the root relative to the
focus, drift between the render root and the raw gameplay coordinate, contact
projection state, and the existing player-subtree coherence correction. It is
event-driven and rate-limited, with a denser bounded sample only inside 500
units. This diagnostic changes no camera, player, collision or interpolation
decision. A short reproduction can now prove whether the remaining image
tremor belongs to the spring arm, the source focus, the player render root, or
an overwrite between publication and raster entry.

The reproduction answers that question. All midpoint-player,
midpoint-camera, exact-player and exact-camera publication errors are exactly
zero: neither the renderer nor the synthetic transaction replaces the poses.
The camera focus follows the raw gameplay coordinate, while the animated
render root legitimately moves relative to both by as much as 84 units. That
motion becomes visually dominant only because the room solver repeatedly
contracts the rear spring all the way to the focus. The run contains four
long sampled zero-radius intervals (`144--168`, `288--312`, `608--720` and
`760--857`). Thus the remaining hard tremor is not a hidden spring
oscillation; it is an invalid presentation topology in which a stable camera
sits exactly inside a moving actor.

The first attempted presentation fix kept the contracted rear spring as
collision authority but placed the visible camera 120 units in front of the
focus. Runtime rejects that topology: it is an automatic first-person switch,
and the 30-tick near-pivot latch can hold it for hundreds of source ticks while
the player remains near a wall. The forward point is removed completely.

The subsequent attempt to hide the player subtree at a deeply contracted arm
is rejected as well. Although it removes animated geometry from the camera,
runtime makes the character disappear during ordinary wall approaches and the
visibility state can remain active for an unacceptable interval. Complete
player hiding is removed. The previously accepted half-transparent capsule
presentation is restored unchanged; neither rejected experiment remains in
the runtime. Further work must preserve both third-person topology and
continuous character visibility rather than disguising the zero-radius state.

The attempted render-root stabilization is also rejected and removed. In the
run `deathtrap-native-20260829-220812-638-pid1096.log`, the final failure holds
the camera at approximately `32--34` units from ticks 1608 through 1776 while
the player root, raw gameplay position and focus are all stationary. The
computed stabilization correction converges to zero, yet the visible problem
continues. The remaining motion is the user orbit rotating a camera that is
latched inside the character, not player animation requiring compensation.

That run exposes a temporal-policy error. The 30-tick near-pivot recovery hold
was designed for passive running along a wall, where it prevents the same face
from pumping the arm. During deliberate mouse/right-stick rotation, however,
each return to the wall resets the hold before a clear angular interval can
release the camera. Near-pivot release evidence now observes the current exact
direct clearance instead of the already-contracted published radius. Active
orbit input uses a separate two-tick confirmation and a bounded 96-unit
recovery step while the arm remains below 512 units. Any current wall contact
still contracts immediately. Passive wall-following retains the established
30-tick/16-unit policy.


## Manual acceptance matrix

Every behavioral stage must be tested with both mouse and right stick:

1. rotate 360 degrees in an open room;
2. back into a flat wall and rotate through contact;
3. stand in a two-wall corner and rotate continuously;
4. run through a narrow doorway and along a ceiling/stair boundary;
5. circle the flag, lever, a static block, and a moving block;
6. test both playable characters, sword attacks, jumps, and an embedded arrow;
7. verify authored lever/reveal takeover and return;
8. verify custom first person and return to the same third-person heading;
9. compare `x1` and interpolated rendering;
10. confirm that camera-relative movement never changes direction because a
    collision candidate changed;
11. confirm that close third person fades only the player, then restores it,
    while custom first person always shows the full body and weapon.

The change is accepted only if world collision remains closed, close character
intersection is visually readable without changing the camera direction, and
repeated rotation through the same angle does not create an A-B-A cycle.
