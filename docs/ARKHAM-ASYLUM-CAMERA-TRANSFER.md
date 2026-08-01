# Arkham Asylum camera study and Deathtrap transfer

Date: 2026-08-01

This document records the evidence, the transferable design and the concrete
Deathtrap implementation boundary. It is deliberately not a claim that the two
games share a camera engine. Batman: Arkham Asylum is an Unreal Engine 3 game;
Deathtrap Dungeon uses a much older room/sector camera with fixed-point
transforms. The useful part is the ownership model, not copied code or magic
constants.

## Evidence and confidence

### Direct evidence from the installed Arkham Asylum GOTY data

The local installation was inspected read-only. `BmGame.u` was decompressed
with Gildor's UE package decompressor and its class metadata was exported with
[UE Explorer](https://github.com/UE-Explorer/UE-Explorer). Rocksteady uses
custom UE3 serialization, so many unrelated classes do not decompile cleanly,
but the complete property/state export of `BmGame.R3rdPersonCamera` is present.
The conclusions below use names and relationships from that actual Asylum
class, not Arkham City or a generic Unreal sample.

The installed `BmGame/Config/DefaultCamera.ini` also directly contains:

```ini
[BmGame.R3rdPersonCamera]
EnableCameraAssist=True
```

The exported class establishes these facts:

- `R3rdPersonCamera` derives from UE3 `Camera` and owns a native
  `UpdateCameraPosition`/`UpdateFreeCam` solve.
- `FreeCameraConfig` contains pivot and zoom offsets, pitch limits, minimum and
  maximum free-camera distance, separate short/long spring constants, default
  pitch and sit-offset shaping.
- camera state contains `ChasePosition`, `ChaseVelocity`, a ten-entry smoothing
  buffer, maximum smoothing velocity, smoothing strength and maximum
  acceleration.
- collision state is separate: `LastCollisionDistSqr`, `MaxCollisionDist`, four
  directional distance samples in `CameraSmoother`, and arrays of nine normal
  plus nine tight-space zoom directions.
- input/assist is separate again: yaw/pitch acceleration and deceleration,
  explicit look-at priority/strength/speed enums, build-up time and the user
  `EnableCameraAssist` option.
- authored view ownership is explicit. Walk, run, combat, stealth, corridor,
  ledge, grapple, grate, weapon and first-person states select their own camera
  configuration and transition time.
- `ResetCamera` clears smoothing count, chase position/velocity and rotation
  speed. Collision can be disabled during a blend and re-enabled with collision
  distance reset.
- close-camera handling can hide clipping objects or nearby pawns rather than
  forcing the physical camera into an unstable alternative orbit.

Examples of state policy:

- Walk selects `WalkCamConfig`, a 1.5-second transition and pelvis sway.
- Run forces camera drag/assist, selects `RunCamConfig`, uses a 0.75-second
  transition and has distinct pelvis offsets/sway.
- Brawl combat selects `CombatWalkCamConfig`, shifts the target upward, forces
  drag and enables combat/head checks.
- transition durations vary by context; camera endpoints are not all fed to
  one universal interpolation latch.

### Corroborating public evidence

- The public PC configuration documentation exposes Asylum's
  `WalkCamConfig`, including pivot/zoom offsets, pitch limits, min/max camera
  distance and short/long spring constants:
  [PCGamingWiki](https://www.pcgamingwiki.com/wiki/Batman:_Arkham_Asylum).
- The PC manual describes free camera rotation and a user-selectable Camera
  Assist option:
  [Square Enix manual](https://support.na.square-enix.com/document/manual/985/Batman_Arkham_Asylum_PC_Manual.pdf).
- Contemporary observation notes the characteristic near-wall response: when
  the orbit reaches a wall, the view closes in behind Batman rather than
  choosing a remote lateral anchor:
  [Game Developer design review](https://www.gamedeveloper.com/design/game-design-review-batman-arkham-asylum).
- Combat uses a wider framing context:
  [Wired review](https://www.wired.com/2009/09/batman-arkham-asylum-review/).

The exact native UE3 formulas are not available in the script export. Any
numerical filter or collision implementation used below is therefore a
Deathtrap-side design inference guided by the verified state layout, not a
claim of source-identical Rocksteady code.

## What makes the Arkham camera coherent

Arkham does not have one smoothing operation that attempts to repair every
kind of camera motion. It separates five responsibilities:

1. A context chooses the desired rig: target offset, distance, pitch limits,
   assistance and transition policy.
2. A chase state follows the character target with bounded velocity and
   acceleration.
3. Manual orbit changes desired yaw/pitch. Camera assist is a separate optional
   influence and cannot silently become collision state.
4. A volume collision solve limits the desired endpoint. Several direction
   samples and tight-space directions provide more information than one ray.
5. Inward obstruction response and outward recovery use collision distance
   history and different spring behavior. An old displayed world point is not
   retained as a second orbit centre.

This gives three important invariants:

- collision pull-in is immediate;
- recovery may be smooth, but only toward a currently validated desired arm;
- exactly one camera state owns the final gameplay position for a frame.

## Why the previous Deathtrap implementation became unplayable

Before this transfer, ordinary mode-3 presentation could be modified by all of
the following:

- persistent yaw/pitch and spring radius;
- native `0x30910` seven-trace visibility and radial clipping;
- render-triangle swept-sphere collision;
- native `0x2F380` room/floor/sector/history placement;
- post-native mesh pushout;
- direct history and matrix commits;
- previous-clear-arm retention and tangent detours;
- a render-only mesh presentation latch;
- a second render-only follow filter and temporal chord guard.

Each layer was locally defensible, but together they allowed translation,
rotation and history to refer to different endpoints. The observed symptoms
follow directly: orbit around an invisible point, camera stuck at a column,
A-B-A contact cycling, sudden camera-to-character collapse and black regions
when an old endpoint re-entered geometry.

## Transfer constraints

Deathtrap does not expose Arkham's capsule sweep, physical-material filters,
animation camera targets or UE3 view-target blend framework. Its verified
primitives are narrower:

- mode-3 dispatcher hook at `0x2F310`;
- native configure/history/orientation at `0x2F380`;
- seven-trace room traversability predicate at `0x30910`;
- focus at `controller+0x264`;
- captured render triangles for visible dynamic/static props absent from BSP;
- 16.67 Hz source simulation with approximately 50 Hz rigid presentation.

Therefore the transfer must preserve native room/floor/sector/orientation
ownership. Replacing the final matrix globally already failed in version
0.0.85. The render mesh phase may publish an exact correction only after a
positive qualified-mesh collision, where native BSP has no equivalent data.

## Implemented Deathtrap architecture

### One source-tick owner

Ordinary gameplay camera position is now produced only in the mode-3 source
tick. Synthetic presentation frames interpolate the accepted rigid camera
transform but do not replace its translation.

The former render-only follow and mesh-latch applications are disabled. The
former previous-arm retention and tangent-detour owner are removed from the
active solve. Their most damaging property was retaining a valid old
world-space endpoint while current yaw/pitch continued changing.

### Chase target before collision

The raw native focus feeds a critically damped `chase_focus` state with bounded
velocity and acceleration. The desired yaw/pitch orbit is constructed around
that chase target. A teleport-sized error resets the chase state instead of
slowly crossing the room.

Only the target is smoothed. A collision result is never smoothed through this
filter. This follows the responsibility split evidenced by Asylum's
`ChasePosition`, `ChaseVelocity`, smoothing buffer and separate collision
distance fields.

### Single collision decision

For every complete desired arm:

1. Query native room traversability.
2. If blocked, shorten the exact current ray to the nearest native-safe point.
3. Sweep the same complete arm as a 96-unit camera sphere against qualified
   render meshes; thin levers remain nonblocking by intrinsic two-axis size.
4. Select the nearer current-arm result.
5. Contract immediately. Recover outward only through the bounded spring-arm
   distance state after repeated clear evidence.
6. Submit once through native `0x2F380` so rooms, floors, sectors and rotation
   remain native.
7. Sweep the actual published native result. Only a positive remaining mesh
   intersection authorizes the existing contact-only exact correction.

An initial expanded-OBB overlap remains the one non-radial exception because a
ray prefix cannot escape when its origin is already contained. It is recomputed
from the current requested direction; it is not latched as another orbit
centre.

### Authored cameras and first person

Explicit authored reveal ownership remains outside the ordinary camera solve.
It suspends the chase/orbit state and resumes it afterward. Native first-person
selection is likewise a separate view state. This mirrors Arkham's explicit
view-target/state ownership and avoids treating cutscenes as collision noise.

## Invariants and test obligations

Automated state tests cover:

- bounded chase progress and invalid-timing fail-closed behavior;
- immediate spring contraction;
- delayed, bounded outward recovery;
- floor guard behavior;
- thin-prop classification;
- overlap escape geometry.

Runtime acceptance requires a short user-run matrix because only the game can
exercise native rooms and live render meshes:

1. run straight without mouse input;
2. orbit while stationary in open space;
3. orbit while running beside a wall;
4. circle one static column and one dynamic block;
5. press against a wall so the camera contracts, then leave and verify smooth
   distance recovery;
6. use a lever and confirm authored reveal then ordinary-camera return;
7. cross a room/scene transition and verify no black exact frame;
8. verify mouse and right stick, including first-person toggle.

Any failure must be diagnosed against the single-source log stages
(`camera_orbit`, `camera_native_spring`, `camera_mesh_sweep`,
`camera_post_native_mesh`). Reintroducing a render latch, a carried displayed
point or a second final-position smoother would violate this design.
