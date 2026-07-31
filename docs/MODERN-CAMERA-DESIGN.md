# Modern third-person camera design

## Objective

Add a player-controlled third-person orbit camera to the supported Windows
build of Deathtrap Dungeon without changing simulation timing, collision,
animation, the native 50 Hz renderer or frontend menus.

This is not a fixed chase camera. The right stick and mouse own an independent
camera pivot that can orbit around the player. The left stick is interpreted in
camera space, and the character turns toward the requested movement direction.

The stable `v0.0.48-stable` build is the immutable baseline. Camera development
lives on the `modern-third-person-camera` branch and remains opt-in until every
acceptance test in this document passes.

## Reference model

The rig combines the common parts of current third-person camera systems:

- an independent yaw/pitch pivot around the followed character;
- a shoulder/hand target above the character root;
- a spring arm from that target to the desired camera position;
- a volume sweep rather than a single center ray for camera collision;
- immediate obstruction response and a slower, damped return to full distance;
- input acceleration/deceleration, circular deadzone and an adjustable response
  curve;
- optional delayed recentering rather than continually fighting player input.

This follows the official Unreal Spring Arm, Unity Cinemachine Third Person
Follow/Orbital Follow and Godot SpringArm3D models:

- <https://dev.epicgames.com/documentation/en-us/unreal-engine/using-spring-arm-components>
- <https://docs.unity.cn/Packages/com.unity.cinemachine@3.1/manual/ThirdPersonCameras.html>
- <https://docs.unity.cn/Packages/com.unity.cinemachine@3.0/manual/CinemachineOrbitalFollow.html>
- <https://docs.godotengine.org/en/stable/tutorials/3d/spring_arm.html>

## Runtime state machine

The camera layer must use explicit state priority. It must never infer control
from a single frame or overwrite a retail camera in a non-gameplay state.

1. `Native`: startup, videos, loading, menus, pause, death and scripted camera.
2. `Selector`: radial inventory owns the right stick; camera input is frozen.
3. `FirstPerson`: the retail first-person camera owns both look axes.
4. `ModernThirdPerson`: independent orbit and camera-relative movement.
5. `BlendToNative` / `BlendFromNative`: short, bounded transitions between
   compatible camera transforms.

Modern control may start only after several consecutive real gameplay ticks
with a stable live player and camera owner. Any loss of those preconditions
returns immediately to the native camera. Synthetic 50 Hz render passes never
consume input or advance camera state; they only interpolate two accepted real
camera endpoints.

## Orbit rig

The rig owns persistent `yaw`, `pitch` and `distance` values.

1. Build a pivot at the player position plus a configurable vertical offset.
2. Rotate a local shoulder offset around world up by `yaw`.
3. Rotate the spring arm vertically around the shoulder by `pitch`.
4. Place the camera at the collision-limited end of the arm.
5. Aim at a hand/look target above the player root, not at the feet.
6. Preserve world up and eliminate roll.

Initial tuning targets, all configurable in `deathtrap_native.ini`:

| Parameter | Initial value |
| --- | ---: |
| Pivot height | derived from the live player bounds |
| Shoulder side offset | 0.20 player heights |
| Desired distance | 2.25 player heights |
| Pitch range | -35 to +55 degrees |
| Gamepad yaw speed | 210 degrees/second |
| Gamepad pitch speed | 145 degrees/second |
| Right-stick circular deadzone | 14 percent |
| Right-stick response exponent | 1.35 |
| Recenter wait | 1.0 second after the last manual look input |
| Recenter time | 0.55 second |

Mouse input is raw relative delta with independent X/Y sensitivity and no
temporal smoothing. Gamepad input is integrated using real elapsed time so its
angular velocity does not change between original simulation ticks and native
50 Hz presentation. A circular deadzone and normalized post-deadzone magnitude
are required, following Microsoft's XInput guidance:

<https://learn.microsoft.com/en-us/windows/win32/xinput/getting-started-with-xinput>

The response curve and acceleration/deceleration remain independent settings,
matching Cinemachine's input-axis controller and Steam Input's camera mode:

- <https://docs.unity.cn/Packages/com.unity.cinemachine@3.1/manual/CinemachineInputAxisController.html>
- <https://partner.steamgames.com/doc/features/steam_controller/input_source_modes>

## Recentering

The camera never recenters while the player is actively moving the right stick
or mouse. After the configured idle delay, optional soft recentering rotates
only yaw toward the character's actual movement/facing direction. Pitch remains
where the player placed it.

Recenter is suspended while stationary, blocked against geometry, selecting an
item, attacking with a committed animation, or transitioning camera state.
Runtime `0.0.61` deliberately exposes one gameplay camera. R3 and SELECT no
longer enter independent retail or head-camera state machines; the right stick
and physical mouse always address the same persistent third-person rig.

## Camera-relative movement

The left stick must not be converted to the old tank-turn buttons.

1. Flatten camera forward and right vectors onto the gameplay ground plane.
2. Combine them with the normalized left-stick vector.
3. Preserve the retail walk/run magnitude and thresholds.
4. Turn the character toward the desired ground-plane vector through the
   verified player-controller heading path.
5. Feed forward magnitude through the native movement/collision solver.

The camera layer must never write player position, bypass collision, or repeat
the simulation. Existing stable wall-contact and animation behavior therefore
remains authoritative. Until the heading and movement entry points are proven,
camera-relative movement stays disabled rather than being approximated with
synthetic keyboard diagonals.

## Obstruction handling

A center ray is insufficient: it lets near-plane corners enter walls. The
preferred solution is a sphere sweep, or a small near-plane-shaped sweep, from
the pivot to the desired camera position. The player collider is excluded.

- Pull the camera inward immediately when geometry blocks the arm.
- Keep a small contact margin so the near plane is not placed exactly on the
  wall.
- Return to full distance more slowly with exponential, frame-rate-independent
  damping.
- Do not smooth through an obstruction.
- If the available distance becomes extremely small, reduce the shoulder
  offset before moving the look target away from the player.

The implementation should reuse a verified retail world-query routine if its
contract can be recovered. Otherwise a read-only query over the same collision
geometry is required. Pixel/depth heuristics are not acceptable.

## Integration with the native 50 Hz renderer

The modern camera endpoint is calculated once on each real simulation tick,
after the retail camera/controller state is valid and before the camera cache
is finalized. The existing renderer then interpolates the accepted camera
matrix at its synthetic phases.

Smoothing uses time constants (`1 - exp(-dt / tau)`), not a fixed per-frame
lerp. This prevents the camera response from changing with presentation rate.
Manual look rotation uses minimal damping to avoid input lag; follow-position
and obstruction release may use separate damping constants.

The camera-owner callback and the true mode-3 desired-position entry have now
been verified. Orbit input is inserted before the retail collision/cache path;
the final camera transform remains engine-owned.

## Implementation phases

### Phase A: camera-owner instrumentation

- Log the live camera owner, its callback RVA and bounded field deltas only on
  real gameplay ticks.
- Correlate those deltas with controlled horizontal turn, vertical first-person
  look, room transition and scripted camera samples.
- Identify the engine's pre-cache camera position/orientation and its collision
  query, if present.

### Phase B: orbit-only prototype (`0.0.53`)

- Add opt-in yaw/pitch orbit without changing player movement.
- Preserve menus, selector, first-person and scripted cameras.
- Interpolate verified camera endpoints through the existing 50 Hz renderer.

The first runtime attempt hooked `Dungeon.dll+0x2F380`, but one active mode-3
branch bypasses it. The second attempt hooked the shared `0x2DF60` resolver,
but its local vector proved to be sector-lookup input rather than the
authoritative camera endpoint. Version `0.0.53` hooks the `0x2F310` mode-3
dispatcher before its rail/fixed-camera pre-check, then submits the orbit
target through the original `0x2F380` configure/collision pipeline exactly
once. It initializes yaw, pitch and radius from the live retail camera only
after the player moves the right stick. Input is consumed once per real source
tick. This phase intentionally keeps tank movement and uses the retail camera
distance as its initial spring-arm length; camera-relative movement and custom
obstruction release remain later phases.

Version `0.0.54` keeps that verified native insertion point and refines the
input rig. Right-stick samples now pass through a time-based exponential
response filter before yaw and pitch integration, the vertical range extends
below the initial retail elevation, and temporary ownership by the selector,
menus or first-person view suspends rather than destroys the orbit state.
Returning from first person therefore resumes the same yaw, pitch and radius
instead of snapping back to the room camera.

Version `0.0.55` corrects the default XInput axis directions, reduces the
per-source-tick angular step and synchronizes the separately published camera
matrix with every synthetic 50 Hz phase. SELECT now toggles an overlay-owned
head view. Its initial prototype borrowed the entire retail mode-4 callback
while leaving the controller field in mode 3. Runtime testing proved that this
callback is not a pose-only solver: it also leaks persistent player-visibility
state, so the model can remain hidden after SELECT returns to third person.

Version `0.0.56` removes the mode-4 callback completely. SELECT remains in the
verified mode-3 orbit pipeline, preserves its complete native rotation and
moves only the captured render-camera origin to a configurable player-relative
eye point. Player visibility, hands, weapon, shadow, controller mode and game
state are never modified. The head view has its own `-75..75` degree default
pitch range, and returning uses the existing phase-synchronized rigid transform
blend back to the preserved orbit. This also prevents head view from poisoning
the subsequent third-person distance or room-camera state.

Version `0.0.57` replaces the unsafe through-body transition with a safe camera
policy cut and makes SELECT cycle modern third-person, custom head and retail
camera modes. The verified `controller+0x1B8` owner flag at `owner+0x8 & 0x80`
temporarily returns mode 3 to the original dispatcher for lever and reveal
shots. The orbit resumes only after that owner releases control. The existing
native resolver's output at `controller+0x1DC..+0x1E4` is also fed back as the
collision-limited arm length: pull-in is immediate, while release is damped.
This is the first Phase C step and retains native room/world collision as the
sole authority.

Version `0.0.59` corrects the integration layer after runtime logs proved that
the mode-3 dispatcher is called repeatedly by camera-cache refreshes and by
synthetic presentation phases. Input integration, cinematic arbitration,
spring-arm release and collision feedback now advance only on the first call
for a unique `Dungeon.dll+0x1D24DC` engine-frame stamp. Later calls are
read-only and retain the already accepted endpoint. This removes the idle
radius oscillation that previously appeared as camera shake.

The same version always samples the untouched native mode-3 candidate once per
source tick before applying the custom endpoint. A recent interaction plus
native-camera motion while the player is stationary gives the retail reveal
shot priority even when the ambiguous owner bit is absent; ordinary fixed
camera zones remain under the modern rig. Physical DirectInput mouse deltas are
intercepted by the existing proxy only during modern gameplay, accumulated
without temporal filtering, and consumed once by this source-tick camera
state. Menus, selectors, retail first person and retail camera mode receive the
original mouse stream unchanged. The accepted source endpoints continue to be
rigidly interpolated by the native 50 Hz presentation layer.

Version `0.0.60` removes the false post-callback camera-mode gate. Runtime
`0.0.59` showed that the fixed/rail branches reached the hooked mode-3
dispatcher and then changed the visible mode byte to `0` or `1`; treating that
byte as a second ownership check was why orbit stopped in most locations.
Room/fixed-camera ownership can no longer switch the modern rig off by itself.
Only an explicitly armed interaction-driven reveal may temporarily yield to
the retail camera. The DirectInput proxy also filters physical X/Y from the
buffered `GetDeviceData` path, completing the separation between mouse camera
look and character movement.

Version `0.0.61` removes runtime view cycling and scripted-camera arbitration
from gameplay. Camera ownership no longer depends on receiving XInput state,
so a physical mouse activates orbit correctly on a mouse-only system and is
released again when the native gameplay context disappears. Collision recovery
is motion-gated: obstruction contracts the spring arm through the native
resolver, while outward probing occurs only after clean samples and real
player/orbit motion. A stationary camera therefore cannot enter the former
extend/contract sawtooth.

Version `0.0.62` keeps the single modern gameplay camera but restores a
strictly interaction-gated retail reveal path. Only a recent controller or
physical-keyboard operate command followed by independently travelling native
camera output may suspend the orbit, so ordinary fixed-camera rooms cannot
steal ownership. The default mouse and stick axes use conventional directions,
and the unobstructed spring arm starts at configurable `PreferredRadius`
instead of inheriting the retail camera's very short distance. A recent mode-3
callback watchdog releases physical mouse motion to pause/front-end screens
even when their stale player pointers still look like gameplay.

Version `0.0.63` separates resolver smoothing from obstruction feedback. A
resolved point observed while the player is actively rotating the orbit is no
longer allowed to collapse the persistent spring arm; confirmed stationary-ray
contacts retain a player-side surface margin. Lever reveals are armed by an
explicit interaction and remain native through the delayed scripted-owner
phase instead of resuming the modern orbit during the pre-shot pause. Physical
mouse and right-stick menu cursor input use last-active-device arbitration,
while the mode-3 callback watchdog automatically transfers controller
ownership between gameplay and frontend screens.

Version `0.0.64` makes spring-arm feedback stateful rather than treating every
shortened native camera point as an obstruction. A collision candidate must
remain at a stable distance for three source ticks before it may contract the
persistent arm; the changing distances produced by native angular damping are
discarded. Mouse samples are protected by a short asynchronous settle window,
confirmed contacts retain a larger player-side margin, and a contracted arm
now restores its preferred radius after the last confirmed contact even when
the player is stationary. This prevents both false penetration feedback while
rotating and the permanent minimum-radius state that pinned the camera to the
character's back.

Version `0.0.65` replaces timer-driven spring-arm release with a contact
manifold tied to the player position and orbit ray. A confirmed prop, stair or
wall hit pulls the arm directly to its safe radius and remains pinned while
that manifold is unchanged; rotating or moving away releases it in bounded
steps and allows a new surface to take ownership. This removes the repeated
contract/release cycle inside non-wall geometry. Collision candidates remain
active during orbit input so thin objects can pull in the camera instead of
being ignored until the mouse stops. Scripted-camera arbitration also assigns
each operate press a sequence: owner-less reveals require a quiet pre-event
retail baseline and sustained post-event travel, while a script owner must
actually transition after that press. Merely pressing E can no longer hand
the camera to an unrelated room camera, and one interaction cannot retrigger
after its reveal completes.

Version `0.0.66` initially treated `Dungeon.dll+0x2E800` as a pre-damping
collision endpoint. Subsequent disassembly disproved that interpretation:
`0x2E800` shapes angular/vector offsets, while the authoritative retail camera
visibility predicate is the `0x2F6D0` call to the seven-trace volume test at
`0x30910`. The obsolete return hook is removed in `0.0.75`; non-camera calls
and all retail shaping remain untouched.

Version `0.0.67` closes a separate hole in the game's data: some visible
switch housings, stairs and props are render objects but are absent from the
room collision BSP queried by the retail camera. The scene-cache pass at
`0x3AC00` publishes an own-object world bounding sphere at node offsets
`0x80..0x8C`. The custom spring arm performs a read-only segment sweep through
stable drawable spheres before submitting its target to the native resolver.
Player and room ancestors, animated bounds, tiny effects and oversized room
bounds are excluded. A hit is submitted as the native target while the full
orbit point remains the requested endpoint, so the verified v0.0.66
pre-damping path still owns immediate pull-in and the existing contact
manifold still owns release.

Version `0.0.68` closes two failure modes that cannot be solved by enlarging
those spheres. First, negative orbit pitch could place the requested camera
below the player's ground reference; the room portal query could then miss the
floor crossing and let the camera remain under the level. The desired endpoint
now has a hard lower bound derived from both the live camera-controller pivot
and the captured player root. Second, a coarse prop sphere may contain both the
player and the requested camera, so there is no valid entry point to shorten
on the current ray. Such an overlap now retains the previous native-resolved,
object-validated camera endpoint translated with the player instead of
entering the prop and attempting a delayed push-out. The fallback is updated
only by endpoints that pass the native room resolver and the supplementary
object test.

Version `0.0.69` removes the sphere itself from the final collision decision.
The resource handle at `node+0x3C` is resolved through the game's renderer
registry, and its original convex surface records are cached as local-space
triangles. Stable scene nodes first pass their cheap `node+0x80` sphere broad
phase; the spring arm is shortened only when its centre ray intersects a real
world-transformed render triangle. A fixed surface clearance keeps the camera
near plane outside the mesh. Consequently, a lever housing, stair block or
other visible prop no longer needs a gameplay/BSP collision flag to block the
camera, while the empty space inside a coarse sphere remains traversable.

Version `0.0.70` gives the camera itself a physical volume. The `0.0.69`
centre ray could change the selected face abruptly or pass exactly beside a
triangle edge, producing both running jitter and occasional penetration of a
visible prop. The new narrow phase sweeps a 96-world-unit sphere across every
candidate triangle's face, edges and vertices and performs a closest-point
overlap test at the start of the usable arm. Because the radius already
contains the required surface clearance, only a small numerical backoff is
applied to the accepted centre distance. The original native BSP resolver,
immediate inward contraction and damped outward recovery remain unchanged.

Version `0.0.71` separates exact object contact from ambiguous native follow
motion. A mesh contact owns the spring arm while the same surface remains
within a 24-unit hysteresis band, even as the player moves, eliminating the
extend/re-contract cycle seen during running. Its already validated swept-
sphere radius is applied directly rather than subtracting the native
96-unit margin a second time. Native BSP results still use the captured
pre-damping endpoint, but require temporal stability before changing the
persistent arm; ordinary camera-follow lag can no longer trigger immediate
pull-in.

Version `0.0.72` removes the remaining ownership conflict exposed by the
`0.0.71` runtime log. The old resolver produced 63 persistent arm contractions
during ordinary running, walking the radius through unrelated follow-camera
distances even when orbit input was idle. It remains active as a same-frame
room/BSP safety stage, but only exact swept-sphere contacts against render
meshes may now change the modern spring-arm length. The former 180-unit hard
minimum is also reduced to the swept sphere's 96-unit radius: repeated
`contact=180` events proved that the old clamp could leave the camera volume
partly inside a tight corner with no legal way to retreat further inward.

Version `0.0.73` separates the 96-unit camera collision volume from the
minimum permitted distance between the camera centre and the player pivot.
When a swept sphere already overlaps a render triangle at the start of its
usable arm, the condition is now reported explicitly and the endpoint
collapses to a 16-unit near-pivot position on the player's side of the
surface. A translated historical endpoint is deliberately not reused because
it may already lie behind a one-sided prop after a room transition. Such an
overlap endpoint is also excluded from the last-safe cache. In addition, an
exact render-mesh contact survives up to two missing adjacent snapshots before
outward recovery begins. This removes the measured `96 -> 144 -> 96` release
cycle without delaying a real release after the spring-arm direction changes.

Version `0.0.75` re-bases collision on the verified engine focus at
`controller+0x264` and tests the complete desired arm on every unique source
tick. The native seven-trace room/portal predicate supplies world clipping;
the render-mesh sweep supplies only missing static prop geometry. The arm
contracts immediately to the nearest hard result and returns by a bounded
step after two clear samples. It never pre-extends before a query, never probes
beyond the desired endpoint, and never reuses a historical world-space camera
point. Initial render-mesh overlap is direction-aware: inward motion blocks,
while outward or tangential motion is allowed to depenetrate instead of being
trapped by the capsule exit. These state transitions are covered by the
standalone `camera_spring_arm_test` target.

Version `0.0.79` restores the 96-unit swept camera volume after exact runtime
telemetry separated centre safety from near-plane safety. With the temporary
64-unit volume, the final contact against lever resource `12708` left the
camera centre roughly 66 units from a triangle edge, then declared the desired
arm clear while the visible near-plane corner was still inside the prop. The
larger volume is not a release-time heuristic: it keeps the geometric query
blocked for precisely the interval in which any part of the protected camera
volume still intersects the mesh. Same-tick exact endpoint publication from
`0.0.78` remains authoritative.

Version `0.0.80` fixes a different, presentation-only path that the source-tick
camera telemetry could not observe. Both collision-resolved camera endpoints
may be valid while the straight Cartesian interpolation chord between them
crosses the corner of a prop. This is especially visible with two synthetic
50 Hz samples between the 16.7 Hz source endpoints. Every synthetic camera
chord is now swept as the same 96-unit volume against stable render meshes. A
clear chord keeps normal translation and rotation interpolation. A blocked
chord keeps the 1/3 sample at the previous safe endpoint and the 2/3 sample at
the current safe endpoint while preserving interpolated orientation. Thus only
geometrically unsafe camera translation samples lose smoothing; actors and all
unobstructed camera motion retain the existing 50 Hz presentation path. The
diagnostic log records `camera_temporal_chord_guard` with the exact resource,
triangle, phase and selected endpoint.

Version `0.0.81` closes the corresponding source-tick publication gap during
spring-arm recovery. `0x2F380` owns a retail position-history resolver and may
move a submitted short-arm endpoint laterally or vertically after the render-
mesh sweep has accepted it. The trace captured this as a validated
`-11015/-1376/16390` submission becoming `-11160/-1361/16235` in the live and
published camera. While the modern spring arm is shorter than its requested
radius, the exact submitted point is now committed to the controller history,
camera node and published matrix after `0x2F380`. At full radius, the retail
path remains untouched.

Version `0.0.82` replaces that publication model with native ownership. The
orbit layer supplies only yaw, pitch and a desired endpoint. The original
mode-3 dispatcher owns visibility, safe fallback placement, history and final
publication. This removes the competing mesh/sphere collision model from the
active camera path and preserves the safety gap already demonstrated by the
stock camera around the lever block. Authored interaction reveals retain the
existing arbitration path.

Version `0.0.83` adds hysteresis above that native authority. A blocked modern
ray is still resolved first by retail mode 3, but its first native-safe
alternate offset is retained instead of asking the randomized `0x2F750`
search to choose a new side every source tick. The offset translates with the
focus, advances toward the requested orbit only through clear `0x30910`
queries, and retracts through the same query when a protrusion blocks the next
step. This is a safe-path glide rather than post-render smoothing: no endpoint
is submitted unless the retail visibility volume accepts it.

The diagnostic retail sample used for authored-shot arbitration is now
transactional. If it does not take camera ownership, controller resolver and
position-history state are restored before the orbit pass. This prevents two
history integrations per source tick and removes periodic running jolts without
weakening interaction-camera detection.

Runtime `0.0.83` disproved retained alternate placement as a suitable
player-controlled orbit model. The cached offset could remain active for more
than one hundred source ticks, and the complete mode-3 dispatcher still
re-shaped each independently validated submission. An unchanged focus and
orbit therefore produced changing desired, resolved and published positions.

Version `0.0.84` removes alternate-camera retention and stops re-entering the
complete mode-3 dispatcher for the ordinary modern view. The retail `0x30910`
centre-plus-six-offset volume query remains the collision authority. If the
requested orbit is blocked, a bounded binary search finds the farthest clear
point on the exact focus-to-orbit ray and applies a small native-space backoff.
The spring arm contracts immediately and extends by a bounded amount only
after consecutive clear samples. Every rounded recovery endpoint is queried
again before submission.

The resulting clear endpoint is passed once to `0x2F380`, the verified retail
configure/history/publication function. This preserves native sector
bookkeeping and camera orientation but bypasses `0x2F750`, whose randomized
lateral fallback is appropriate for fixed cameras and unstable for a
continuously controlled orbit. The diagnostic retail call remains
transactional and is retained only for authored interaction-shot arbitration.

The failed `0.0.85` build applied an exact post-resolver camera translation.
That protected the known prop but bypassed native wall/floor placement and
separated position from the orientation calculated by the retail controller.
It is reverted and must not be reused.

Version `0.0.86` treats supplemental prop geometry as a veto rather than a
replacement camera solver. The ordinary native result remains untouched unless
its published camera volume intersects a stable render mesh large enough to
span the camera diameter on two intrinsic axes. A rejected native probe has
its resolver/history state restored before a shorter target is resubmitted
through the same `0x2F380` path. Bounded failure falls back to the unmodified
native result. This retains native walls, floors, orientation, authored shots
and history while giving large BSP-absent props a constrained retry.

Runtime `0.0.86` showed why restoring before every constrained retry cannot
work: all three passes emitted the same stale four-sample history average.
Version `0.0.87` instead treats the whole bounded correction as one
transaction. It retains a single rollback snapshot, then lets up to four
prop-safe `0x2F380` submissions advance the native ring and evict its
pre-contact samples. The complete native resolver remains the only writer of
walls, floors, sectors, orientation, history and publication. Failure rolls
the entire transaction back to the ordinary result.

Runtime `0.0.87` showed that removing the intermediate restores was still not
enough: one ordinary plus four constrained calls in the same source tick
published an identical camera point in all 714 exhausted contact sequences.
The remaining user-reported obstacles were then identified as trap blocks that
translate or extend over time, not the stable static blocks already handled
reasonably. Those nodes had been intentionally removed by the old animated-
bounds filter before their triangles reached the camera sweep.

Version `0.0.88` adds a separate kinematic-block class. A large render mesh is
latched only after an adjacent snapshot pair proves rigid translation: stable
resource and radius, unchanged 3x3 basis, and matching world/bounds movement.
The two-axis camera-diameter test still excludes thin levers. The latch
survives after the block stops, until the scene resets.

The ordinary path again calls `0x2F380` once and leaves its complete result
untouched. Only an actual swept-sphere hit on a latched kinematic block may
commit a shorter point on the already native-safe radial segment in the same
source tick. Thus a moving block can push the camera out, while static walls,
floors, unobstructed tracking, spring-arm recovery and authored reveals never
use the exact-write exception.

The immediate `0.0.88` runtime proved that movement history was the wrong
authorization signal. It latched 18 unrelated moving nodes, performed zero
commits, yet the post-native sweep independently found 769 real mesh
intersections. Resources 13676 and 13678 alone accounted for 378 of them; both
reported zero snapshot motion at contact. The user saw no visible change.

Version `0.0.89` therefore treats object collision as a phase, not an object
label. The normal engine call first resolves rooms, walls, floors, sectors,
orientation and camera history. The actual published camera sphere is then
tested against large current scene meshes, including translating ones. Only a
positive intersection permits an exact shorter endpoint on the same already
native-safe radial segment. This matches the useful room/LOS-then-item-push
separation in the open TombEngine implementation.

The rejected 0.0.85 global overwrite is not restored: clear ticks, authored
shots, native walls/floors and mesh-safe recovery results are never written
directly. Thin levers still fail the two-axis camera-diameter rule. Because the
test happens after native publication on every source tick, any recovery shift
back into a block is immediately constrained again.

The user reported that `0.0.89` finally handles the blocks but continuously
pushes and jitters against them. Runtime values show the spring releasing by
64 units per tick after each successful post-native correction, then entering
the same mesh and being shortened again. Object recognition and publication
are correct; obstruction ownership is missing.

Version `0.0.90` feeds the complete desired-arm mesh sweep into the same
spring state as native wall collision. Native and mesh endpoints are combined
by nearest safe radius, and either obstruction prevents clear-tick
accumulation. A sustained block therefore holds one contracted radius instead
of alternating release and push-out. The post-native phase remains only as a
safety constraint around `0x2F380` output, preserving the two-stage
room-then-item architecture without allowing the native history to move the
camera back through the block.

The next `0.0.90` capture distinguishes two remaining problems. Temporary
`pivot_not_clear` failures must not hand one frame back to the retail
fixed-camera result, and a one-frame outward change in a still-blocked native
boundary must not move the spring. Version `0.0.91` therefore adds two
state-level rules:

1. Recover a proven-clear lower endpoint by sampling the same complete ray
   when the focus footprint is blocked. If the query remains unavailable,
   translate the last modern-camera result with the moving focus and run that
   hold through the normal native and mesh phases.
2. Require three consecutive outward boundary samples while
   `obstruction_present` remains true before releasing by the usual bounded
   64-unit step. Any inward boundary is still authoritative immediately and
   resets confirmation.

This is temporal hysteresis on the native obstruction boundary, not a lever
resource blacklist. It suppresses the observed 182/311 alternating sample
while retaining collision for large blocks and ordinary room geometry.

The `0.0.91` log shows that source collision and presentation collision must
also share ownership. At the final lever housing, the spring radius is stable
but camera snapshots alternate every real tick between the same 65-unit safe
point and a 211-unit native-history point. Repeated post-native writes cannot
solve this because the 50 Hz renderer has already retained both endpoints.

Version `0.0.92` adds the missing presentation contract:

1. Compact props whose bounding-sphere radius is smaller than the full
   192-unit camera diameter do not own the arm. This is a footprint-derived
   small-object rule, not a resource ID exception.
2. A positive contact with any remaining large mesh latches its final
   post-native safe endpoint.
3. On latch acquisition, both historical camera snapshots are cut to the
   verified safe endpoint. Later safe endpoints interpolate normally as the
   player, block or orbit moves.
4. Every new snapshot is constrained to the active target before synthetic
   and exact rendering. One missing source sample is tolerated; two complete
   clear rays release the latch.
5. Authored cameras and scene-history resets clear the latch immediately.

This prevents a native/mesh A-B-A presentation loop without restoring the
rejected global exact-camera ownership of `0.0.85`.

The `0.0.92` run demonstrates why object size cannot replace collision
resolution. The one-diameter bounds-radius test removed 107 resources from
camera collision and reintroduced visible static-block penetration. At the
same time, a qualified resource 9997 produced `contact=0`; contracting a
radial spring to that contact placed the camera exactly at the player-side
pivot and generated black near-plane frames.

Version `0.0.93` adopts a separate initial-overlap operation, modelled on the
maintained TombEngine camera's room-then-object sequence. Normal contacts are
unchanged and remain radial. If a qualified object's expanded oriented bounds
already contain the pivot while the requested ray moves deeper into it, the
camera volume is translated to the nearest horizontal face instead of being
collapsed to zero. The previous camera chooses the side only when the pivot is
centred on an axis; it does not supply a stale endpoint. The candidate is then
checked by Deathtrap's native room-volume resolver, so object depenetration
cannot authorize wall or floor penetration. Thin geometry is still rejected
only by the intrinsic two-axis extent test.

The first `0.0.93` run exposed a target-ownership error in the presentation
latch rather than another collision-classification failure. During a
pre-configure hit on moving-block resource 13676, the validated submitted
endpoint changed every source tick, but the latch repeatedly retained the
unchanged older point emitted by the native history ring. Because that target
was an absolute world-space coordinate, the rendered camera remained there
while the player moved and eventually intersected internal room geometry.

Version `0.0.94` distinguishes the two collision phases explicitly. A
pre-configure arm hit presents the submitted endpoint already validated by the
native room-volume query. Only a positive post-native mesh correction presents
the final published/committed endpoint. The one-sample contact grace period
translates its target with the current camera focus, preserving the relative
safe pose instead of freezing a world point. This changes neither the
room/wall/floor resolver nor the oriented overlap-pushout operation.

The `0.0.94` test leaves one geometric degeneracy. When the player-side pivot
grazes the expanded face of moving block 13676, the current orbit ray may have
only 1--42 units of usable radial length. Exact contraction is collision-safe
for the protected sphere but is not a usable third-person pose; the screenshot
shows the resulting partial-black view. The latch itself remains current and
releases correctly.

Version `0.0.95` does not raise the collision radius or force a minimum point
through the block. Below the existing 120-unit minimum usable camera distance,
it selects the requested-side horizontal face of the same expanded OBB. If the
pivot is already outside another horizontal face, that coordinate is preserved
so the complete move is a slide around the object rather than a chord through
it. The candidate remains bounded by the desired arm and is revalidated by the
native room-volume query. Ordinary radial contacts at 120 units or more,
walls/floors, presentation ownership and authored shots are unchanged.

The complete `0.0.95` trace leaves one locomotion-specific oscillation. During
the user's final straight run, mouse and stick orbit input are zero and
yaw/pitch are constant. Native room clipping nevertheless alternates between
large inward contacts and clear/outward samples as the same diagonal arm
crosses adjacent BSP or portal faces. Immediate pull-in is correct, but the
64-unit-per-source-tick return reaches full radius quickly enough to collide
with each following face as a separate visible jolt.

Version `0.0.96` makes recovery policy depend on collision ownership and
locomotion state:

1. Every newly nearer native or mesh endpoint still contracts immediately.
2. Exact render-mesh contacts keep the normal responsive recovery.
3. Manual camera input also keeps normal recovery, so rotating away never
   feels stuck.
4. Only a native-owned contraction while the player moves with idle orbit
   input uses an eight-tick confirmation and a 12-unit outward step.
5. Reaching the complete clear orbit releases native ownership.

This is conservative hysteresis rather than another collision threshold. It
never accepts an endpoint rejected by the native predicate and never changes
the dynamic-block OBB escape or scripted-camera arbitration.

The runtime result disproves this design. A single-tick movement predicate is
not stable enough to select spring timing, and reducing the outward step
prolongs bad low-radius native publications. The actual locomotion hitch is
one layer later: the retail configure routine smooths absolute camera
positions, so its four-sample history can remain anchored to the room while the
modern orbit focus moves.

Version `0.0.97` moves stabilization to that ownership boundary. With no orbit
input, the cached average and all four position-history samples are translated
by the same bounded focus delta before the sole retail configure call. This
preserves their relative shape and lets the native routine continue resolving
walls, floors, sectors and orientation. Manual orbit bypasses rebasing, the
normal 0.0.95 spring timing is restored, and no clear-space or native-wall
frame is directly published by the patch.

The remaining `0.0.97` jumps require the standard modern-camera separation
between collision target and presentation pose. The collision target may
change immediately whenever the legacy room solver chooses another valid
portal placement; the visible camera should follow that target with damping
unless its existing pose has become unsafe.

Version `0.0.98` implements that separation only in the render snapshot. An
idle-input camera carries the previous presentation pose with the focus,
applies a proportional response capped separately in horizontal and vertical
axes, and blends rotation by the same progress. Both endpoint rays and the
temporal movement chord must remain clear in native room geometry and
qualified object meshes. A newly invalid old pose takes the exact native
collision result immediately; a blocked transition chord holds the old safe
pose. This is asymmetric camera damping: safety contracts instantly, while
valid lateral/outward changes converge smoothly.

The first runtime test exposed an input-ownership error rather than another
collision threshold. Physical mouse deltas arrive as discrete DirectInput
packets. `0.0.98` disabled follow only on the source tick that consumed a
non-zero packet, so an empty tick between packets immediately re-enabled
position and rotation damping. The trace shows yaw/pitch continuing to change
while follow repeatedly restarts at its first smoothed tick; it contains no
presentation `HOLD` loop.

Version `0.0.99` requires a four-source-period quiet window after the most
recent manual orbit sample before presentation follow may resume. Manual
rotation is therefore exact for the complete gesture, including packet gaps
and dynamic-block latch release boundaries. This is camera-state arbitration;
it does not change native volume queries, mesh classification, contact
distance, spring recovery or authored-shot ownership.

The next runtime trace isolates a separate multi-contact defect. The
presentation latch treated each node/resource identity as a new owner even
when contact never cleared. In one run it acquired 208 times, released only
38 times and switched owner 170 times without a clear interval. Some
focus-relative target changes were 900--1093 units, and each one reset both
presentation-history snapshots. The camera was therefore not following one
contact manifold; adjacent block faces repeatedly stole ownership.

Post-native correction also bypassed the existing 120-unit usable-distance
topology rule. It made nine exact commits below that boundary, including
0-, 10- and 15-unit camera positions. These are the screenshot's black
near-pivot states.

Version `0.0.100` treats the latch as one continuous contact manifold. A new
mesh identity may replace it only when the old focus-relative target fails a
native room query or overlaps a qualified current mesh/expanded OBB. A safe
old target survives the owner switch without a generation reset or history
normalization. Independently, a sub-120 post-native correction restores the
already validated pre-native submitted endpoint and cannot become an exact
publication or latch target.

The `0.0.100` runtime leaves one presentation-state race. A contact-only
commit updates the visible/published camera immediately, while the native
four-sample controller can advance its resolved point one source tick before
that point reaches the publication matrix. The old latch release counted the
safe committed publication as clear during this delay. In the measured loop,
the second such sample released the latch and the pending native point entered
the same block on the following tick.

Version `0.0.101` makes release evidence predictive. The raw resolved point is
captured immediately after the one native configure call and swept through the
same current render meshes while the latch is active. If it is blocked, clear
evidence is reset and the existing focus-relative latch target remains active
after revalidation. The latch releases only after two samples whose published
and pending native endpoints are both clear. This changes neither the spring
radius nor ordinary native wall/floor publication and performs no new direct
camera commit.

Version `0.0.102` adds two ownership boundaries without changing collision
thresholds. First, the desired orbit is clamped to a player-relative floor
envelope before native room and mesh validation, because the verified
room-volume predicate accepted the `0.0.101` endpoint below the player root.
Second, a scene-root change is detected before any render-only mesh latch,
follow target, head target or transition is applied. The first frame of a new
root is exact native state and becomes clean interpolation history.

The `0.0.102` run confirms the floor fix and exposes a remaining ownership
stack: native collision contraction, translation-only mesh retention and
render-only follow can each present a different endpoint. During mouse orbit,
retaining translation while accepting a new native orientation creates the
reported invisible orbit centre. Near a wall, following a previously displayed
endpoint on top of the contracted spring arm creates a delayed return followed
by a hard cut. Corrupt same-root exact frames also bypass the midpoint-only
presentation guard.

Version `0.0.103` therefore gives the complete manual gesture priority over
translation-only retention, gives native/mesh collision priority over
presentation follow, and permits exact-frame corruption rejection only while
that modern-camera collision owner is active. These are ownership decisions;
mesh qualification, spring radius, native room tests, contact margin, release
thresholds and authored-camera arbitration are unchanged.

Runtime establishes that presentation is not allowed to veto an exact frame:
the `0.0.103` collision-conditioned guard rejected 276 exact frames, with a
near-continuous rejection streak at a stair contact. The result looks like a
whole-game render freeze because the swap chain retains the previous image
while simulation proceeds. Version `0.0.104` restores the stronger invariant
that native exact presentation always fails open. Only synthetic midpoint
phases may be rejected. Manual-orbit latch arbitration and collision-owned
follow bypass remain unchanged; the first run recorded no follow hard cuts.

The `0.0.104` door run demonstrates that contact ownership must also include
the native position-history ring. A qualified pre-native mesh contact selected
one stable 609.5-unit endpoint, but post-native mesh contact appeared only on
every other tick. The uncorrected clear tick left a 235-unit intermediate in
history; the following contact tick restored the safe endpoint, producing an
exact two-position strobe. A retained latch face from before the manual orbit
could additionally become visible after input grace despite being far from the
current contact side.

Version `0.0.105` pins the validated submitted endpoint on the clear half of a
continuous qualified-mesh contact cycle once a latch has established that
contact. It also lets manual orbit refresh a usable latch target across
adjacent nodes, while continuing to suppress translation-only latch rendering
during the gesture. This is a contact-manifold state fix, not additional
damping or a new collision threshold.

The next pillar trace shows that pinning only the clear half is insufficient.
The complete desired-arm sweep selects resource 11432, but the legacy history
resolver sometimes shifts the result into resource 12613. Post-native
depenetration then chooses a second safe position and shortens the spring,
while the render latch may keep that 12613 face after exact publication has
returned to the 11432 solution. This is one physical contact represented by
two independently valid camera owners.

Version `0.0.106` gives the already validated pre-native submitted endpoint
single authority after contact has been established. The same endpoint now
owns exact publication, spring state and presentation latch even when the
post-native pass reports an adjacent mesh. An authoritative latch transfer
updates the current contact generation instead of retaining the preceding
face or normalizing presentation history. The temporal chord guard remains
responsible for unsafe synthetic interpolation between consecutive safe
endpoints. No collision distance, object-size classifier, native wall/floor
query or authored-camera rule changes.

The `0.0.106` trace confirms that ownership unification worked, but reveals a
geometric discontinuity within one blocker. Resource 11432 can provide two
opposite expanded-OBB escape faces. Selecting between them from the current
requested orbit vector makes an angular sign change produce a 958-unit exact
side swap while the player, focus and physical contact are unchanged.

Version `0.0.107` gives a near-pivot escape persistent face continuity. The
last accepted source camera selects the side for the next contained or grazing
escape, so orbit input cannot switch through the solid object merely by
crossing a directional tie. Once the complete arm has a normal radial point at
or above the established usable distance, ordinary collision takes over and
the camera may move around the obstacle. This is contact topology, not input
damping or a new distance threshold.

The `0.0.107` trace shows that a persistent face is still insufficient because
the escape representation stores only the face-normal endpoint. While the
requested orbit rotates through many directions around resource 12613, exact
publication remains at one coordinate for hundreds of source ticks. A modern
camera needs a continuous constraint surface, not a permanent point anchor.

Version `0.0.108` maps a contained pivot's requested horizontal ray to the
expanded OBB boundary. The endpoint stays on that ray and, when necessary,
continues beyond the first face only far enough to meet the already established
usable camera distance. As yaw changes, this construction slides continuously
across a face and through shared corners instead of either sticking or jumping
to an unrelated face-normal point. An already-outside pivot retains the safe
supporting-face fallback so no path is allowed back through the obstacle.

Version `0.0.109` closes the delayed native-history release path. A mesh latch
may count a clear sample only when the configured raw desired point, resolved
point, cached average and each of the four position-history samples are all
clear of qualified scene meshes. A pending blocked position keeps the current
focus-relative latch target authoritative and synchronizes the resolver's
complete history to it. This uses the known native history topology instead of
lengthening a grace timer: release remains responsive as soon as the complete
future publication path is actually clear.

Version `0.0.110` also treats a verified clear submitted arm as authoritative
during an established post-only mesh contact. This prevents a corrected
near-pivot history sample from becoming a permanent world-space camera anchor
while yaw continues to change. Authority is granted only after the whole
submitted arm passes the scene-mesh sweep and its endpoint has already passed
the native room-volume query; blocked arms and first-contact escape selection
remain unchanged.

Version `0.0.111` introduces continuity-aware mesh-corner avoidance. If the
new radial orbit hits a prop but the preceding camera arm remains clear after
being translated with player motion, that complete arm is kept as a temporary
detour. Both native room-volume and scene-mesh sweeps must validate it on the
current tick. This avoids unnecessary near-pivot collapse around multi-part
lift corners without delaying a real collision, weakening wall/floor
authority or relying on a frame-count threshold.

Version `0.0.112` handles the complementary case where player motion has made
the previous arm invalid before a mesh latch can be established. It projects
the previous camera direction onto the exact blocking triangle plane and tests
that continuity-aligned tangent at the previous distance. This is a geometric
surface slide, not collision delay: only a fully scene-clear and native-clear
tangent may replace radial contraction. No opposite-side tangent is attempted,
preventing an avoidance solution from flipping through the player.

The `0.0.112` test shows that a clear previous arm cannot remain the primary
solution after contact acquisition. While the requested orbit changes, that
branch can repeatedly republish one exact camera coordinate and present as a
stuck camera. Version `0.0.113` separates acquisition continuity from contact
motion. First contact may still use the previous-direction plane projection;
an established contact instead projects only the current source-tick orbit
displacement onto the blocking plane and applies that incremental tangent to
the previous clear arm. Thus the camera walks along the constraint surface
without a full-arm side flip. The previous point remains a fail-closed fallback
when the incremental target is rejected by either mesh or native geometry.

Version `0.0.114` restores native first-person as a separate, complete camera
state. Tab and R3 both drive the retail `ACTION_1ST_PERSON_VIEW`; mode byte 4
keeps gameplay input ownership even though the mode-3 callback is absent.
Physical mouse and right-stick relative deltas go to the native first-person
look path, while the modern third-person orbit is suspended and receives no
look input. This does not re-enable the discarded custom head camera or SELECT
camera-policy cycle.

### Phase C: spring-arm collision

- Add volume sweep, contact margin, immediate pull-in and damped release.
- Validate corners, low ceilings, narrow corridors, doors and elevators.

### Phase D: camera-relative movement

- Locate and hook the native desired-heading/controller path.
- Rotate the player toward camera-relative left-stick input while retaining the
  original motion, collision, animation and speed selection.
- Add soft and explicit recenter only after movement is stable.

Version `0.0.116` implements the first two items through the verified
`Dungeon.dll+0x44EA0` player-turn gateway. A circularly normalized left stick
selects a ground-plane direction relative to the persistent modern-camera yaw.
Only a bounded Q10 turn delta is substituted through the native selected turn
source for the duration of the original turn call; retail forward motion,
collision, animation and walk/run actions remain untouched. Large reversals
turn briefly in place before the existing forward action is asserted. LB and
native first person deliberately retain explicit side-step movement. Camera
recentering remains disabled until this control path is gameplay-tested.

The first `0.0.116` run exposed a state-entry omission rather than a coordinate
mapping error. Outside the forward arc the bridge released W, A and D
together, so the character remained in an idle state and never reached the
hooked turn gateway. Version `0.0.117` asserts one native turn side from the
sign of the wrapped target error, solely to enter and retain the ordinary
locomotion state. The hook continues to replace that selected source with the
exact bounded camera-relative delta during the original native call.

`0.0.117` also makes selector ownership immediate at the orbit filter. An
inactive right-stick owner clears accumulated filtered angular velocity, so a
stick movement used to select a radial item cannot continue rotating the
third-person camera. Returning the ordinary gameplay stick to centre remains a
normal filtered deceleration.

### Phase E: tuning and release

- Expose sensitivity, inversion, pitch limits, shoulder side, distance,
  deadzone, curve and recenter settings in the INI.
- Keep diagnostic logging opt-in.
- Ship only after the stable 50 Hz, UI, text, vibration and inventory tests all
  pass unchanged.

## Acceptance criteria

- Full 360-degree horizontal orbit and bounded vertical orbit from the right
  stick, with no character rotation while only looking.
- Camera-relative movement in every direction with native walk/run, animation
  and collision behavior.
- No camera penetration, corner popping or oscillation along walls.
- No forced recenter while the player is controlling the camera.
- No input-rate change between 16 Hz simulation and 50 Hz presentation.
- Menus, videos, loading, radial inventory, first-person mode, doors, elevators,
  combat, text and vibration behave exactly as in `v0.0.48-stable`.
- Disabling the feature restores the original camera without restarting the
  game.
