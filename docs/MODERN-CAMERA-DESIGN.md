# Modern third-person camera design

## Objective

Add a player-controlled third-person orbit camera to the supported Windows
build of Deathtrap Dungeon without changing simulation timing, collision,
animation, the native 50 Hz renderer, menus, cinematics or first-person mode.

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
item, attacking with a committed animation, or transitioning camera state. A
short R3 press performs an explicit smooth recenter. The existing first-person
toggle can move to a long R3 press so both actions remain available without an
extra button.

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

### Phase B: orbit-only prototype (`0.0.51`)

- Add opt-in yaw/pitch orbit without changing player movement.
- Preserve menus, selector, first-person and scripted cameras.
- Interpolate verified camera endpoints through the existing 50 Hz renderer.

The first implementation hooks `Dungeon.dll+0x2F380`, the mode-3
desired-position entry immediately before the native camera collision and
smoothing chain. It initializes yaw, pitch and radius from the live retail
camera only after the player moves the right stick. Input is consumed once per
real source tick. This phase intentionally keeps tank movement and uses the
retail camera distance as its initial spring-arm length; camera-relative
movement and custom obstruction release remain later phases.

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
