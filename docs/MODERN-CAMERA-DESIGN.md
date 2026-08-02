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

Version `0.0.190` adds a new, narrowly scoped immersive first-person owner
without reviving that discarded policy cycle. F10 or gamepad SELECT toggles
only between the persistent modern third-person rig and an eye endpoint inside
the same mode-3 source transaction. Tab/R3 and retail mode 4 remain unchanged.
The eye is derived from the live upper-body camera anchor, `HeadHeight` and
`HeadForwardOffset`; its short path is checked against the owned room graph and
qualified scene meshes before the detached camera matrix is atomically
published. Orientation uses the persistent user yaw and the wider configured
head pitch range. No retail first-person callback is invoked and no player,
weapon, visibility or culling field is changed, so the full animated body and
equipped weapon remain render-owned by the game. This source-owned publication
also works at x1; it is not the old x2/x3-only render translation.

The first live v0.0.190 run proved that the controller pointer is already an
upper-body anchor rather than the model root: adding the old render-only
485-unit root height placed the eye around 400 units above the visible model.
Version v0.0.191 therefore uses anchor-relative offsets `60/80`. The next live
test showed the apparent movement reversal was actually a 180-degree camera
orientation error: lowering the eye made the character's heels visible.
Version v0.0.192 restores the original signed longitudinal XInput channel and
reverses only the vector passed into Dungeon's look-at angle builder. The
persistent orbit/body course, keyboard W/S, retail first person and
third-person camera-relative movement remain unchanged.

The v0.0.192 live trace then exposed a separate translational defect. At
`focus=3247/5800/14095`, the render root is exactly
`player=3247/5400/14095`; this verified focus is a fixed root-relative anchor.
The controller pointer coordinates used by v0.0.190-v0.0.192 instead vary from
`-20` to `+166` longitudinal units relative to the render root during
forward/reverse motion, making the model slide through the camera. Version
v0.0.193 constructs the eye directly from `camera_focus`, preserving a rigid
character-relative offset. Defaults become `HeadHeight=60` and
`HeadForwardOffset=120`, which also raises and advances the view modestly.

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

The `0.0.117` runtime disproves `0x44EA0` as a universal heading gateway. It
remains specific to one locomotion wrapper; turn-in-place and other movement
states enter through `0x44E90`. Both call the shared canonical mutation at
`0x44DD0`. Version `0.0.118` hooks that shared function instead, preserving
the live-player guard and the temporary-source transaction. The surrounding
state machine, action selection, lean, animation, collision and position
remain native.

The `0.0.118` runtime still records no successful player-heading substitution
and leaves the synthetic A/D trigger as competing tank input. Version
`0.0.119` therefore returns the shipped preset to `CameraRelativeMovement=0`
and does not install the experimental hook. Camera-relative movement remains a
design objective, not a release claim, until a read-only diagnostic proves the
live player identity and exact native call lifetime. The stable camera and
selector right-stick ownership fix remain enabled.

### Validated controller ownership and two-phase steering (`0.0.120`)

The live gameplay pointer and the native movement callback argument are not
the same object. The former is the outer player entity; the latter is its
movement controller at `entity+0x114`. Heading, the selected turn source at
`+0x154`, and the `0x44DD0` argument must all be resolved relative to that
controller.

Turn-in-place is also a separate native path. Ordinary and fast turn states
update heading directly in `0x68970` and `0x68C20`; they do not converge on
`0x44DD0`. The camera-relative design therefore uses those retail states only
for large stationary reversals. Inside the forward arc it releases A/D and
asserts W alone. The resulting `0x7E530 -> 0x44EA0 -> 0x44DD0` locomotion path
accepts the bounded camera-relative delta while preserving native root motion,
animation, collision and walk/run selection. A 20-degree exit margin around
the configured entry arc provides state hysteresis.

The initial cardinal test of `0.0.120` exposed an input-axis difference, not a
state-machine failure: physical backward arrived as the positive vertical
component and therefore matched camera forward. Native root motion shows that
heading zero advances world +Z, ruling out a global 180-degree basis change.
`0.0.121` inverts only left-stick Y for camera-relative third-person movement;
`CameraRelativeInvertY=0` supports mappers that already provide the expected
sign. The controller resolver and two-phase steering remain unchanged.

The gameplay run then isolated a failure specific to that two-phase design:
when the desired direction started outside the forward arc, the asserted
retail A/D action entered `0x68970` or `0x68C20`. Those states bypass the
validated `0x44DD0` hook, so the native fixed-step turn could pass the desired
heading and continue circling for as long as the stick remained held.
Version `0.0.122` removes that competing state entirely. Every camera-relative
direction asserts W with A/D released and therefore reaches
`0x7E530 -> 0x44EA0 -> 0x44DD0`; the bounded shortest-angle substitution is
now the sole steering writer for both small and large heading errors. Native
root motion, animation, collision and run selection remain unchanged.

The `0.0.122` run invalidated the assumption behind that bridge. Fourteen
separate camera-relative activations produced only the initial sparse
`0x44DD0` hook traffic; once native locomotion was established, sustained root
motion did not revisit the gateway on every source tick. With W held and A/D
released, the character therefore continued along the existing heading for
every requested stick direction. Version `0.0.123` disables the experiment and
restores the known-good native tank mapping. A future camera-relative design
must first identify the recurring engine-owned heading writer used throughout
sustained locomotion; neither another input threshold nor another synthetic
A/D/W combination is an acceptable substitute.

Version `0.0.124` supplies that missing boundary. XInput axes now enter through
the retail DirectInput joystick poll at `+0x51500` and the engine's own
`JOY_*` action resolver. Sustained runtime probes identify `+0x7E530` as the
recurring root-motion locomotion callback. It selects walk/run turn sources at
controller `+0x130/+0x138` immediately before the original
`+0x44EA0 -> +0x44DD0` heading path. The camera-relative layer temporarily
substitutes only those values for the duration of the original callback and
restores them afterward; W/A/S/D are no longer part of third-person movement.

The first runtime pass through that design reached the recurring callback 57
times but passed its nested UI-owner validation only once. Version `0.0.125`
therefore carries the already validated controller identity from input
sampling into locomotion and validates the callback against that exact value.
If no successful steering call arrives within 250 ms, the input bridge
automatically restores the physical native joystick axes for the remainder of
that stick hold. This preserves controllable tank movement on any runtime
validation failure instead of continuing a forward-only fallback.

## Arkham Asylum ownership transfer (`0.0.126`)

The previous incremental collision work accumulated several independent final
position owners. Version `0.0.126` replaces that active topology using the
verified `BmGame.R3rdPersonCamera` responsibility split: bounded chase
position/velocity is applied to the followed target before collision, while
collision contraction and recovery remain source-tick state. Render-only
follow, render mesh latching, previous-world-point retention and tangent
detours no longer replace the accepted ordinary camera transform.

The evidence, constraints, algorithm and runtime test matrix are recorded in
[`ARKHAM-ASYLUM-CAMERA-TRANSFER.md`](ARKHAM-ASYLUM-CAMERA-TRANSFER.md).

## Deterministic native-axis steering (`0.0.127`)

The `0.0.126` log proves that `0x7E530` is intermittent across normal stick
holds: the 250 ms watchdog repeatedly changed a camera-relative request into
physical tank axes. Version `0.0.127` removes both the hook and this semantic
fallback. Desired camera-relative course is continuously converted to the
retail joystick's horizontal turn and forward axes. Forward strength is the
nonnegative alignment with the desired course, so rearward input first rotates
in place and can never become native backward movement.

Runtime rejects this design: native horizontal turn is not a continuous
actuator. Its sign and step are owned by the current locomotion/turn state, so
closed-loop joystick-axis feedback cannot guarantee convergence.

## Shared dispatcher heading (`0.0.128`)

All ordinary ground states register `Dungeon.dll+0x82750` as controller
`+0x2EC`. It refreshes input actions and then invokes whichever movement
callback is active at `+0x2F0`, including forward locomotion and both direct
turn states. Version `0.0.128` hooks this common pre-state boundary. Left-stick
magnitude remains native joystick forward input; horizontal joystick input is
neutral. A bounded camera-relative course delta is applied through the retail
`+0x44DD0` dual render/collision heading writer before the active callback.
Special states outside the common ground dispatcher retain complete ownership.

The first runtime validates convergence but shows the physical longitudinal
axis reversed. Version `0.0.129` enables `CameraRelativeInvertY` by default;
this is an isolated input-basis correction and does not alter the dispatcher,
turn step, native forward path or horizontal mapping.

### Phase E: tuning and release

- Expose sensitivity, inversion, pitch limits, shoulder side, distance,
  deadzone, curve and recenter settings in the INI.
- Keep diagnostic logging opt-in.
- Ship only after the stable 50 Hz, UI, text, vibration and inventory tests all
  pass unchanged.

## Acceptance criteria

The post-`0.0.129` read-only investigation of remaining source-tick jitter,
mesh sticking, synthetic-frame steps and Present stalls is recorded in
[`CAMERA-STABILITY-AUDIT-2026-08-01.md`](CAMERA-STABILITY-AUDIT-2026-08-01.md).
Its ordered phases supersede further threshold-only collision experiments:
stabilize chase mathematics and remove double position smoothing before
changing contact recovery or presentation guards.

Version `0.0.130` implements only the first behavioural phase. The followed
pivot now uses an implicit critically damped source-tick solve whose discrete
poles cannot alternate at 60 ms. Per-axis limits isolate height response from
horizontal turns. The native publication history and all collision and
presentation layers remain unchanged pending the isolated runtime test.

The runtime test validates that chase replacement: normal running median
relative camera motion drops from 41.0 to 3.0 units per source tick and
opposite-direction deltas drop from 173 to 30.

Version `0.0.131` then tested the proposed Phase 2 position-ring seeding, but
the isolated runtime rejected it. All 433 calls reported a successful write,
yet clear-flag submitted-to-published error changed from median 148.5/p95 429.9
to 177.9/431.9. Static reinspection explains the null result: `0x2F380` enters
`0x2F340 -> 0x2DC40 -> 0x2DC80`, which resets the ring's current/oldest
pointers before `0x2DEF0` inserts the one current native placement. Values
written into inactive samples before `0x2F380` are not members of the ring.
The old interpretation of clear-path error as a second persistent smoother is
therefore withdrawn; the difference includes current native placement and
source/publication timing.

The same run isolates a contact defect in unidentified render resource
`10017`. Its node transform/bounds change between samples, but the log has no
semantic name and does not identify it as a gameplay block. It produces 21
contained/near-pivot OBB escapes and changes supporting axis nine times.
Version `0.0.132` removes the rejected seed and retains only the axis
for the same node/resource while alternatives remain within a 96-unit
hysteresis band. The endpoint remains pivot-relative and is recomputed on the
current requested ray against the current OBB transform every source tick.
Materially better faces, a changed object, a clear/radial contact or a ray that
no longer reaches the retained face release it immediately. This is contact
identity, not final-position smoothing or a retained absolute target.

Version `0.0.133` makes collision-distance recovery contact-aware. Release
confirmation belongs to one blocker key and a non-regressing safe-distance
sequence; it is discarded when the blocker changes. A clear path must remain
complete for four source ticks before the arm begins its bounded outward
return. Immediate inward contraction remains authoritative.

The `0.0.133` runtime trace proves that contact identity must cross the retail
placement boundary. A pre-native native-wall release reached radii 644 and
708, but only the post-native position at 708 intersected scene-mesh resource
12613 and contracted to 580; the four-tick cycle then repeated. Version
`0.0.134` retains the last post-native verified radius and tests the next
bounded radius internally. Only a post-native-clear result advances the scalar
ceiling. It stores no world-space target and therefore cannot become another
orbit centre.

The `0.0.134` trace validates that scalar release path but finds a different
conflict for contained/near-pivot contact: the current OBB escape and a stale
post-native radial point alternated at approximately 619 and 128 units. In
`0.0.135`, the current-transform escape remains the final translation owner
for that tick after retail configure has updated room metadata. It is rebuilt
on the next tick and is never retained as an absolute position.

Version `0.0.136` corrects the scalar gate lifetime found by the `0.0.135`
trace. A temporary native-wall or pre-mesh safe radius is not evidence that a
post-native contact ended. The gate therefore survives while any pre-native
obstruction is present and releases only after the complete desired arm is
clear and post-native verified.

Version `0.0.137` moves synthetic modern-camera translation out of Cartesian
node interpolation. It linearly interpolates the two captured live pivots,
uses the shortest yaw arc, and interpolates pitch and collision radius. The
resulting pivot-to-camera ray must pass both the native volume query and the
stable scene-mesh sweep. When it does not, both synthetic phases select the
same current safe endpoint instead of alternating previous/current translation
inside one source tick.

The `0.0.137` runtime proves that translation alone is not a coherent safe
fallback: an accepted current origin with an interpolated orientation still
produces black-region rejects. Version `0.0.138` therefore selects the complete
current source-tick camera transform (world and local, including orientation)
for both synthetic phases whenever the reconstructed pivot ray is unsafe.
Clear pivot-relative phases continue to interpolate normally.

Version `0.0.139` adds a stateless collision resolution step for an unsafe
synthetic ray. The interpolated pivot/angle remains authoritative; only its
phase-local radius or qualified near-pivot escape is clipped by the existing
native and scene-mesh geometry routines. The complete result must pass both
queries again. Failure still produces the atomic current-transform fallback,
but a resolvable contact now slides through safe intermediate phases instead of
jumping directly to the next exact view.

Version `0.0.140` is a behaviour-neutral Phase 6 audit. A D3D-rejected
midpoint sample is compared with the exact sample that follows it in the same
source tick. Persisted black cells describe the new exact view; recovered cells
exist only in the synthetic phase. This evidence is required before narrowing
the synchronous guard or moving it off the Present path.

That audit identifies a missing invariant rather than a visibility heuristic.
The scene clipper may return a point only 4--8 units from the interpolated pivot
when a near-pivot escape cannot be constructed. The source collision path
already treats distances below 120 units as unusable, but the first synthetic
resolver omitted that check. Version `0.0.141` applies the same minimum before
acceptance and otherwise keeps the complete exact-current transform. This
prevents a formally clear ray from becoming an inside-character near-plane
view without adding persistent state.

Version `0.0.142` aligns render-cache ownership with the phase-local camera.
The retail camera-cache routine is not a gameplay integration when revisited
at the same engine frame: the mode-3 callback is idempotently rejected, after
which only the camera node, published matrix, bounds and camera-dependent
render caches are rebuilt. Each midpoint performs that refresh after its
transform is installed; restoring the exact snapshot performs it again. Thus
the synthetic camera and its visibility/render state belong to one transform,
while the source tick remains the sole owner of orbit input and collision
history.

Runtime rejects the `0.0.142` replay boundary: player rendering jitters during
movement, proving that the camera-cache tail mutates additional presentation
state outside the known transaction. Version `0.0.143` removes that experiment
and returns to `0.0.141` behaviour while retaining its minimum synthetic
distance and D3D audit. The full `0x3860` routine must not be replayed at a
synthetic phase without a complete rollback contract.

The `0.0.143` rollback validates that boundary: player jitter is gone, while
same-tick audits attribute recovered black regions overwhelmingly to arbitrary
synthetic camera transforms (89.6% recovery after clipped phases and 84.4%
after clear pivot-relative phases). Complete current-exact fallback recovers
only 4.9%, so it agrees with the following exact render-cache state.

Version `0.0.144` makes that fallback the normal modern-camera presentation
policy. Both synthetic phases use the complete current exact camera transform;
actors, animation and UI keep their 50 Hz interpolation. This trades camera-
transform interpolation for a single cache-compatible camera owner and does
not alter source-tick chase, collision, input or authored-camera arbitration.

Runtime rejects `0.0.144` as a global policy: the current exact camera and the
interpolated followed actor belong to different presentation times, producing
visible player shake and reduced smoothness. Version `0.0.145` restores the
coherent pivot-relative path from `0.0.143`; exact-current remains a local
fallback for unsafe synthetic phases rather than the normal camera owner.

Distance release is independently changed in `0.0.145`. The post-native gate
still requires three clear source ticks before its first outward probe. Once a
probe is accepted by both native and scene-mesh validation, the gate retains
release readiness and may test the next 64-unit step on the next source tick.
It no longer inserts three stationary ticks after every accepted step. A
rejected candidate or renewed contact resets readiness immediately.

The `0.0.145` runtime accepts this policy. Player-relative presentation is
stable again, the user reports smooth camera pull-back, and five logged gate
releases reach the requested arm through consecutive validated steps. This
accepts only the release-cadence change; it does not accept global collision
stability.

The same run contains a separate resource-`12613` squeeze between the player,
flag geometry and native wall. The near-pivot fallback publishes the identical
world point `-70/400/15148` for 39 consecutive contacts while requested orbit
continues rotating, then changes escape face. Elsewhere the same resource jumps
`203 -> 561` and `679 -> 207` units as local axes 0 and 2 exchange ownership.
The outside-pivot branch claims to slide along a supporting face, but its
candidate changes only one discrete face coordinate and uses the preceding
camera as the selector; it therefore recreates the fixed-point anchor rejected
by `0.0.107`. The required correction is a stateless supporting-face slide:
hold only the current safe outside coordinate and derive the tangent coordinate
from the current requested orbit. No world-space endpoint or larger axis
hysteresis may be retained.

Version `0.0.146` makes the outside-pivot path a real stateless surface slide.
The single safe support coordinate cannot cross the expanded OBB, while the
tangent coordinate follows the current requested orbit. At tangent zero the
camera moves outward on a continuous 120-unit arc instead of choosing either
discrete neighbouring face. Contained pivots use their actual first ray exit,
not a later preferred face. Native wall and complete scene-mesh validation
remain authoritative after construction.

The runtime does not accept this path: overall jitter increased, so `0.0.147`
removes it. More importantly, the final trace isolates a different four-source-
tick loop while that path is inactive. Retail history alternates a roughly
302-unit delayed publication with the scalar gate's 179--183-unit exact mesh
correction even though the requested orbit and player pivot are stationary.

`0.0.147` makes the established post-native scalar gate own pending native
history as well as its radius. A currently clear published sample is accepted
only when the resolver's desired point, resolved point, average and complete
four-sample ring are mesh-clear. Otherwise the current verified scalar radius
is rebuilt on the current orbit, rechecked against both native volume and the
full scene-mesh arm, and published exactly in the same source tick. This is not
a camera-position latch: only the scalar ceiling persists, so player movement
and mouse/right-stick orbit always rebuild a new endpoint.

Runtime validates that pending-history ownership but exposes a separate
source/presentation disagreement. At one stationary resource-12613 contact,
the exact phase repeatedly shows a 725.7-unit pre-native OBB escape while both
synthetic phases clip the same arm to 134.8. The post-native source pass also
clips it to 134.8, but the pre-native exceptional commit then restores 725.7
before the exact snapshot is captured.

`0.0.148` orders these two sources of evidence explicitly. A post-native sweep
of an unchanged submitted endpoint is a complete current-arm test and wins
when it returns a materially different safe endpoint. The pre-native escape
continues to override only a different stale retail-history publication, which
has not tested the current escape. Exact and synthetic phases should therefore
share the same collision solution without weakening the older delayed-history
protection.

The next runtime shows that per-tick veto alone is insufficient: direct tests
are rejected, but alternating delayed-history ticks still recommit the escape,
producing a stable 489.4/267.6 exact-source two-cycle. `0.0.149` fixes the state
priority rather than adding another predicate. Once a post-native gate has a
verified ceiling for the same blocker key, its current-orbit scalar solution
owns selection ahead of the non-radial escape. Checked outward probes or a
changed blocker are the only release mechanisms.

Runtime `0.0.149` exposes the more general defect beneath those ownership
rules. A rejected outward probe is currently "restored" by rebuilding the
verified scalar radius on the requested orbit. Equal radius does not imply an
equal camera transform: the final stationary loop alternates two 214-unit arms
whose endpoints are about 186 units apart.

The correct recovery model is transactional. `0x2F380` supplies the current
native publication plus queued speculative state. When the current publication
is scene-mesh clear but a queued point is not, the speculative transaction must
be rolled back to that exact current native publication. It must not synthesize
a replacement from the scalar ceiling. This makes a rejected probe visually
and historically idempotent while preserving native room, floor, sector and
orientation ownership. Only acceptance may change the visible endpoint or
advance the ceiling.

The same invariant covers a current publication which does intersect a scene
mesh. The post-native sweep has already shortened and exactly committed the
current focus-to-publication ray. That Cartesian safe prefix is the accepted
transaction result. Rebuilding the former ceiling on the requested orbit would
again be a new camera position, not rollback, and is forbidden regardless of
resource identity or room coordinates.

Runtime `0.0.150` then isolates a constraint-composition error rather than
another failed rollback. A pre-native near-pivot escape around one scene node
and a post-native radial clip against a second node alternate ownership every
source tick. Comparing their resource keys is invalid: pipeline stages observe
different portions of the same constrained camera path, so different keys may
describe simultaneous blockers. An active post-native gate consequently owns
ahead of every pre-native escape until the complete desired arm is pre-native
clear and its outward candidate passes post-native validation. Blocker keys
remain diagnostics and reset recovery evidence; they do not grant publication
ownership.

Runtime `0.0.151` validates that composition rule: the permanent contact loop
is gone. The remaining narrow-corner failure is instead a non-atomic camera
pose. Exact scene-mesh commits update translation and positional history while
leaving the orientation produced for another native-history endpoint. A fixed
camera origin can therefore retain collision ownership while its view basis
continues to follow a rotating requested arm.

`0.0.152` restores the missing invariant: a collision result owns a complete
pose, not translation alone. On the exceptional exact-commit path the retail
`0x30730` look-at leaf derives angles from the accepted endpoint and current
focus. The normal downstream `0x3A980/0x3AC00` node update constructs the
matching bases and published matrix. Ordinary `0x2F380` publications remain
byte-for-byte native, and authored modes never enter this writer. This is
deliberately narrower than the rejected `0.0.142` cache replay and the rejected
`0.0.85` global position override.

Runtime rejects the `0.0.152` target assumption. The retail caller does not
pass the modern focus directly to `0x30730`; it passes a target selected by the
native resolver and optionally modified by authored camera logic. Version
`0.0.153` removes that direct call rather than guessing the missing target.

The narrow-space fix is instead placed at the geometric and transactional
owners. Pivot containment in an expanded object OBB is an allowed prefix of
the camera ray, not proof that every orbit direction is blocked. The prefix is
removed and only the segment after its exit is swept against real triangles.
The accepted boundary is idempotent, and the post-native scalar gate advances
only by actually published, post-mesh-verified distance. These rules preserve
current-orbit responsiveness and cannot create a retained invisible centre.

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

## 0.0.154 measured ownership correction

The 0.0.153 runtime demonstrates that containment, post-native escape and
native release delay are distinct. `initial_overlap` ray exits occur, but the
reported invisible-centre/sticking runs use the old outside-pivot
`near_pivot_escape` and repeatedly commit one point while yaw changes.

The outside-pivot constraint is now a surface, not a point: one safe support
coordinate remains outside the expanded OBB, the tangent follows the current
orbit, and a continuous 120-unit arc handles zero tangent. Exact collision
translation and orientation are also one transaction. The look target is
captured from the actual native `0x2F380 -> 0x30790 -> 0x30730` call and reused
only for its same-tick exact correction. Finally, an unexpectedly shorter
native publication is immediate safety contraction and invalidates outward
release readiness; it cannot be paired with an immediate return probe.

The resulting `0.0.154` release is installed with SHA-256
`3A0936BF42AD7CEE80AAE7FE9E245263C207EC41FE439E96932B18C5E3DBB4EC`.
Local state, proxy and installer tests pass; the flag and narrow-corner runtime
cases remain the acceptance test.

## 0.0.155 publication/history ownership

A safe source-tick publication and the resolver's queued next position are
different temporal state. When the current native publication is mesh-clear
but a pending sample intersects a scene object, collision owns only the
current visible result and release readiness. It must not copy that world point
over desired, average and the history ring. Doing so removes all tangential
orbit progress and creates the invisible-centre behaviour measured in
0.0.154. The queued state remains native-owned until its next publication,
where the ordinary post-native sweep can accept or contract it before render
presentation consumes it.

The installed `0.0.155` runtime SHA-256 is
`387FAC51C52D5F252A2B0DD2DDC3D4F28256F53481475E84DFA820E13C096745`.

## 0.0.156 targeted future-state sanitation

The 0.0.155 assumption that an unsafe queued desired position could simply be
left for the ordinary next-tick post-native clip was false. That next tick made
the unsafe value visible before contracting it, producing alternating
defer/commit positions and an effective half-rate camera update.

Pending collision ownership is now per position field. The current resolved
publication is immutable in this path. Desired, cached average and each of the
four history samples are swept independently, and only a field with positive
scene-mesh contact is eligible for replacement. Its replacement remains on
the field's own focus ray and must pass native room-volume, scene endpoint and
repeat-clip stability checks. Failure retains the verified current point in
that one field rather than flattening the entire temporal filter. This keeps
both safety and tangential progress without introducing another camera owner.

The installed x86 `0.0.156` DLL SHA-256 is
`592047A0871BCC6F3E423C672784DC4FD2B0A6E859D150C17C73ED294D9568C5`.

## 0.0.157 pre-publication mesh veto

Runtime 0.0.156 confirms that individual future-slot sanitation makes the
final endpoint much steadier but cannot prevent `0x2F380` from recreating the
unsafe candidate after its ring reset. Collision ownership therefore moves to
the last native candidate boundary before temporal filtering:
`0x2EDC0 -> 0x2EFFF -> 0x2DE30(controller+0x204, position)`.

The generic ring-adder is not globally redefined. Replacement is permitted
only while the overlay's one modern configure call is active, for its exact
controller, and for the `+0x204` position ring. The four other histories and
all retail/authored calls pass through byte-for-byte. A positive scene-mesh
contact is clipped and checked against native room volume and the complete
scene endpoint before insertion. Invalid or unavailable validation calls the
original ring-adder with the original position and leaves the existing
post-native exact safety net responsible.

The installed/build/dist x86 `0.0.157` DLL SHA-256 is
`D4FB37A0E3F8A875378451C342DA97CDB79B7BEE85B2DC0A32EB01D0659E4CB0`.

## 0.0.158 independent gameplay and render-rate solve

Gameplay hooks no longer depend on presentation multiplication. The supported
game image, modern mode-3 hook, native joystick bridge, camera-relative
dispatcher, selector, first-person and event hooks initialize regardless of
NativeRender `Enabled` or `Subframes`. With zero subframes, the installed
scheduler remains the single real input boundary but calls the original
renderer once without synthetic capture or interpolation.

The source camera has one position owner. The current focus-to-endpoint spring
arm is resolved before native configure, revalidated at the native position
ring insertion and replaces a different resolver-shaped positional candidate.
Sector bookkeeping and the resolver-selected look target/orientation remain
native. This differs from rejected 0.0.85: no finished matrix is globally
overwritten, and ownership is limited to one modern configure scope and one
position ring.

The render-rate camera is a phase solve rather than interpolation between two
finished collision answers. Each synthetic pass builds collision geometry
from interpolated node transforms and bounds, interpolates the pivot and polar
arm, then solves native plus scene-mesh collision for that phase. A genuinely
unsolved phase holds the preceding complete pose and never advances to the
future exact transform before the rest of the scene reaches that source tick.

The installed/build/dist x86 `0.0.158` DLL SHA-256 is
`539CF94C508D19F4C1337C621310CD87A91B87E10F8101BE2A405B6978A9BF85`.

## 0.0.159: one camera clock, interpolated focus follow

The camera controller is a source-rate gameplay system even when the renderer
presents two synthetic phases. Its accepted exact matrix is the only complete
camera pose. A synthetic phase computes an interpolated focus and adds its
delta from the preceding focus to the preceding exact camera origin. Rotation,
orbit radius and collision state remain byte-for-byte from that exact pose.
This gives the 50 Hz actor a matching translational follow without asking a
second collision solver to reinterpret the flag, wall or dynamic-object
topology between source ticks.

Safety at presentation rate is origin-based. The carried origin must pass the
native room-volume query and must not occupy a qualified phase-local scene
mesh. If it fails, presentation selects one complete exact matrix; it never
combines one endpoint's translation with another endpoint's orientation and
never publishes a newly clipped radial point. The next source tick therefore
cannot inherit presentation-only collision state.

The native `controller+0x204` history detour is a mesh veto, not a general
position owner. No positive scene-mesh intersection means the original native
candidate and its wall/room shaping pass through unchanged. A mesh correction
still requires native-volume and endpoint validation, with the existing
post-native path retained as fail-closed backup.

The installed/build/dist x86 `0.0.159` DLL SHA-256 is
`9D5605FA3BFCB3CFF5656A4288974EAC9B717D46529C273B4548C4C949A88839`.

## 0.0.160: defer gameplay camera until exact render

Synthetic presentation must not move the gameplay camera update earlier in
the frame. The actor scene cache is refreshed before capture as before. The
camera cache is instead deferred transactionally: save `camera_owner+0`, stamp
it with the current engine frame for midpoint renderer calls, then restore the
saved value before the exact original renderer. Thus synthetic calls observe
the last complete exact pose but cannot invoke mode-3, history or downstream
camera cache work. The exact call follows the same native sequence as x1.

After the exact renderer returns, the live camera world/local matrices and
focus replace the stale camera fields in the current scene snapshot before it
is advanced into interpolation history. The next interval therefore follows a
real exact endpoint rather than a synthetic or pre-cache estimate.

The phase-local expanded-OBB endpoint test is not part of presentation safety.
Runtime 0.0.159 classified ordinary clear camera origins as occupied on 410
phases and caused continuous exact-pose switching. Focus-follow now falls back
to the preceding complete exact pose only for a native room-volume rejection.
Qualified scene meshes remain handled by the exact spring-arm transaction.

The installed/build/dist x86 `0.0.160` DLL SHA-256 is
`8C2D6F35949F3F900461A555F6524187FB76AD592C86EEC4AEB7B9F992468ED7`.

## 0.0.161: exact mesh ownership and x1 scene history

Runtime rejects camera-cache deferral: it removes presentation guards but not
the stationary flag cycle, while the source-rate camera makes an interpolated
actor/world visibly judder. Exact diagnostics instead show a four-tick loop
created by `submitted_fallback=1` in the pre-history mesh veto.

Pre-history replacement is now single-source. A positive mesh contact produces
one scene-clipped candidate. If that exact candidate cannot pass native volume
and endpoint validation, no alternate submitted point is inserted into the
native ring. The unmodified candidate continues to post-native correction.
This prevents a failed constraint from periodically changing positional owner
after the scalar gate reaches its third clear tick.

Synthetic camera presentation again interpolates the two complete exact
orbits in pivot space, including shortest yaw, pitch and radius. Native room
volume is the only presentation-time clip. Qualified scene props remain owned
by the exact source spring arm rather than a second phase-local OBB solver.

When synthetic presentation is off, the original renderer runs first and the
overlay captures its completed exact scene afterward. Two consecutive x1
snapshots therefore keep source mesh collision supplied with current/stable
object transforms. No additional camera callback or render is executed.

The installed/build/dist x86 `0.0.161` DLL SHA-256 is
`CF31ABCBF375564247DED43871EA9D46B6EC3DEDBE1DC55FD6A70B6FC4DC6FA0`.

## 0.0.162: one boundary contract and one candidate owner

The flag/door failure was not a damping problem. Two source stages disagreed
about geometry: the spring-arm sweep used real triangles and returned an
eight-unit-backed-off endpoint, while pre-history endpoint validation rejected
that same point for remaining inside a much larger conservative OBB. Native
history therefore received the unsafe point and post-native code corrected it
after publication, producing a deterministic A/B camera.

The source pipeline now has the following non-overlapping responsibilities:

- orbit/chase state owns the requested focus-relative arm;
- native volume queries own rooms, walls, floors and portal traversal;
- the scoped pre-history hook owns qualified scene-mesh candidate replacement
  before the retail position-ring average;
- retail owns the average, sector and resolver look target;
- post-native mesh correction is safety-only and cannot become a second normal
  candidate path;
- exact snapshots own x1/x2/x3 history, while synthetic interpolation is
  read-only with respect to gameplay camera state.

Expanded OBBs remain valid broad-phase and exceptional contained-pivot escape
tools. They are not proof that an already swept, backed-off endpoint occupies
render geometry. Render-only latch/follow state and targeted future-history
repair have no role in this design and are deleted.

Installed/build/dist x86 `0.0.162` SHA-256 is
`4A0121D2C2205E8FA9CE202E7C6B2FEBB517846EFC7D2DE7B5FC02F3E3C26AB3`.

## 0.0.163: one collision state machine

Runtime 0.0.162 showed that a separate post-native radius ceiling violates the
one-owner design. Retail position history is deliberately delayed; using its
published radius to approve or reject an outward collision probe closes a
feedback loop around a temporal filter. That loop can oscillate even when the
current requested arm, native volume and scene mesh are all clear.

The camera therefore has exactly one persistent collision state machine:
`ResolveThirdPersonSpringArm`. Its input is the current desired arm plus the
nearest safe endpoint from current native and qualified scene sweeps. It owns
immediate contraction, blocker identity, clear confirmation and bounded
outward recovery. The post-native pass owns no recovery state. On positive
current contact it exact-commits a safe endpoint and contracts the same spring
state; on a clear result it does nothing. Native history remains presentation
output and can never lower a collision ceiling.

Installed/build/dist x86 `0.0.163` SHA-256 is
`D9AABAA44A4AB9982C0DD732CF5C13E164CF5938FBDD2169D4769ACE0CEA2E34`.

## 0.0.164: escape is a complete-current-constraint candidate

A non-radial escape is exceptional topology handling, not an authoritative
override. It may be committed only when the complete current configure call
does not produce a different qualified scene target. Native history latency is
irrelevant to that decision. This makes a squeeze between two objects
fail-closed to the configured safe result instead of alternating each object's
individually valid projection.

Escape construction also preserves the requested pitch. The OBB solver still
selects a deterministic horizontal supporting face, but its vertical component
is reconstructed from the requested orbit using the escape's normalized
horizontal progress. Thus rotating around a prop changes the collision target
continuously instead of snapping the camera to pivot height.

Installed/build/dist x86 `0.0.164` SHA-256 is
`0FC6D6D6F43779116B02B54ED131E06F949E6EC8184A1111BB6DCD1A90724554`.

## 0.0.165: obstacle scale and continuous moving-target chase

Render meshes are camera obstacles only when at least two intrinsic scaled
axes span two complete camera diameters. This prevents long thin decoration
from becoming a wall merely because its world AABB rotates, while retaining
large static and kinematic blocks. The rule is geometry-based and applies
uniformly to every resource.

Follow-state discontinuities are reserved for initialization, invalid data and
the existing single-tick teleport test. Accumulated chase lag is not a
teleport signal: a continuously falling focus may legitimately stay more than
900 units ahead of a bounded smoother. It must converge through the same
backward-Euler state rather than periodically resetting to the target.

Installed/build/dist x86 `0.0.165` SHA-256 is
`B5D13CB513B27B6CEE0879D3C39B8580F5F054A875BDCD06A42E73897A57E982`.

## 0.0.166: fixed-point history transaction

The v0.0.165 stationary flag trace disproves object-size filtering as the
solution. Resource 12613 still enters the collision path, while increasing the
general threshold risks ignoring thin closed doors. The 192-unit two-axis rule
is restored; collision resolution, not asset shape, owns this defect.

The final trace has a constant focus, requested orbit and submitted endpoint
`-548/414/14768`, but retail repeatedly inserts candidate
`-715/544/14828`. Candidate-derived validation cannot compose the two nearby
mesh constraints, so the unmodified candidate reaches post-native correction
and produces the exact four-position loop. The configure call is now a
transaction. Candidate-derived replacement remains primary. On failure, the
submitted endpoint passes through the same complete native-volume,
scene-sweep, endpoint and repeat-stability validator. It may replace the ring
candidate only if the validator returns it byte-for-byte unchanged. No changed
submitted-side point, retained world latch or resource exception is accepted.

This follows the common engine invariant rather than copying an engine API:

- Unreal Spring Arm separates the unfixed desired position from one collision
  result and uses a sized camera probe;
- Godot SpringArm3D performs one ray/shape motion cast from pivot to desired
  length, with a margin, and recommends a volume (often a sphere) instead of a
  point ray for smooth edge behaviour;
- Unity Cinemachine selects the obstacle nearest the target, handles multiple
  contacts within one bounded solve, retains the accepted correction, and
  separates occlusion response from slower return damping.

Primary references:

- https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USpringArmComponent
- https://docs.godotengine.org/en/stable/classes/class_springarm3d.html
- https://docs.godotengine.org/en/latest/tutorials/3d/spring_arm.html
- https://docs.unity.cn/Packages/com.unity.cinemachine@3.0/manual/CinemachineDeoccluder.html
- https://github.com/Unity-Technologies/com.unity.cinemachine/blob/main/com.unity.cinemachine/Runtime/Behaviours/CinemachineDeoccluder.cs

The fall correction is independent. Exact vertical pivot damping is removed:
Y follows the current focus directly while horizontal follow remains damped.
This prevents a continuously falling target from stretching a 1400-unit orbit
above 2100 units and provoking a reverse native correction. Presentation
interpolation still supplies smooth intermediate Y at x2/x3.

The first installed x86 `0.0.166` SHA-256 is
`6BF68DABAA36574228175DE75A2F042E0EF1F68E2EAFF695C20EE3D6140B03AE`.
The latest build/dist adds only explicit submitted-validation coordinates and
has SHA-256
`0FC4C08438B5021AA244B1245146A2AB83E2AE6B0C344335348D9F23F828C3AF`;
installation is deferred while the game process is running.

## 0.0.167: idempotent accepted boundaries

The completed v0.0.166 run is
`<game-directory>\logs\deathtrap-native-20260801-230007-111-pid41168.log`.
It proves that input ownership is intact in both failures. In the final
two-block corner, requested orbit positions traverse every quadrant, but the
published, desired and resolved positions remain exactly
`-8844/-884/16304`. Resource 13676 returns that same point as its post-native
escape on every tick. The former implementation nevertheless performs an
exact commit every time, leaving the arm at radius 9 and resetting
`clear_ticks` to zero. Resource 12613 produces the same unchanged-publication
pattern at the flag.

The correction phase must be idempotent. If its result differs from the
already published integer position by no more than two world units, committing
it cannot materially improve safety. Version 0.0.167 records the contact but
performs no state transition: it does not rewrite the native history ring,
does not change the collision radius or release counters, and does not veto a
validated pre-native escape derived from current orbit input. Material
corrections still use the existing exact fail-closed transaction.

The x86 build, spring-arm state test, DirectInput proxy smoke test and installer
CRLF test pass. Build/dist SHA-256 is
`9C854ECD5276BE186272015D09C461DD6EDD3A2F7B8047C2A4C5CE48415FA4F7`.

## 0.0.168: collision follows renderer membership

Presence in the scene-node tree is necessary but not sufficient for camera
collision. The retail renderer can retain a node's transform, resource handle
and bounds while suppressing that node's own draw. Its verified
`0x02000000` node flag skips only the resource submission and preserves child
traversal. Treating such a retained resource as solid creates deterministic
invisible contacts at the same yaw in an otherwise empty room.

Scene collision now captures the node flags and applies the same own-resource
eligibility decision before all broad phases and triangle queries. This is a
semantic collision-channel correction, not temporal damping: visible walls,
doors, moving blocks and drawable children keep their existing sized-sphere
sweeps, immediate safety contraction and bounded recovery. Hidden/inactive
parent resources cannot create a `1400 -> 1373 -> 1400` orbit sawtooth.

The x86 build and both repository tests pass. Installed/build/dist SHA-256 is
`1972C919616133E29EF1FD9B16D8A9829E5F759AABB746B5473F0C223573CDDF`.

## 0.0.169: cardinal right-stick intent

Collision and look input are independent owners. A camera may follow a
perfectly clear scene path and still develop a repeatable low-pitch wobble if
a nominally horizontal physical stick reports a persistent vertical component.
Because orbit input is velocity, even a small residual accumulates until the
pitch or floor constraint is reached.

Third-person stick input therefore passes through a proportional axial cone
before its response curve. The dominant axis is unchanged. The secondary axis
is zero inside the cone and restored continuously toward a diagonal, avoiding
both long-term cross-axis drift and a hard angular threshold. This filter is
limited to third-person orbit and does not reinterpret mouse deltas, movement,
the radial selector, menus or native first-person control.

The x86 build and both repository tests pass. Installed/build/dist SHA-256 is
`1C3194A60E7E2ECDD61046526A3BC2E54E119FDD6E3C69F7102FB1DA549A7989`.

## 0.0.170: strict-clear final position owner

The modern solver and the retail fixed-camera resolver must not both shape a
clear orbit. Runtime v0.0.169 proves that the requested arm can be an exact
circle while the downstream retail position repeatedly changes radius,
height and even reverses yaw for one tick. Interpolating that result only makes
the source discontinuity visible at more presentation samples.

Final position ownership is therefore explicit but fail-closed. `0x2F380`
still runs exactly once for room/sector state and the native look target. The
modern endpoint replaces only its positional result when all of these are
true:

- the arm is at its complete requested radius;
- neither the desired-arm nor post-native scene query has contact;
- no exact correction or exhausted solver already owns the tick;
- the endpoint is clear of qualified scene triangles;
- every one of the seven underlying native room traces succeeds.

The last requirement is intentionally stronger than retail `0x30910`, which
accepts when any one of those traces succeeds. It is the missing safety
distinction from rejected v0.0.158. Any constrained or ambiguous tick remains
fully native-owned, so this change does not reinterpret walls, floors, portals,
moving blocks or contained-pivot escapes.

Installed/build/dist x86 `0.0.170` SHA-256 is
`B0A28F1B5D2FE6FB70CD7160A9F200FDE6FEFF74E68CBA48ED0E60BDCD6800E1`.

## 0.0.171: position, volume and sector are one camera state

A valid camera endpoint is not only a translation. It consists of three
coherent parts:

- the centre position and orientation;
- the collision footprint around that centre;
- the room sector used by traversal and culling.

Version 0.0.170 enforced source position ownership but its strict room proof
sampled only the focus-side offsets inherited from retail `0x30910`. Version
0.0.171 adds a separate endpoint footprint: centre plus six cardinal samples
at the established 96-unit camera radius must all resolve and trace to the
focus sector. This deliberately remains fail-closed; it does not make thin
objects pass-through and it does not add another spring or fallback position.

The retail cache tail proves that `camera_node+0x100` is the sector belonging
to the final node translation. Exact modern publication therefore resolves
the submitted endpoint and commits that node sector together with
`controller+0x200`, controller position/history, node matrices and published
matrix. The transaction matcher includes both sector fields, so an apparently
unchanged translation cannot conceal stale room ownership.

Interpolation receives the same invariant without becoming a persistent
gameplay writer. Each synthetic pass resolves the camera node sector from the
already selected midpoint translation immediately before rendering. It then
restores the exact-current sector along with the exact matrices. The unsafe
same-frame `0x3860` camera-cache replay rejected in v0.0.143 is not restored.

Installed/build/dist x86 `0.0.171` SHA-256 is
`4BB179E196E6C668B240835290D56E506B0CA8DD9CDA7FA671DF87781B069F9C`.

## 0.0.172: one collision predicate and one contracted-position owner

The complete camera footprint is now a general modern spring-arm invariant,
not merely permission for an unconstrained exact endpoint. Desired orbit,
binary contraction, rounded endpoint validation, scene-pushout validation and
synthetic pivot phases all use the same all-trace room predicate. This closes
the ceiling case where retail `0x30910` returned clear because one of seven
rays survived while the camera sphere crossed the plane.

Room geometry and scene geometry remain separate collision channels, but they
no longer publish competing positions. A scene-mesh contact first selects the
safe spring radius. After the required ordinary native configure transaction,
the same selected point must still pass the complete room footprint, minimum
distance and scene endpoint occupancy tests. With no conflicting post-native
contact it owns final translation atomically; the legacy fixed-camera result
cannot substitute a different height or radius. Ambiguous results remain
fail-closed on the native path.

Installed/build/dist x86 `0.0.172` SHA-256 is
`9F25C74A6BDDD5435917DC999BB27B6BABE524ADEB93153074AE3428FC197A54`.

## 0.0.180 detached owned-pose publication audit

The full-owned room and scene solver has passed its combined-distance gate;
publication is now isolated from collision tuning. The live cache leaves are
not pose-only: `0x3A980` and `0x3AC00` recurse through node children, and the
latter also updates resources, bounds and child aggregates. They are therefore
run only on a detached camera-node copy whose external writable edges are
cleared. The previous exact raw position/angles must reproduce its captured
local/world matrices byte-for-byte before a proposed position/focus pose is
accepted as constructible.

The audit writes no live camera state. It re-resolves the final combined
endpoint through the owned room graph, builds its local/world matrices on the
detached node, and compares the complete live camera node, published matrix
and player-node bytes before and after. Runtime ownership cannot advance from
shadow mode unless all three remain unchanged and the detached world
translation equals the selected endpoint.

## 0.0.181: atomic full-owned source publication

The 0.0.180 runtime audit passes the actual mutation-safety gates. Of 78 logged
audits, every detached local/world rebuild, owned translation, sector pair and
camera/global/player untouched comparison is exact. The only pre-publication
global/live mismatch is the expected first orbit frame after a scene change,
where the global still represents the preceding exact render. Publication must
establish coherence; it cannot require stale owners to be coherent first.

Ordinary third-person gameplay now has one source owner. The combined room and
scene result is fed through a separate owned radial state, then the rounded
intermediate is revalidated through both channels. Contraction is immediate;
clear recovery uses the existing tested bounded spring policy. If a radial or
angular intermediate enters the three-radius unsafe region while the selected
shot is useful, the camera cuts to that already verified shot and presentation
suppresses the corresponding interpolation chord once.

The live transaction writes only the narrow proven state: controller resolved,
desired and history positions; controller and camera-node sector; node source
position/angles; detached local/world matrices; and the global published
matrix. It does not call `0x3860`, `0x3A980`, or `0x3AC00` on the live node and
does not recurse resources, callbacks, children or the player. All touched
fields are backed up and the post-write state is compared in full. Failure
restores the backup and chooses the frozen hybrid for that source tick.

The native callback remains the sole owner during explicitly arbitrated
scripted reveals. Successful owned publication returns before the legacy
native configure path, so full-owned and hybrid/native positions never combine
within one source state. Synthetic x2/x3 renders continue to interpolate only
completed exact snapshots and consume an owned safe-cut marker without running
collision or mutating gameplay-camera history.

Implementation commit is `ba3df97`. Build/dist/installed x86 SHA-256 is
`08E5C1CADFF419A74F576BE2C0FE442F211BC4534EC3DF21801A8E1046B3CF29`.

## 0.0.182: volume exit and sustained release evidence

The first live-owned log separates publication correctness from collision
response. All sampled live transactions are exact, but initial overlap in the
dynamic triangle channel can still report a zero-distance obstruction when the
player-side pivot is inside a block's 96-unit shell. That is a topological
exit, not a valid inward spring endpoint. The owned channel now removes only
the interval required to leave the complete mesh's expanded OBB and resumes
the real triangle sweep after it. A later re-entry is preserved as collision;
a clean exit cannot collapse the camera to the focus or force hybrid ownership.

Collision evidence near a mesh silhouette is not perfectly continuous at
20 Hz. Four clear ticks allowed the radius to expand twice between recurring
contacts, creating a visible contraction sawtooth even though every individual
endpoint was safe. Owned gameplay therefore uses ten clear ticks and eight
monotonic moving-boundary ticks before recovery. This changes temporal
confidence only: immediate inward safety, maximum recovery speed, collision
radius and mesh qualification are unchanged. The frozen hybrid retains the
original generic policy.

The new diagnostic unit is `camera_owned_solution`, which records the complete
decision after angular selection, radial response and revalidation. It makes a
future candidate alternation distinguishable from scene-contact flicker and
from publication failure without reconstructing state from unrelated legacy
logs.

Implementation commit is `7faae22`. Build/dist/installed x86 SHA-256 is
`49ACBB5103FA1F7CC325DD53169C4FEF0D09E79ED704BAE7447B6272DD2EE8AA`.

## 0.0.183: angular avoidance is a latched state

A collision-safe third-person camera must not choose a fresh composition just
because another candidate is longer on the current tick. That turns collision
avoidance into an involuntary camera director. Near a corner, small changes in
the direct ray can make several side, pitch and rear candidates exchange first
place even though the currently published side remains completely usable.

The owned planner now treats a selected non-direct shot as a latched avoidance
state. It retains that candidate until either its combined room+scene distance
falls below the existing three-sphere transition boundary or the direct shot
has remained fully useful for eight consecutive 20 ms source ticks. Loss of
direct clearance resets the release evidence. A collapsed current side may
still select and safety-cut to a verified replacement immediately. Therefore
temporal stability never overrides collision safety.

Camera-relative locomotion must use the rendered view, not the user's
unobstructed request. The final owned transaction can add a substantial yaw
offset, so the raw orbit yaw is no longer a valid screen-space basis. A
successful publication now computes the horizontal yaw from its exact final
focus and target and atomically publishes that as the movement reference. The
next native input sample is consequently aligned with the last completed
visible source pose. No valid horizontal vector means no invented replacement
heading.

## 0.0.186: a collapsed arm remains on the user ray

Moving a collapsed camera to the opposite side of the pivot preserves its
mathematical view-forward vector but violates spatial continuity and creates a
new collision problem. The near-pivot state now keeps the camera on the exact
requested orbit ray and contracts radius through the same spring-arm state,
including a valid zero-length terminal segment. Its independent look-forward
target provides a nonzero orientation at the pivot.

Collision-safe endpoints contribute scalar clearance only. Their rounded
short vectors must not author direction: the published radial point is rebuilt
from the full requested orbit vector. This prevents integer quantization from
turning into visible angular noise as radius approaches zero.

Scripted takeover also has two evidence classes. A verified owner transition
is authoritative only immediately after explicit interaction. A delayed
ownerless reveal still requires sustained independent native travel. An
ordinary fixed-camera owner encountered later in the broad interaction tail
cannot take control.

## 0.0.187: diagnostics cannot amplify collision cost

Runtime telemetry is observational and must not materially change camera frame
pacing. Opening and closing the session file per diagnostic line violates that
boundary: obstruction activates several dense records on the gameplay thread,
so the instrumentation itself can create a contact-correlated frame-rate
drop.

The session logger now owns one lazily opened, synchronized append handle for
the process lifetime. Single-line records and pre-buffered probe blocks share
the same writer. Repeated clean exits from the same expanded mesh interval are
logged on state change and as a bounded heartbeat, not once for every direct
and revalidation query. No collision decision or camera endpoint is changed in
this version.

## 0.0.185: collision cannot select gameplay rotation

Restricting angular avoidance to a physical emergency does not make it
predictable. The v0.0.184 log still shows multi-candidate sequences whenever
the direct arm collapses, and each sequence is visible as an unrequested
camera direction change. Gameplay collision therefore has no angular
candidates in v0.0.185: the runtime candidate list contains only the exact
user orbit.

A direct arm shorter than the nonzero look-at minimum enters a separate
near-pivot presentation state. The camera moves to the horizontal projection
of the same view-forward vector in front of the pivot, retains that exact
forward direction after all collision corrections, and uses the normal full
room-sphere and scene-mesh sweeps. Four consecutive ticks above the larger
exit clearance are required before returning behind the player. Entry and exit
are presentation cuts. Thus collision may change distance and third/near
presentation, but never gameplay yaw, pitch or camera-relative movement basis.

## 0.0.184: collision cannot own the gameplay heading

The v0.0.183 live result rejects latched angular composition as the primary
gameplay response. Latching reduces immediate alternation but cannot make an
automatically selected plus/minus 15--180-degree shot predictable to the
player. Feeding that published offset back into camera-relative locomotion then
lets collision modify both view and movement intent.

The stable control rotation is now the raw user orbit yaw. Collision owns
radius, not heading. The direct combined room+scene shot is accepted down to
the 120-unit physical look-at minimum (and can remain direct inside that through
its established hysteresis). Angular candidates are considered only when the
direct result is effectively unusable; because evaluation is ordered by
angular cost, the first physically usable emergency direction wins rather than
the longest cinematic composition.

Blocked radial recovery no longer requires a farther convex boundary to be
monotonically increasing. That condition confuses changing plane distance with
loss of clearance: a boundary varying between 1200 and 1500 is still sustained
evidence that a 300-unit arm may extend. The owned path now counts consecutive
samples whose safe distance remains beyond the current published radius. A
sample that reaches the current radius resets evidence, and inward contraction
remains immediate. The legacy hybrid policy is unchanged.

## 0.0.188: authored ownership needs convergence and lifecycle bounds

An interaction alone does not authorize a retail view, and an owner pointer
alone is shared by ordinary fixed-camera volumes. A delayed authored takeover
requires their temporal conjunction with the verified rising edge of retail
owner-target convergence (`controller+0x180 & 0x20`). This admits lever scripts
that begin late without turning the broad six-second interaction window into a
fixed-camera permission window.

The lifecycle has two additional bounds. A completed owner identity cannot
replay later in the same process after an unrelated operate press, and a
persistent owner with a quiet endpoint returns control after a bounded 2.6
second tail rather than holding until the emergency timeout. Native owner
release remains authoritative when it arrives normally.

Retail first person is presentation state, not a third-person spring arm. Its
mode-4 matrices use generic quaternion interpolation and never enter the
pivot-relative validation/fallback branch that intentionally constrains the
modern mode-3 orbit.
