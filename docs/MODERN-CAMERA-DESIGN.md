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

### Phase C: spring-arm collision

- Add volume sweep, contact margin, immediate pull-in and damped release.
- Validate corners, low ceilings, narrow corridors, doors and elevators.

### Phase D: camera-relative movement

- Locate and hook the native desired-heading/controller path.
- Rotate the player toward camera-relative left-stick input while retaining the
  original motion, collision, animation and speed selection.
- Add soft and explicit recenter only after movement is stable.

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
