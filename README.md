# Deathtrap Native 50 Overlay

Current development version: `0.0.198` (the accepted fully owned camera and
Steam music-routing fix plus an independent body-visible immersive
first-person view with corrected look-at orientation and rigid player-focus
attachment, now replaced by a structurally resolved animated head mount whose
eye sits just in front of the face while rotation remains user-controlled;
animated neck translation and the full ±75-degree pitch range no longer drop
the view to third person; normal per-launch logs now omit the two heavy
reverse-engineering probe streams).

The `modern-third-person-camera` branch contains the native modern-camera
implementation. It takes ownership at the mode-3 dispatcher, composes the
verified room graph with qualified dynamic scene meshes, and atomically
publishes one validated camera pose per source tick. See
[Camera reverse-engineering notes](docs/CAMERA-REVERSE-ENGINEERING.md) for the
verified call path and [modern camera design](docs/MODERN-CAMERA-DESIGN.md) for
the safety boundary. The replacement architecture and verified convex
sector/portal layout are recorded in
[full camera ownership](docs/FULL-CAMERA-OWNERSHIP.md).

A native-render-rate modification for the 32-bit Windows release of
*Ian Livingstone's Deathtrap Dungeon*.

The game normally exposes roughly 16.7 unique rendered frames per second. This
overlay preserves the original simulation, collision, animation, input and
audio clocks, but renders two additional clock-isolated geometry phases between
real game endpoints. The resulting presentation rate is approximately 50 FPS
(`16.7 x 3`) without speeding up gameplay.

## Scope

This repository contains only our Deathtrap-specific work:

- the fixed-step native transform interpolation patch;
- the `DINPUT.dll` loader and system-DirectInput forwarder;
- the minimal D3D11/DXGI presentation bridge;
- the corrupt/black native-phase guard;
- native mouse turning, attacks, safe wheel weapon cycling and an experimental
  XInput controller layer;
- corrected Redbook-to-MP3 track routing for the exact Steam audio wrapper;
- configuration, build, verification and installation material.

dgVoodoo and optional presentation launchers remain independent external
components and are not redistributed by this repository.

## Runtime dependencies

- the Steam release (App ID `245010`) running as its original 32-bit process;
- Windows 10 or 11 x64;
- the supported `Dungeon.dll` listed below;
- dgVoodoo 2.86 or newer x86 wrappers configured for D3D11;
- a D3D11-capable GPU and driver.

dgVoodoo is an independent rendering backend and is not included here. The mod
does not patch, rename or redistribute it. Obtain dgVoodoo separately and place
its x86 `DDraw.dll`, `D3DImm.dll` and `D3D9.dll` beside `DD_CD.EXE`. The wrappers
do not need to match a single binary hash: 2.86 is the compatibility baseline,
2.86.2 is the currently validated build, and newer builds may be used.

Configure dgVoodoo itself for `D3D11 feature level 11.0`. In the text config
this is `OutputAPI = d3d11_fl11_0`. This is mandatory: the native surface guard
observes the D3D11 swapchain created by dgVoodoo. Do not use `bestavailable`,
D3D12 or WARP with this build.

## Supported game build

| File | SHA-256 |
|---|---|
| `Dungeon.dll` | `95FE9CE0FFF387F00704548F152E4340815213FCB3833DBE1B5C42871E7D2E56` |
| `DD_CD.EXE` | `0C644A00E62652E046C5DAD2960F0F6C8C1998F4CA065780FBD7811D9908BF1F` |

The patch refuses to activate on an unsupported `Dungeon.dll` image.

## Quick installation

1. Close the game and back up existing wrapper DLLs from its directory.
2. Copy the x86 `DDraw.dll`, `D3DImm.dll` and `D3D9.dll` from a clean dgVoodoo
   2.86+ package beside `DD_CD.EXE`.
3. Configure dgVoodoo to use `D3D11 feature level 11.0` and save its generated
   `dgVoodoo.conf` beside `DD_CD.EXE`.
4. Copy `dist/DINPUT.dll` and `config/deathtrap_native.ini` beside
   `DD_CD.EXE`.
5. Start `DD_CD.EXE` from Steam, directly with the game directory as its
   working directory, or through a launcher targeting that same executable.

Alternatively, run:

```powershell
.\scripts\install.ps1 `
  -GameDirectory "<SteamLibrary>\steamapps\common\Deathtrap Dungeon"
```

`<SteamLibrary>` is a placeholder, not a fixed drive or directory. The installer
accepts any Steam library, validates Steam App ID `245010`, verifies the game
hash, checks the external dgVoodoo installation and accepts dgVoodoo versions
from 2.86 onward. It does not copy or alter dgVoodoo.

See [docs/RUNNING.md](docs/RUNNING.md) for the exact file layout, required
D3D11 settings, first-run verification and troubleshooting.

## Controls and diagnostics

- `F11`: toggle the native render-rate modification.
- `NativeRender/Enabled=0`: disable it before process startup.
- `NativeRender/Subframes=3`: approximately 50 FPS, the recommended mode.
- `NativeRender/Subframes=2`: conservative approximately 33 FPS fallback.
- `Audio/FixMusicTracks=1`: expose all fifteen shipped music tracks and map
  the game's CD tracks `2..16` to `Sounds/0.mp3..Sounds/14.mp3`. The overlay
  validates the exact Steam wrapper before enabling this fix and leaves other
  Miles/GOG audio DLLs untouched. See [music fix](docs/MUSIC-FIX.md).
- `Text/MessageLifetimePercent=300`: keep both ordinary transient messages and
  level-script notifications (for example, missing-key prompts) visible for
  three times the retail duration. Use `100` for the original duration.
- The installer adds native mouse bindings to `ASYLUM/keys.cfg` once, before
  the game starts. The DLL never writes the game's action table; its only
  runtime input hook observes wheel deltas after the system DirectInput call.
- In modern third-person mode, physical relative mouse motion rotates the
  camera on both axes and is not forwarded to the old tank-turn actions.
  Menus and the radial selector keep the original mouse stream. Mouse camera
  ownership no longer depends on an XInput controller being connected.
  Sensitivity is independently configurable per axis under `[Camera]`.
- Left click uses the retail primary attack and right click uses parry.
- Mouse wheel selects the previous or next available close-combat weapon. The
  proxy only observes relative wheel input; `Dungeon.dll` performs the actual
  inventory check and equipment change on the next real gameplay tick.
- `WeaponWheel/Enabled=0`: disable wheel weapon selection.
- `WeaponWheel/Invert=1`: reverse wheel direction.
- XInput controller 0 is enabled in the test config. In third person the left
  stick selects a camera-relative heading. The shared ground-state dispatcher
  applies a bounded turn through the engine's canonical heading writer, while
  native forward locomotion keeps the original animation, speed and collision
  path. Hold LB to use the native side-step actions. A jumps/climbs, X
  operates, RT attacks, LT blocks, RB casts, and Start opens the menu. Gameplay
  uses one persistent modern third-person camera. Tab enters the game's native
  first-person view, and R3 toggles that same Tab-driven mode. The separate
  overlay-owned immersive first-person view is toggled by F10 or gamepad
  SELECT. It remains in mode 3, keeps walking, running, attacks and the full
  character/weapon render, and uses the modern mouse/right-stick look path.
  Its eye stays rigidly attached to the player-focus anchor so looking down
  retains the hands, sword and body in frame without locomotion drift.
  Press F10/SELECT again to return to the preserved third-person orbit; R3
  still enters the untouched retail first-person mode independently.
  `Camera/InvertX=1` and `Camera/InvertY=1` reverse the conventional default
  camera axes. `Camera/PreferredRadius=1400` controls the unobstructed camera
  distance; the native collision resolver may pull it closer near geometry.
- The native orbit camera engages as soon as gameplay becomes valid, whether
  input comes from a mouse, controller, or no controller at all. Inventory
  selection suppresses look input without replacing or resetting the rig.
  The verified native seven-trace room query supplies world clipping;
  obstruction pulls the camera in immediately and two complete clear samples
  begin a bounded spring return. A second read-only swept-volume query uses
  stable per-object render meshes for props such as lever blocks and stairs
  that are absent from the room collision BSP. Every source tick tests the
  complete pivot-to-desired segment, so a contracted arm is never extended
  before collision is known.
  Configure it in
  `[Camera]`; set `ThirdPersonOrbit=0` for exact retail camera behavior.
  Mode-3 input and spring-arm state advance exactly once per unique engine
  frame; repeated cache refreshes and synthetic 50 Hz phases reuse that
  endpoint instead of reintegrating it.
- XInput vibration is enabled by default. LT produces a light low-frequency
  block-action pulse. RT itself never vibrates: attack feedback begins only
  when the engine accepts the melee downstroke, so pressing attack while the
  character remains in block cannot create a false pulse.
  Version 0.0.41 additionally hooks the game's verified damage handler and
  emits distinct envelopes for a confirmed hit, player damage and death. A hit
  is accepted only after target health actually decreases and only within the
  attribution window of a recent controller attack or spell. Version 0.0.42
  also follows the engine's own melee animation damage window;
  its rising edge adds a stronger full-motor downstroke pulse even when the
  weapon misses. This is not a fixed delay from RT. Version 0.0.46 observes
  the engine's accepted block-impact transition at `Dungeon.dll+0x834F0`;
  this path is reached only when an enemy contact hits the player while the
  player is already blocking. Merely pressing LT cannot produce the heavy
  pulse. It also adds a distinct high-frequency pulse only after
  `Dungeon.dll+0x1D210` returns a real offensive spell projectile for the
  player; healing and utility actions are excluded. These engine events are
  queued, and their duration begins only when the XInput owner submits the
  motor command, so a slow frame or diagnostic file write cannot consume the
  pulse before it is felt. The motors stop in menus,
  on focus loss and after controller disconnect. Configure
  `VibrationEnabled`, `VibrationStrengthPercent`, `MeleeSwingVibrationMs`,
  `BlockVibrationMs`,
  `SuccessfulBlockVibrationMs`, `SpellCastVibrationMs`,
  `RangedShotVibrationMs`, `HealingVibrationMs`,
  `SelectorTickVibrationMs`, `LandingVibrationMs`,
  `HeavyDamageVibrationMs`, `HeavyDamageThresholdHp`, `HitVibrationMs`,
  `DamageVibrationMs` and `DeathVibrationMs` in the `[XInput]` section.
  Ranged feedback is emitted only after the retail engine creates a real
  projectile; healing requires a measured health increase; landing requires
  an airborne-to-contact transition with vertical motion; and the heavy
  envelope extends only confirmed player damage at or above the configured
  threshold.
- From process startup onward, the right stick moves the native menu pointer,
  A clicks/confirms and skips movies, the left stick or D-pad emits arrow
  navigation, B/Start goes back, and X is an alternate loading/movie skip.
  `MenuRightStickPixelsPerTick=6` controls pointer speed independently.
- Version 0.0.125 routes the left stick through the retail DirectInput
  joystick poll and `JOY_*` action bindings instead of synthesizing W/A/S/D.
  Third-person movement steers relative to the modern camera at the recurring
  native locomotion callback; native root motion, collision, animation and
  heading writers remain in charge. LB and first person retain the explicit
  side-step layout. Running engages at
  `RunThresholdPercent=50` and remains latched until the stick falls below
  `RunReleaseThresholdPercent=30`.
- Version 0.0.126 replaces the multi-owner ordinary camera with the transfer
  documented in
  [`docs/ARKHAM-ASYLUM-CAMERA-TRANSFER.md`](docs/ARKHAM-ASYLUM-CAMERA-TRANSFER.md).
  The followed pivot now uses bounded chase position/velocity before
  collision. Render-only follow, mesh presentation latching and old-world
  tangent/previous-arm ownership no longer replace the accepted source-tick
  camera transform.
- Version 0.0.127 removes the intermittent `0x7E530` heading-hook/watchdog
  movement path, but its replacement was invalid: the retail horizontal
  joystick action has state-dependent turn semantics and did not converge on
  a held desired course.
- Version 0.0.128 uses the statically verified shared ground-state dispatcher
  at `Dungeon.dll+0x82750`. Native joystick X is neutral, native Y retains
  forward/root-motion ownership, and the dispatcher submits one bounded
  shortest-course delta through the canonical dual heading writer at
  `Dungeon.dll+0x44DD0` before the active state callback.
- Version 0.0.129 changes only the camera-relative longitudinal sign after the
  first `0.0.128` runtime proved that physical stick up/down arrived opposite
  to the camera-space movement basis. Left/right and dispatcher steering are
  unchanged.
- Version 0.0.130 replaces the numerically unstable source-tick chase Euler
  step. The 60 ms update now uses an implicit critically damped solve with
  independent per-axis speed and acceleration limits. A fixed focus converges
  monotonically instead of alternating around its target, and horizontal
  turning cannot consume the vertical response budget. Collision, spring-arm
  timing, native configure history and presentation guards are unchanged for
  this isolated runtime test.
- Version 0.0.131 tested, and rejected, pre-seeding the retail position ring.
  Static follow-up proved that `0x2F380` resets the ring pointers before the
  seeded samples can participate, and runtime submitted-to-published error did
  not improve. Version 0.0.132 removes that no-op experiment completely.
- Version 0.0.132 retains only the supporting OBB axis during a continuous
  contained/near-pivot contact with the same scene object. A competing face
  must improve the current escape by more than one camera radius before it can
  replace the support. The endpoint itself is rebuilt every tick from the
  current pivot, requested ray and current dynamic-object transform, so no old
  world-space point or invisible orbit centre is introduced. Ordinary radial
  contact, native walls/floors, thin levers and authored cameras are unchanged.
- Version 0.0.133 makes spring-arm recovery contact-aware. Outward evidence is
  reset when the native/scene-mesh blocker changes or its measured clearance
  regresses, and four complete clear source ticks are required before the arm
  starts its bounded return.
- Version 0.0.134 closes the post-native feedback loop. When retail camera
  placement exposes a scene-mesh contact that the requested arm could not see,
  recovery retains only the last verified radius. Larger radii are tested as
  internal source-tick probes and become visible only after the post-native
  mesh pass accepts them; no absolute world-space camera point is retained.
- Version 0.0.135 makes a current-transform contained/near-pivot OBB escape the
  final translation owner for that source tick. Retail configure still runs
  once for room metadata, but its history cannot replace the verified escape
  with an older radial point and form a near/far A/B cycle.
- Version 0.0.136 keeps a post-native scalar contact gate active while any
  pre-native obstruction still constrains the arm. Reaching that temporary
  shortened limit is not a contact release; the gate is cleared only after the
  complete desired arm is pre-native clear and post-native verified.
- Version 0.0.137 interpolates modern-camera translation in pivot-relative
  spherical space: focus, shortest-path yaw, pitch and radius. Each synthetic
  pivot-to-camera ray is validated against native room geometry and stable
  scene meshes. If it is unsafe, both synthetic phases use the same current
  safe endpoint instead of an explicit previous-at-1/3/current-at-2/3 step.
- Version 0.0.138 makes that unsafe-arm fallback atomic: both synthetic phases
  use the complete accepted current camera transform, because the 0.0.137 run
  proved that a safe current origin combined with an interpolated orientation
  could still render new black/inside-mesh regions.
- Version 0.0.139 resolves a blocked synthetic pivot ray statelessly against
  native room and stable scene-mesh geometry before falling back. The clipped
  phase is revalidated as a complete pivot-to-camera volume and is never kept
  as another camera target between source ticks.
- Version 0.0.140 leaves camera and Present behaviour unchanged while auditing
  every rejected midpoint against the following exact frame. It records which
  newly black sample cells persist and which recover, allowing the partial-
  black guard to be narrowed from runtime evidence rather than thresholds.
- Version 0.0.141 applies the source camera's existing 120-unit usable-distance
  invariant to phase-local synthetic clipping. A mesh clip that collapses the
  camera at the pivot can no longer pass point/ray validation and render an
  inside-character near-plane view.
- Version 0.0.142 transactionally reruns the retail `0x3860` camera render-
  cache path after installing each modern-camera midpoint and again after
  restoring exact state. Its callback remains source-tick idempotent, while
  camera-dependent matrices and caches now match the transform being rendered.
- Version 0.0.143 removes the rejected 0.0.142 cache replay after runtime
  exposed new player-render jitter. Camera presentation returns to the 0.0.141
  path; the minimum-distance correction and midpoint audit remain active.
- Version 0.0.144 stops synthesizing the modern camera transform. Both
  presentation phases use the complete cache-compatible current exact camera,
  while actor, animation and UI interpolation remain active. The v0.0.143
  audit showed midpoint-only black regions recover in the next exact frame for
  89.6% of clipped samples and 84.4% of clear interpolated samples, but only
  4.9% when the complete exact-current fallback is shown. This is an explicit
  ownership boundary, not another collision threshold.
- Version 0.0.145 rejects that global exact-current presentation policy after
  runtime showed player/camera temporal mismatch and reduced smoothness. It
  restores the v0.0.143 pivot-relative presentation. Separately, a successfully
  validated post-native outward probe now preserves release readiness: after
  the initial three clear source ticks, distance recovers by one checked step
  every source tick instead of pausing three ticks after every step.
  Runtime confirms the player remains stable and camera pull-back is smooth;
  five observed recovery sequences reached the desired radius, while unsafe
  candidates were still rejected.
- Version 0.0.146's outside-face slide is rejected after runtime reported more
  jitter. The captured final cycle never entered that path: with player and
  orbit stationary, exact camera positions repeated a four-tick sequence
  between roughly 302-unit retail-history publications and 179--183-unit
  post-native mesh corrections on resource 12613.
- Version 0.0.147 removes the rejected slide. During an established
  post-native scalar-gate lifetime it also inspects the raw desired point,
  resolved point, cached average and all four native history samples. If the
  current publication is clear but a queued sample already intersects a
  qualified scene mesh, the queue is normalized in the same source tick to a
  freshly rebuilt current-orbit endpoint at the verified scalar radius. That
  endpoint must independently pass minimum-distance, native-volume and full
  scene-mesh-arm checks; no world-space camera point survives into the next
  tick.
- Version 0.0.148 resolves the distinct three-phase loop exposed by the
  0.0.147 runtime. At the end of that trace the exact source camera is fixed
  at radius 725.7, while both synthetic phases independently clip it to 134.8
  against resource 12613. The post-native pass had already tested the exact
  submitted endpoint and selected the short point, but the exceptional
  pre-native escape immediately overwrote it with the long point on every
  source tick. A pre-native escape can no longer own publication when the
  unchanged submitted point was directly disproved by the newer complete
  post-native mesh sweep. Delayed-history publications which differ from the
  submitted point retain the existing exception.
- Version 0.0.149 closes the following source-tick alternation observed at
  stationary resource-12613 contact. The v0.0.148 veto correctly rejected the
  long escape on 109 directly tested ticks, but every intervening tick still
  discarded the active 267.6-unit post-native gate and committed the same
  489.4-unit escape because retail was displaying the preceding short history
  sample. A verified post-native gate now outranks a non-radial escape from the
  same launch-local blocker key. The gate retains only its scalar ceiling and
  releases through the existing fully checked outward probes; a new object or
  changed native-wall topology may still select a new escape immediately.
- Version 0.0.150 makes rejected post-native expansion probes transactional.
  If retail's current publication is mesh-clear but queued history is unsafe,
  controller history is normalized to that exact current native publication.
  If the current publication itself hits a scene mesh, the already committed
  safe prefix of that exact ray remains final. Neither rejection path rebuilds
  an old scalar ceiling on a different requested direction, eliminating the
  blocker-independent same-radius return cycle exposed by the 0.0.149 run.
- Version 0.0.151 treats pre- and post-native contacts as one constraint set.
  The 0.0.150 trace showed a stationary camera alternating every source tick
  between a 339.7-unit near-pivot escape around one object and a 147.8-unit
  radial clip against another. A different resource ID no longer discards an
  active post-native ceiling: only a completely pre-native-clear arm followed
  by successful post-native validation can release it.
- Version 0.0.152 makes every exceptional collision commit a coherent camera
  pose. The retail `Dungeon.dll+0x30730` look-at writer recalculates the camera
  node angles from the committed position and current focus before the normal
  scene-node cache tail builds local/world matrices. This removes the retained
  translation plus unrelated native orientation split without replaying the
  camera cache or changing ordinary native wall/floor and authored-camera
  ownership.
- Version 0.0.153 rejects that direct look-at call because retail supplies it
  a resolver-owned target rather than the modern-camera focus. Pivot
  containment is now a removable interval of the ray inside the expanded OBB;
  the segment after exit is swept again against real triangles. A clear exit
  no longer becomes a permanent supporting-face collision, a later re-entry
  still blocks, repeated exact boundary transactions are idempotent, and the
  post-native recovery ceiling advances only to the radius actually published
  and verified during the current source tick.
- Version 0.0.154 follows the runtime evidence from the 0.0.153 test rather
  than the earlier containment hypothesis. The reproduced fixed-camera runs
  have `overlap=0`: post-native `near_pivot_escape` repeatedly republishes one
  point while the requested orbit moves. An outside pivot now keeps only its
  safe supporting coordinate and takes tangential progress from the current
  orbit, with a continuous 120-unit arc at zero tangent. Exact commits capture
  and reuse the real resolver-owned look target passed to `0x30730` by the
  same `0x2F380` call, so translation and basis describe one pose. Finally, a
  delayed native publication that falls inside the verified scalar ceiling
  resets release evidence; it cannot be followed by the previous immediate
  outward rebound.
- Version 0.0.155 separates a safe current publication from the resolver's
  future position state. The 0.0.154 trace contained 290 pending-history
  normalizations and no idempotent transaction: each rejected queued sample
  flattened desired, average and all four history entries back to the same
  current world point, creating 30 fixed-camera runs while the requested orbit
  kept moving. An unsafe pending sample now holds the already mesh-clear
  publication and resets release readiness for that tick, but future native
  history remains intact. If it becomes the next publication, the ordinary
  post-native mesh pass clips it before presentation. Full history collapse is
  retained only for a real new mesh contraction.
- The 0.0.155 runtime trace showed why preservation alone was insufficient:
  143 of 146 deferred pending contacts became a mesh commit within three
  source records, and 142 did so on the very next tick. Version 0.0.156 repairs
  only the future fields whose own focus-to-position sweep intersects scene
  geometry. Each replacement is clipped on that same ray and revalidated
  against both the native room volume and the complete scene mesh. If it cannot
  be validated, only that affected slot retains the verified current
  publication. The visible position and every safe history entry remain
  untouched, so the resolver neither alternates at half rate nor acquires a
  fixed world-space anchor.
- Version 0.0.157 moves scene-mesh rejection to the native position-ring
  insertion at `Dungeon.dll+0x2DE30`. It is active only inside the overlay's
  explicit modern-camera `0x2F380` transaction and only when the destination
  is this controller's `+0x204` position ring; the other native history rings,
  retail probes and authored cameras remain untouched. The candidate is
  clipped and revalidated before averaging, camera-node translation and
  presentation. The post-native exact correction remains a fail-closed backup.
- D-pad selects the four native inventory categories: up close combat, right
  ranged, down spells, left potions/charms. A short tap cycles the next
  available entry. Holding for 225 ms opens a large radial selector; the right
  stick selects slots 1–8. In the ranged row, slots 1–6 are weapons, slot 7 is
  unused, and slot 8 invokes the PC version's native F2+8 chalk path. The
  game's own renderer supplies each real inventory icon, number, quantity and
  selection highlight. Chalk confirmation calls the same dedicated routine
  as the retail F2+8 entry (`Dungeon.dll+0x458B0`) with the current gameplay
  owner. It does not synthesize C and cannot be repeated by synthetic render
  phases. Ranged
  items, chalk and potions/charms require A while their D-pad direction remains
  held; B or release cancels.
- `XInput/BaseBindings=0` keeps only the category selector and leaves all base
  controller buttons untouched.
- `Diagnostics/DebugLog=1`: write render, input and present diagnostics into
  one timestamped file per process launch under the game's `logs` directory.
  A later launch never appends to an earlier session.
- `Diagnostics/CameraProbe=0` and `HeadJointProbe=0`: normal compact logging.
  Set either to `1` only when a requested reverse-engineering capture needs
  per-tick camera fields or the complete animated head candidate list.

Version 0.0.158 separates gameplay features from optional presentation
multiplication. Modern camera, XInput, selectors, retail first person and
event hooks initialize at native x1 and when `[NativeRender] Enabled=0`; only
synthetic rendering is gated by `Subframes`. The current verified spring-arm
endpoint is the sole positional input to the native mode-3 position ring,
while retail sector and resolver-owned look-target/orientation work remains
downstream. Synthetic x2/x3 camera phases solve collision against their own
interpolated scene transforms instead of advancing early to the future exact
camera pose.

Version 0.0.159 removes that second render-rate camera solver after the
0.0.158 F11 A/B trace proved it was feeding alternating positions back into
the source camera. World and actor transforms still interpolate at x2/x3;
the camera carries the complete preceding exact pose only by the interpolated
focus displacement. It therefore keeps source-rate orbit, orientation and
collision ownership while following the interpolated player. Synthetic
endpoint occupancy may select a complete exact fallback, but never invents a
new orbit radius. The position-ring hook is again a narrow scene-mesh veto:
without positive mesh contact the native wall/room history path is untouched.

Version 0.0.160 rejects the remaining early-camera-cache boundary. At x2/x3
the overlay refreshes scene/actor transforms before midpoint capture, but it
temporarily stamps the camera cache as complete so neither synthetic renderer
can run native camera logic. The original camera-owner stamp is restored just
before the one exact renderer; native mode-3 history and collision therefore
run at the same retail point as x1. The resulting exact camera matrix is then
recaptured for the following interpolation interval. Presentation focus-follow
uses only the native room-volume predicate; the over-conservative phase OBB
endpoint test from 0.0.159 is removed.

Version 0.0.161 restores smooth pivot/yaw/pitch/radius interpolation between
the two exact camera poses and rejects 0.0.160's camera-cache deferral. The
synthetic camera uses only native room-volume clipping; it does not run the
unstable phase mesh/OBB solver. At x1, an exact scene snapshot is captured
after each retail render so the source spring arm retains qualified prop/block
collision history even without synthetic passes. A failed pre-history
mesh-clipped candidate no longer falls back to the unrelated submitted point;
it leaves the native candidate to the post-native safety path instead.

Version 0.0.162 removes the remaining stationary flag/door A/B collision
pipeline. A scene-clipped endpoint is now accepted by its real render-triangle
clearance after the complete swept-sphere validation; conservative expanded
object bounds are no longer treated as final endpoint occupancy. The native
position-ring insertion is therefore the only scene-mesh candidate owner, with
post-native correction retained solely as a fail-closed safety net. The unused
render presentation latch/follow and the experimentally disproved
post-history repair path have been deleted.

Version 0.0.163 removes the second, post-native radius state machine. Runtime
0.0.162 proved that it could remain active for 215 consecutive diagnostic
records while both current native and scene-mesh collision queries were clear.
It treated a delayed four-sample retail publication as a new obstruction,
lowered its own ceiling, probed outward and thereby generated the repeated
inward/outward cycle in both x1 and x3. The retail publication is now output
only, never collision evidence. Current native/scene sweeps feed the single
spring-arm state; post-native mesh correction remains a stateless exact safety
commit for a positive current-tick contact.

Version 0.0.164 resolves the remaining multi-object escape conflict exposed by
the v0.0.163 run. A near-pivot escape from resource 13676 alternated between a
741-unit horizontal OBB target and resource 13679's 129-unit safe target. The
escape was committed whenever retail history published a different point,
even when the scoped current configure call had positively found the second
object. Current pre-history/post-configure contact now vetoes an incompatible
escape. OBB escape also reapplies the requested orbit pitch at matching
horizontal progress instead of flattening the camera to focus height, removing
the 700--960-unit vertical drops seen during pitched turns.

Version 0.0.165 separates narrow scene clutter from camera-scale obstacles.
The former one-diameter rule classified the door flag (approximately
`309x1036x309`) as a wall; qualified props now need two intrinsic axes at
least two camera diameters wide. Walls and large moving blocks still qualify,
while flags, levers and small housings do not steer the spring arm. The same
runtime exposed an unrelated fall cadence: a 900-unit accumulated chase-error
reset snapped the followed pivot to a focus falling 400 units per source tick
every third tick. Real teleports are already handled by the 2500-unit
single-tick focus-jump check, so the accumulated-error reset is removed.

Version 0.0.166 rejects the size-filter shortcut after the v0.0.165 runtime
showed that it did not remove resource 12613 from collision and could also
exclude thin closed doors. The former 192-unit two-axis qualification is
restored. The measured stationary failure is instead a four-position history
transaction: the submitted endpoint is stable, retail creates an intersecting
candidate, candidate-derived validation fails, and post-native correction
alternates between two objects. A failed candidate may now roll back only to
the submitted endpoint after the complete native+scene validator returns that
endpoint unchanged. Diagnostics report the attempted, valid and unchanged
conditions separately. Vertical chase lag is also removed from exact source
ticks so a fast fall cannot inflate the nominal arm; x2/x3 interpolation still
smooths presentation between exact pivots.

Version 0.0.167 fixes a second transaction defect exposed by the v0.0.166
runtime. A post-native scene sweep could return the already published integer
boundary unchanged, yet the code still exact-committed that same coordinate,
rewrote the retail position ring and reset spring-arm release state every
source tick. Orbit input continued to change while the camera remained pinned
to the flag or to one face of a two-block corner. A scene contact whose result
moves the publication by at most the validator's two-unit equality tolerance
is now an idempotent no-op: it remains visible as collision evidence, but it
cannot write history, reset release counters or veto a separately validated
current-orbit escape.

Version 0.0.168 fixes a separate scene-membership defect found by a stationary
360-degree orbit in an otherwise empty room. The retail renderer explicitly
skips a node's own resource when node flag `0x02000000` is set, while still
traversing its children. The camera snapshot previously ignored that flag and
therefore treated hidden/inactive render resources left in the scene tree as
solid invisible obstacles. Collision eligibility now follows that verified
renderer decision in every source and temporal mesh query. Diagnostics record
the exclusion once as `camera_mesh_renderer_skip ... reason=NO_OWN_DRAW`.

Version 0.0.169 separates the remaining stationary-orbit defect from scene
collision. In the measured run, a nominally horizontal full-left right-stick
gesture reports raw XInput `-32768/-11969`; after the ordinary per-axis
deadzone and response curve, the residual Y continues changing pitch until it
reaches the floor-constrained minimum. The right stick now has a configurable
continuous axial lock (`XInput/RightStickAxisLockPercent=25`): minor-axis
leakage inside the cardinal-direction cone is suppressed, while intentional
diagonals transition continuously and retain both axes. Mouse orbit, menus,
radial selection and first-person mouse emulation are unchanged.

Version 0.0.171 makes position, collision footprint and room sector one
coherent camera state. Strict clear ownership now requires all seven native
focus traces plus centre and six cardinal samples around the 96-unit camera
sphere. Exact modern commits resolve the sector belonging to their replacement
translation. Synthetic camera passes temporarily publish the sector belonging
to their interpolated position and restore the exact sector after rendering.
Installed/build/dist SHA-256 is
`4BB179E196E6C668B240835290D56E506B0CA8DD9CDA7FA671DF87781B069F9C`.

Version 0.0.172 makes that complete footprint the collision predicate for the
whole modern spring arm, including binary contraction and synthetic phase
validation. A safe contraction selected by scene geometry is also revalidated
against the full room footprint and scene endpoint after the ordinary native
configure call, then published atomically. The retail fixed-camera tail can no
longer replace one prop-contact endpoint with a different height and radius.
Installed/build/dist SHA-256 is
`9F25C74A6BDDD5435917DC999BB27B6BABE524ADEB93153074AE3428FC197A54`.

## Building

Requirements:

- Visual Studio 2022 Build Tools with Desktop development with C++;
- Windows 10/11 SDK;
- CMake 3.21 or newer;
- internet access during the first configure so CMake can fetch pinned MinHook
  1.3.4.

Run:

```powershell
.\scripts\build-x86.ps1
```

The script configures an explicit Win32 build, compiles the DLL and smoke test,
verifies the DirectInput forwarding path, and updates `dist/DINPUT.dll`.

See [docs/BUILDING.md](docs/BUILDING.md),
[docs/RUNNING.md](docs/RUNNING.md) and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for details. Third-party license
terms for the statically linked build dependency are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
