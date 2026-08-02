# Full gameplay-camera ownership

## Decision

The `0.0.172` hybrid is the end of the shared-position architecture. It proved
that neither more thresholds nor another native fallback can make the camera
stable while two independent systems shape the same endpoint.

The replacement gameplay camera will own, once per 20 Hz source tick:

1. focus, yaw, pitch and requested arm length;
2. static room collision;
3. dynamic-object collision;
4. immediate contraction and damped release;
5. the exact camera position, sector and render matrix.

Retail remains authoritative only while a scripted/cutscene camera is active.
Presentation interpolation reads two completed owned source states. It cannot
run collision, feed native history or write gameplay-camera state.

No level-resource edit is part of the first implementation. Runtime room data
already contains the static collision topology. A sidecar resource is justified
only if a later measured level contains missing or malformed room geometry.

## Verified retail contracts

The following addresses are RVAs in the shipped `Dungeon.dll`.

### Sector and portal graph

- `0x06130` resolves a point from a seed sector. It calls `0x46BD0` at most five
  times, following the most deeply crossed open portal.
- A sector is `0x3C` bytes. The world sector collection stores its count at
  `+0x00` and its contiguous sector base at `+0x04`.
- Sector solid/containment planes start at `sector+0x1C`, with `0x34` bytes per
  plane record. The point is at `+0x00`; the fixed-point inward normal is at
  `+0x0C`. `0x3D8A0` computes their signed distance.
- `sector+0x14` is the solid containment-plane count used by `0x3D8E0` during
  exact sector lookup. `sector+0x18` is the extended clip-plane count used by
  `0x4E5B0` to validate a portal crossing.
- `sector+0x20` is portal count and `sector+0x24` is the portal array. Portal
  stride is `0x44`.
- A portal plane point is at `portal+0x10` and its fixed-point inward normal is
  at `portal+0x1C`. `portal+0x04` holds flags. `portal+0x08` points to a link
  whose `+0x0C` member is the adjacent sector.
- `0x4E760` is a centre-line graph visibility traversal, not a sphere sweep.
  It calls `0x4E5B0`, which intersects the segment with portal planes and
  checks the crossing point against the sector clip planes.

These contracts are sufficient to snapshot the nearby convex sector graph and
sweep a finite camera sphere through it without asking the fixed-camera
resolver to choose a replacement position.

### Render publication

`0x3860` is not a safe camera-position setter. It is a broad cache transaction:

1. an optional owner callback at cache `+0x18`;
2. `0x3A980`, which updates node trigonometric/local transform state;
3. `0x3AC00`, which composes world matrices through the parent tree;
4. a 12-dword copy from `camera_node+0x9C` to global `0x1D4110`;
5. `0x38E80`, which recursively updates sector/resource/bounds state;
6. `0x3B0C0`, which recursively invokes node callbacks.

Replaying this entire function after the retail tick caused player jitter in
the rejected `0.0.142` experiment. The owned path must therefore publish only
the camera node's position/orientation, its resolved sector, the composed
camera matrices and the 12-dword global render matrix. It must not replay the
generic callback/resource recursion.

## Static collision model

Each room sector is treated as a convex volume with inward-facing planes. A
sphere centre is safe when its signed distance from every solid plane is at
least the camera radius.

For an arm sweep:

1. Find the earliest solid-plane contact and earliest crossed portal.
2. If a portal is first, validate that the full sphere fits at the crossing in
   both sectors, then continue the same sweep in the adjacent sector.
3. If the sphere does not fit, the portal boundary is the blocker; do not
   alternate sector ownership on later ticks.
4. A start overlap may exit along a direction whose signed distance increases.
   Motion deeper into the same plane is blocked immediately.
5. A position already accepted at exactly one-radius distance is clear on the
   next identical query. This is the required idempotence invariant.

The pure implementation is `src/camera_room_collision.h`; its deterministic
tests cover wall contraction, repeated boundary acceptance, portal traversal,
closed portals and initial-overlap escape. It has no game-memory dependency.

## Dynamic collision model

Drawable moving objects remain a separate channel because sector planes are
static. The existing render-mesh snapshot can provide their transformed
triangles, but the owned camera must use one finite sphere sweep and one result
ordering. It must not feed the result through the retail four-position camera
ring or accept a second post-native correction.

Static room planes win ties against dynamic triangles. Contraction is
immediate. Release is applied once to the final minimum safe arm, never once
per collision channel.

## Integration gates

The runtime hook is not enabled until all of these are true:

- a read-only sector snapshot validates counts, pointers, normals, neighbor
  links and reciprocal portals in live logs;
- pure room sweeps pass deterministic multi-sector and overlap tests;
- dynamic triangle sweeps return a single comparable arm fraction;
- the narrow camera publication transaction is byte-verified before and after
  a source tick and does not mutate the player node;
- mode transitions explicitly select either `OWNED_GAMEPLAY` or
  `NATIVE_SCRIPTED`; there is no mixed position owner.

The first in-game build of this architecture will therefore be diagnostic-only
for room snapshots. It will preserve `0.0.172` behaviour until the memory
contract is confirmed by one short user run.

## 0.0.173 runtime validation

The completed diagnostic run is
`<game-directory>\logs\deathtrap-native-20260802-083200-637-pid15520.log`.
It visited 64 distinct sectors and captured 456 plane records and 238 portal
records. There are no invalid collection/header/sector records, no invalid
neighbor pointers and no missing reciprocal portal links. All 64 captured
focus points are on the contained side of every solid plane; the smallest
measured signed distance is 51 units.

Plane normals are direction vectors but are not consistently unit Q14. Their
measured lengths range from 15537.2 to 18536.1. The runtime adapter therefore
normalizes each vector before applying the 96-unit camera radius. Using a
fixed `1/16384` scale here would distort the camera footprint by up to about
13 percent and recreate angle-dependent contacts.

Portal flags observed are `0x19`, `0x1B` and `0x31`. The first two have the
verified traversal bit `0x08`; the two `0x31` records do not and also carry
the room-trace skip bit `0x20`. The owned solver retains every portal plane:
open records transition to their neighbor, while closed records collide as a
solid face. A closed door cannot become a hole merely because its portal is
not traversable.

Version 0.0.174 builds and caches the complete normalized room graph, refreshes
live portal flags on each source tick and runs the pure sphere sweep in shadow
mode. It logs its endpoint, sector transitions, initial-overlap state and
blocker next to the unchanged native/hybrid endpoint. No shadow result is
published in this version.

## 0.0.174 shadow result and near-pivot response

The completed run is
`<game-directory>\logs\deathtrap-native-20260802-084232-977-pid1420.log`.
The graph built successfully with 746 sectors, 2519 normalized solid planes
and 2812 live portals. Across 620 sampled source ticks the owned and native
blocked states were `(owned,native) = (1,1): 306, (1,0): 279, (0,0): 32,
(0,1): 3`. The owned sweep therefore closes the verified same-sector hole in
the retail portal trace, which explains the hybrid camera's remaining wall
and ceiling penetration.

Direct radial contraction is not itself a complete camera response. The owned
safe radius reached zero in 19 samples and fell below 120 units in 46 samples.
Publishing those points would be collision-safe but visually unusable. This is
now treated as a shot-selection problem, not as permission to restore the
retail alternate-position ring or a retained world-space fallback point.

Version 0.0.175 adds a pure deterministic near-pivot planner. Every candidate
is rebuilt from the current focus, requested orbit and an angular offset, then
swept through the same complete graph. The score first maximizes usable arm
length up to the configured 650-unit minimum and then minimizes angular
deviation. A one-camera-radius hysteresis can retain the previous angular side
only while competing avoidance shots remain necessary; a useful direct shot
always becomes the recovery target. No camera endpoint or invisible orbit
centre is retained. The runtime remains shadow-only and logs the selected
offset and safe radius as `camera_owned_shot_shadow`.

## 0.0.175 shot-planner result

The completed run is
`<game-directory>\logs\deathtrap-native-20260802-092039-780-pid24148.log`.
The hybrid trace missed a room obstruction on 536 of 843 logged collision
samples. In the final stationary sequence it reported a nearly full 1400-unit
arm while the complete graph limited the direct ray to 32 units. The planner
found a portal-valid side shot of about 1400 units in the same samples. Across
the run it recovered at least 650 units in 110 samples where the direct room
sweep had already collapsed to zero. This validates both the retail
same-sector hole and the need for angular shot selection.

The run also exposed two planner limitations before publication. Candidate
yaw ended at plus or minus 90 degrees, which is insufficient when the focus is
inside the camera margin of two corner planes. Side hysteresis could also keep
an already collapsing candidate because its improvement deficit remained
inside the 96-unit switch margin.

Version 0.0.176 extends candidate yaw through plus/minus 120 and 150 degrees
to the 180-degree front shot, adds higher pitch alternatives and refuses to
retain a prior candidate below three camera radii when another shot improves
it. A deterministic test covers the two-plane corner where only the opposite
orbit direction escapes. It also simulates a bounded 30-degree-per-source-tick
angular response and re-sweeps every applied intermediate angle. The applied
offset, safe distance and collision state are logged but remain shadow-only.

## 0.0.176 angular-response result

The completed run is
`<game-directory>\logs\deathtrap-native-20260802-093002-294-pid18548.log`.
The expanded final-shot search no longer collapses: its minimum selected safe
distance is 414.9 units, with zero samples below the 96-unit camera radius or
the 288-unit three-radius near-pivot boundary. It retained a useful side on
218 samples and the hybrid still missed 232 owned room contacts.

Only the simulated bounded transition remained unsafe. Ten samples across six
transition events fell below 288 units; four fell below 96. Captured examples
include a plus-45-degree target whose plus-30-degree intermediate position has
only 21 units, and a required side change from minus 90 to plus 90 whose
shortest intermediate path remains around 180--190 units. These are genuine
disconnected safe-shot regions, so more temporal smoothing cannot make the
intermediate positions valid.

Version 0.0.177 adds a deterministic fail-closed safety cut. A bounded angular
step below three camera radii is rejected when the already swept target shot
is at least three radii away; shadow state moves directly to that verified
target and records `cut=1`. Future publication must pair this flag with a
one-source-transition presentation cut so interpolation never draws the chord
through the obstruction. The final target and cut remain diagnostic-only.

## 0.0.177 safety-cut result and dynamic channel

The completed run is
`<game-directory>\logs\deathtrap-native-20260802-093622-642-pid13088.log`.
All static-room gates pass. Across 495 shot samples, seven unsafe angular
transitions are cut. Selected safe distance never falls below 487.6 units and
the actually applied shadow distance never falls below 447.2 units. There are
zero applied samples below 288, 120 or the 96-unit camera radius. All 54
samples whose direct ray is zero retain at least 650 units. The hybrid misses
321 room obstructions in the same run. Static room ownership, full-orbit shot
selection and transition policy are therefore frozen; do not resume hybrid
threshold tuning.

Version 0.0.178 adds the independent dynamic-object evidence channel. It
sweeps one 96-unit sphere from the current focus to the final applied static
endpoint against stable, drawable render triangles. Thin clutter still uses
the tested two-intrinsic-axis size qualification, while closed-door and block
dimensions remain blocking. Initial overlap uses the triangle-direction test:
moving away or tangent is ignored, moving deeper is a zero-distance contact.
The result is radial only with an 8-unit contact backoff. It never calls the
old expanded-OBB pushout, never chooses a supporting face and never retains a
world-space fallback point. The shadow evidence channel is
`camera_owned_scene_shadow`.

## 0.0.178 dynamic-sweep result and combined score

The completed run is
`<game-directory>\logs\deathtrap-native-20260802-094318-936-pid16480.log`.
The clean channel records 205 triangle contacts across resources 11432,
12613, 12616, 12708, 13676 and 13679. It reports 18 direction-qualified
initial overlaps and no non-radial result. Three thinner resources (11467,
12610 and 12709) fail the existing two-axis mass test. Stable contacts have
zero bounds motion; resource 11432 also reaches 20 units of observed motion.

Applying dynamic collision only after static shot selection is insufficient:
158/205 contacts contract below 288 units and 84 below the 96-unit camera
radius. This is expected when the room-optimal ray points through a block; it
does not invalidate the triangle sweep and must not revive OBB pushout.

Version 0.0.179 evaluates room and scene distance as one score for each angular
candidate. A direct shot outside the configured 650-unit useful boundary exits
early. Otherwise alternatives are evaluated in increasing angular cost until
a full-quality combined shot is found; only if none exists is the complete set
scanned for maximum distance. Candidate-index hysteresis and the three-radius
safety-cut rule operate on this combined distance. Intermediate applied angles
are re-swept against both channels. The visible hybrid camera is unchanged.
