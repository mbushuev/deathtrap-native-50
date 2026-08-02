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
