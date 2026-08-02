# Camera stability audit (2026-08-01)

Status: read-only analysis of the installed `0.0.129` runtime. No camera code,
installed DLL, dgVoodoo file or game configuration was changed during this
audit.

Source runtime log:

`deathtrap-native-20260801-115037-487-pid23244.log`

The run contains 1,935 source-camera probes, or approximately 116 seconds at
the native 60 ms source period.

## Conclusion

The remaining shaking, sticking and short freezes do not have one collision
threshold as their common cause. The current path contains independent
oscillation sources at five different stages:

1. followed-pivot integration;
2. native placement/contact branch selection;
3. scene-mesh contact selection and spring recovery;
4. synthetic-frame camera interpolation;
5. D3D presentation validation and synchronous diagnostics.

Collision tuning alone cannot stabilize this topology. The deterministic
follow integrator must be corrected before mesh-contact and presentation
timing changes are meaningful. The proposed second persistent position-
history owner was subsequently tested and rejected by `0.0.131`; the
correction is recorded below rather than hidden by later results.

## Proven sources

### 1. The chase integrator alternates for a fixed target

`StepCameraChase` is described as critically damped, but uses a
semi-implicit Euler step. Runtime parameters are:

- source step: 0.060 seconds;
- response: 2.0 Hz;
- maximum speed: 9,000;
- maximum acceleration: 50,000.

For `omega = 2*pi*2` and `dt = 0.060`, the discrete system has approximate
eigenvalues `+0.6755` and `-0.7520`. The negative eigenvalue makes the error
alternate on successive source ticks. A one-dimensional step from 0 to 100
produces positions like:

`56.849, 52.503, 81.712, 77.271, 92.448, 89.032, ...`

The velocity changes sign even though the target does not. This is a
guaranteed open-space source-tick oscillation, not a collision artifact.

Acceleration and velocity are also clamped by their combined 3D length.
Horizontal turning can therefore consume the vertical acceleration budget and
make height correction arrive later as a visible vertical catch-up.

The existing test checks only that the first result is bounded and that invalid
timing is rejected. It does not assert monotonic convergence, absence of
overshoot or absence of alternating velocity.

### 2. Correction: the tested retail ring is reset, not persistent

The custom orbit is built around `chase_focus`, collision is resolved, and the
result is submitted to retail camera configure `0x2F380`. The original audit
attributed the submitted-to-published difference to a persistent absolute
position-history ring.

From 948 `camera_native_mesh_pushout` records, the distance between submitted
and published positions was:

- median: 192 units;
- p90: 420;
- p95: 500;
- p99: 718.

The important control set is the 391 records where both the native world and
scene-mesh path were clear:

- median: 219.4 units;
- p90: 455.3;
- p95: 528.1;
- p99: 813.9;
- maximum: 3,010.3.

Version `0.0.131` tested that interpretation by pre-seeding the cached average
and four samples before every `0x2F380` call. All 433 writes succeeded, but the
177 clear-flag submitted-to-published samples had median 177.9 and p95 431.9,
not an improvement over `0.0.130` median 148.5 and p95 429.9.

Static reinspection explains why. `0x2F380 -> 0x2F340 -> 0x2DC40 -> 0x2DC80`
calls `0x2DE10` on controller `+0x204` and resets the ring's current/oldest
pointers before the one current native placement is inserted at `0x2EFFF`.
Prewritten inactive samples cannot affect publication. The clear flags also
describe the overlay's pre-native radial/mesh queries, not an identity mapping
through retail room and sector placement. The measured difference is real,
but it does not prove a second persistent smoother. Phase 2 is withdrawn and
the no-op seed is removed in `0.0.132`.

### 3. Mesh depenetration has no persistent supporting face

Near-pivot and initial-overlap escape selects an expanded OBB exit every source
tick, but does not retain a contact identity consisting of resource, face/axis
and normal. Near a corner the shortest exit can switch between axes on adjacent
ticks.

The run contains a direct sequence in which resource 12614 first selects
`axis=0`, the next near-pivot escape selects `axis=2`, and the following clear
radial request is overwritten by retail history with the preceding escape.
Source camera positions around ticks 623-640 alternate by hundreds of units.
This is the observed A/B cycle and the apparent orbit around an invisible
centre.

### 4. Spring recovery turns contact flicker into a sawtooth

The spring contracts immediately but releases only after clear/contact delay,
then expands by at most 64 units per source tick. The latest log contains:

- 939 outward changes;
- 455 changes approximately equal to `+64`;
- 98 inward changes, median 82.5 and maximum 1,612.9;
- 24 large direction reversals in the spring event stream.

When contact classification or the selected OBB face flickers, the resulting
sequence is a bounded outward step followed by a large immediate contraction.
Spring timing cannot remove the underlying face switch, but visibly amplifies
it.

### 5. Temporal chord protection creates a translation step

The exact source endpoints can both be safe while their straight Cartesian
interpolation chord crosses a prop. The current guard handles this by using the
complete previous endpoint for phase 1/3 and the complete current endpoint for
phase 2/3; only rotation remains interpolated.

In the run:

- 204 guard records represent 102 unique source ticks;
- the longest consecutive guarded interval is 12 source ticks, approximately
  0.72 seconds.

This is geometrically conservative but changes one source-tick motion into an
explicit previous-to-current translation step.

### 6. The D3D guard rejects a separate set of frames

The D3D guard rejected 289 midpoint Presents across 111 unique source ticks.
Only 25 of those ticks intersect the 102 temporal-chord ticks:

- D3D-only ticks: 86;
- chord-only ticks: 77;
- intersection: 25.

The two guards therefore detect different failures and cannot be treated as a
single collision correction.

On every Present the sampler issues 24 by 14, or 336,
`CopySubresourceRegion` calls, copies to a staging resource and immediately
maps it for CPU readback. At 50 FPS this is approximately 16,800 small GPU copy
commands and 50 synchronous readbacks per second. A rejected midpoint suppresses
that Present, producing held-frame cadence in addition to the camera error.

The D3D guard is a last-resort corruption detector, not a camera solver. It
must not remain the normal mechanism that hides unsafe synthetic positions.

### 7. Enabled diagnostics add synchronous I/O jitter

`AppendNativeLog` opens, writes and closes the session log for every ordinary
line. The camera probe buffers records but flushes every 16 source ticks or at
24 KiB. Both installed and repository presets currently enable `DebugLog=1`
and `CameraProbe=1`.

This does not explain geometric A/B camera positions, but it can add irregular
main-thread stalls and exaggerate presentation judder. Normal release operation
must use buffered anomaly telemetry with dense probes disabled.

## High-risk but not yet proven primary

`HookMode3Camera` executes the complete retail mode-3 callback on every source
tick to inspect authored-camera arbitration, then restores only a controller
subrange. Controller mode changed 33 times in the run, but those transitions do
not correlate strongly enough with general jitter to call this the primary
cause. The callback can nevertheless mutate owner fields, external nodes or
globals outside the saved range.

The full retail probe should ultimately run only inside a short explicit
operate/authored-camera arbitration window. Ordinary gameplay should have one
camera rig owner.

## Quantitative source-tick symptoms

For 1,127 adjacent samples with player movement above 8 units and zero
right-stick input, camera motion relative to the player was:

- median: 41.7 units per source tick;
- p90: 163.2;
- p95: 223.1;
- p99: 743.1;
- maximum: 3,049.9.

There were 208 adjacent running samples where relative camera deltas above 20
units pointed in opposite directions. The log therefore confirms source-tick
oscillation even before synthetic interpolation and Present rejection are
considered.

## Ordered implementation plan

### Phase 0: compact observability, no camera behaviour change

- Record one structured source-tick state containing raw focus, chase focus and
  velocity, desired endpoint, native safe radius, mesh resource/axis/contact,
  submitted position, published position and active correction flags.
- Buffer records and flush by anomaly or session close instead of opening the
  file per line.
- Keep one log file per launch in `logs`.
- Disable dense camera probes in the normal installed preset.

### Phase 1: replace the unstable chase mathematics

Implementation status: completed and runtime-validated in `0.0.130`. The
selected formulation is a backward-Euler critically damped solve with
independent per-axis speed and acceleration limits.

Against the final `0.0.129` run, ordinary adjacent source samples with player
motion from 8 to 150 units and no right-stick input changed from median
relative motion 41.0 to 3.0, p95 222.3 to 87.6, p99 701.6 to 226.7 and 173 to
30 opposite-direction deltas above 20 units. Unique temporal-chord ticks fell
from 102 to four without a presentation-code change.

- Replace Euler integration with a stable analytic critically damped update or
  a monotonic exponential formulation valid at a 60 ms source step.
- Separate horizontal and vertical response/clamps so turning cannot steal the
  height acceleration budget.
- Add deterministic tests for a fixed step target, constant-velocity target,
  180-degree turn, vertical step and invalid timing.
- Require monotonic convergence with no overshoot or alternating error for a
  fixed target.

### Phase 2: establish one source-tick translation owner

Implementation status: hypothesis rejected by `0.0.131`. The ring is reset
inside `0x2F380` before insertion, so there is no persistent sample history to
disable at this boundary. The seed is removed in `0.0.132`.

- Preserve retail room, sector, floor and orientation validation.
- Do not treat pre-native clear flags as proof that retail room/sector output
  must equal the submitted point.
- Reinitialize chase velocity and publication history coherently only on true
  camera owner/mode transitions.
- Instrument same-stage native candidates before assigning any remaining lag
  to a persistent smoother.

### Phase 3: make collision contact persistent but pivot-relative

Implementation status: first isolated supporting-axis step implemented and
runtime-tested in `0.0.132`. Several stationary resources retained their axis
across almost all exceptional contacts, but the complete run showed no global
stability improvement. In the `0.0.131` trace unidentified
render resource `10017` changes supporting axis nine times across 21
contained/near-pivot escapes while its node bounds change between samples.
The log does not identify the gameplay object type. The retained state
contains node/resource/axis only; the endpoint is recomputed from current
pivot, current ray and current OBB every tick.

- Store resource/node, supporting face/axis, normal and safe radius; do not
  retain a second absolute world-space camera target.
- Retain the supporting face for the same resource until it becomes invalid or
  an alternative improves clearance by a defined hysteresis margin.
- Re-evaluate dynamic-object transforms every source tick while retaining
  contact identity.
- Keep thin levers nonblocking.
- Test a static corner, moving block, initial overlap, pivot-inside-block and
  leave/re-enter sequences without adjacent-tick face flapping.

### Phase 4: make spring recovery contact-aware

Implementation status: the pre-native portion was implemented and runtime-
tested in `0.0.133`. The trace exposed a remaining cross-stage loop: at its
end the spring repeatedly expanded from radius 580.1 to 644.1 and 708.1;
retail placement then exposed scene-mesh resource 12613 and the post-native
pass contracted it back to 580.1. The cycle repeated every four source ticks.
The pre-native blocker key was therefore insufficient.

Version `0.0.134` adds the missing post-native feedback gate, pending runtime
validation. It retains only a verified scalar radius. A bounded larger radius
is configured as an internal probe and advances the ceiling only when the
resulting retail placement passes the post-native mesh sweep. A rejected probe
is contracted before publication and cannot create a visible expand/contract
cycle.

The `0.0.134` runtime validates ordinary scalar recovery: the gate advanced
from radius 267.9 through bounded verified probes to 1399.5 and released
without returning to the old ceiling. The same trace exposes a separate
contained/near-pivot ownership conflict. For resource 12613 the current OBB
escape was approximately 619 units, while retail history and the post-native
radial branch intermittently published approximately 128 units, producing a
619/128 A/B sequence. Version `0.0.135` makes the already native-validated
current-transform OBB escape authoritative after the one retail configure
call. It retains no endpoint beyond the current source tick.

The `0.0.135` trace confirms seven authoritative escape commits with no
failures and removes the 619/128 extreme. It also reveals a smaller scalar-gate
ownership error: a post-native contact near radius 313 was forgotten whenever
the current native obstruction shortened the arm to about 269. The following
post-native pass rediscovered the same contact, producing repeated ON/OFF
cycles and temporal-chord guards. Version `0.0.136` keeps the scalar gate active
for the full lifetime of any pre-native constraint. It releases only when the
complete desired arm is pre-native clear and has reached its verified radius.

The `0.0.136` runtime closes Phase 4: post-native gate OFF events fall to one
real full-arm release, relative-camera median falls from 57.7 to 13.6 units,
p95 from 347.7 to 204.2 and opposite-direction deltas from 26 to 14. Remaining
guarded samples expose Phase 5 rather than another spring lifetime: exact
endpoints can each be collision-corrected while their Cartesian interpolation
chord crosses resource geometry.

The spring now associates outward evidence with one native/scene-mesh blocker
key and a monotonically improving safe-distance sequence. A blocker change or
clearance regression restarts confirmation; four complete clear source ticks
are required before bounded clear-space recovery begins.

- Preserve immediate inward safety contraction.
- Allow outward recovery only after stable clear classification or monotonic
  improvement against the same supporting contact.
- Reset release state only on a real blocker/contact transition.
- Forbid repeated `+64 -> large contraction` cycles while blocker identity is
  unchanged.

### Phase 5: make synthetic camera phases safe without snapping

Implementation status: the first pivot-relative implementation in `0.0.137`
failed runtime validation. It removed the old Cartesian chord guard, but 341
unique source ticks required the new pivot-ray guard and the D3D detector still
rejected 132 synthetic Presents across 95 unique ticks. Sixty-three rejected
ticks coincided with the pivot-ray guard. At representative ticks 765 and 909,
both synthetic translations were the accepted current endpoint, yet the
orientation was still interpolated and both phases produced new black cells.
Version `0.0.138` therefore makes an unsafe synthetic arm an atomic camera cut:
world and local transforms both come from the accepted current source tick.
Pivot-relative interpolation remains enabled only for a fully validated arm.

The `0.0.138` run proves that this atomic cut is safe but too coarse as the
normal collision response. Of 1,128 guarded phases, 1,098 intersect
unidentified render resource 12613; 1,122 pass the native room query and fail
only the scene-mesh ray. The D3D guard rejects 252 phases on those guarded
ticks because the exact-current view is exposed early rather than traversing a
safe intermediate contact surface. Version `0.0.139` first performs a
stateless native/scene clip of the reconstructed pivot ray and revalidates the
complete result against both authorities. Only an unresolvable phase uses the
complete-current fallback. The clipped endpoint is local to one synthetic
call and does not become contact history or another camera owner.

- Interpolate focus, yaw, pitch and radius in pivot-relative space rather than
  the Cartesian previous-to-current camera chord.
- Validate each synthetic endpoint against current scene geometry.
- If no safe intermediate camera exists, use one deterministic safe camera
  translation for both synthetic phases while actors and UI continue to
  interpolate. Do not split the tick into previous position at 1/3 and current
  position at 2/3.
- Require zero midpoint rejects in ordinary running, wall, block and transition
  scenarios before weakening the final safety detector.

### Phase 6: remove presentation stalls

Implementation status: `0.0.140` adds a behaviour-neutral audit at the
existing synchronous sampler. A rejected midpoint is retained only until the
exact sample of the same source tick. The log records how many cells that
collapsed relative to the preceding exact frame remain black in the following
exact and how many recover. This distinguishes a legitimate view change from
midpoint-only corruption before changing the rejection policy; it issues no
additional GPU copy or map operation.

The `0.0.140` audit proves that the partial-black guard is primarily hiding
real midpoint-only corruption: 3,425 of 4,406 collapsed sample cells (77.7%)
recover in the exact frame of the same tick. It also exposes a missing Phase 5
invariant. Of 696 synthetic mesh clips, 644 resolve below the already defined
120-unit usable camera distance; the long ticks 654--669 sequence repeatedly
contracts a 173--216 unit arm to 4.5--8.1 units. Native point/ray and mesh
queries then report clear even though the camera origin is effectively at the
pivot and the near-plane view turns black. Version `0.0.141` requires the same
`kThirdPersonMinimumCameraDistance` used by the source-tick collision path
before a synthetic clipped endpoint can be rendered. Failure selects the
complete exact-current fallback.

The `0.0.141` run removes all sub-120 synthetic clips and reduces rejected
Presents per source tick from 0.203 to 0.119. The remaining midpoint-only
corruption is not radius-specific: 91.3% of collapsed cells on clipped-camera
ticks and 84.5% on ordinary clear pivot-relative ticks recover in exact, while
only 21.7% recover on exact-current fallback ticks. This identifies stale
camera-dependent render caches rather than another collision surface. Static
analysis of `0x3860` confirms that after its owner-stamp test it invokes the
camera callback, updates the node and bounds, publishes the matrix, then calls
`0x38E80` and `0x3B0C0` to finish camera-dependent caches. Version `0.0.142`
invalidates only that owner stamp after installing each synthetic camera,
runs `0x3860`, renders, restores the exact snapshot, and reruns `0x3860` for
exact state. The same-frame mode-3 guard makes both callbacks no-ops for input,
spring and gameplay state.

Runtime rejects that cache replay. Although every forced `0x3860` call reports
a successful owner-stamp refresh and the known player mutable-field rollback
finds no writes, the player render visibly jitters throughout movement. The
unmapped state mutated by the `0x38E80`/`0x3B0C0` tail is therefore not covered
by the existing synthetic transaction. Version `0.0.143` removes the forced
midpoint and exact cache calls completely and returns presentation behaviour to
`0.0.141`. Replaying `0x3860` is not an admissible Phase 6 solution without a
complete rollback map for its render side effects.

The `0.0.143` rollback run confirms that the visible player jitter disappears
when the forced cache replay is absent. Its same-tick audit also sharpens the
ownership boundary: 163 of 182 midpoint-black cells on clipped synthetic
camera phases recover in exact (89.6%), and 2,767 of 3,277 recover on otherwise
clear pivot-relative phases (84.4%). By contrast, only 2 of 41 cells recover
when the phase already uses the complete current exact camera transform (4.9%);
39 persist because they belong to the legitimate current exact view.

Version `0.0.144` therefore stops producing an arbitrary modern-camera
transform in synthetic presentation phases. It copies the complete current
exact camera world/local transform into both phases while leaving actor,
animation and UI interpolation active. This deliberately gives camera
presentation one exact engine/cache owner per source tick. The synchronous D3D
guard and audit remain enabled for the first runtime validation; the rejected
full `0x3860` replay remains forbidden.

Runtime rejects the global `0.0.144` exact-current policy as well. Combining a
current-tick camera with interpolated actor roots makes the player visibly
shake relative to the view and removes much of the presentation smoothness.
Version `0.0.145` restores the `0.0.143` pivot-relative camera presentation.
The experiment still proves that exact-current is a safe local fallback, but
not a coherent normal phase while the followed actor remains interpolated.

The same run isolates an older, independent distance-recovery cadence defect.
An accepted post-native probe advanced the radius by 64 units and then reset
its clear counter, forcing three stationary source ticks before every next
step; the run emitted 66 such probes. Version `0.0.145` retains the initial
three-tick clearance confirmation, but an accepted fully validated probe keeps
that readiness. Recovery then advances by one validated bounded step on every
source tick; any rejected probe or renewed contact still resets it immediately.

The `0.0.145` runtime accepts both changes. The player no longer shakes and the
user reports smooth pull-back. The log contains five complete recovery
sequences; within each sequence accepted radii advance on consecutive probes
(for example `400.1 -> 464.1 -> 528.1 -> ... -> 1407.6`) and terminate with
`OFF reason=desired_clear`. Two unsafe probes are rejected normally, and no
native spring validation failure or unavailable-query fallback occurs.

This result is scoped to release cadence. The run still contains a separate
resource-`12613` player/flag/native-wall squeeze. The camera publishes one
fixed point for 39 consecutive contacts while requested orbit rotates, and
escape axes 0/2 later exchange ownership with `203 -> 561` and `679 -> 207`
radius jumps. Sixteen axis-2 OBB targets are altered by native wall clipping
before the exceptional commit. This is an outside-supporting-face construction
error, not a failure of the newly continuous scalar gate.

Version `0.0.146` replaces the outside-pivot discrete face point with a
stateless supporting-face slide. It preserves only the current safe-side
constraint and derives tangential progress from current orbit; a continuous
minimum-radius arc covers the zero-tangent singularity. Contained ray exits no
longer accept a later preferred-axis plane. The existing native-wall and mesh
validations remain unchanged, so this experiment isolates contact geometry
from the accepted `0.0.145` release cadence.

Runtime rejects that experiment after increased user-visible jitter. The final
failure is nevertheless independent of the slide: its diagnostics show
`overlap=0`, `pushout=0`, `escape=0` throughout. With stationary focus and
orbit, exact probes repeat four points. Two retail-history points lie roughly
302 units from focus; alternating post-native corrections against resource
`12613` lie at 179--183 units. The scalar ceiling remains short, but a clear
post-native tick performs no exact commit, allowing the already queued longer
raw desired point to become visible on the following presentation.

Version `0.0.147` removes the rejected slide and applies the evidence-backed
pending-history rule from the earlier `0.0.109`/`0.0.110` investigation within
the narrower current scalar-gate architecture. It scans every resolver input
after an apparently clear configure. A pending mesh collision resets release
readiness and is normalized only to a current-focus/current-orbit endpoint at
the verified ceiling after minimum-distance, native-volume and full mesh-arm
validation. This should remove the captured A/B publication conflict without
reviving the absolute-point presentation latch or fixed-face anchor.

The runtime confirms 108/108 normalizations succeed. At the final stationary
contact, however, exact probes remain fixed at a 725.7-unit pre-native escape
while every 1/3 and 2/3 phase clips that same arm to 134.8. The source path
contains the same contradiction: post-native mesh commits 134.8, then the
exceptional pre-native branch commits 725.7 immediately afterwards. This
creates a deterministic short/short/long presentation cycle even though exact
source probes alone appear stable.

Version `0.0.148` rejects exceptional pre-native ownership only when the
post-native pass demonstrably swept the unchanged submitted endpoint and
selected a different result. A post-native collision on a different delayed
history point cannot veto the current escape. The distinction is exact and
provenance-based, preserving the `0.0.135` history fix without adding another
temporal or spatial tolerance.

Runtime records 109 `POST_MESH_CONFLICT` vetoes, yet source probes alternate
489.4 and 267.6 units. Each vetoed tick is followed by a delayed-history tick
which displays the short point, disables the active gate and recommits the
long escape without testing it. `0.0.149` therefore changes stage priority:
the verified scalar gate owns a matching blocker before non-radial escape
selection. It stores no endpoint and keeps the established checked-probe
release path unchanged.

The `0.0.149` runtime rejects further blocker-priority patching as the general
solution. At its final stationary focus, exact camera positions repeat a
four-tick sequence: `-539/509/14670`, then `-585/517/14490` for three ticks.
Both have an approximately 214-unit focus distance, so the cycle is invisible
to scalar-radius state. Every fourth tick is a speculative 278-unit gate probe.
Retail keeps the currently published `-585/517/14490` point mesh-clear, while a
queued desired/history point intersects resource `12613`. The pending-history
branch rejects the probe but reconstructs the 214-unit ceiling on the requested
orbit and commits `-539/509/14670`. That is not rollback: it is a different
Cartesian camera position roughly 186 units away.

### Root invariant: rejected speculation cannot reconstruct publication

The source-tick camera pipeline must treat recovery as a real transaction:

1. `0x2F380` may produce a speculative candidate while preserving the current
   native room/floor/orientation solution.
2. The current publication is swept against scene meshes.
3. Pending desired/history samples are checked before acceptance.
4. If any pending sample rejects the speculative step, controller history may
   be normalized only to the byte-for-byte current native publication which
   just passed the complete mesh sweep. Radius, requested direction, escape
   geometry and an older cached endpoint are forbidden rollback sources.
5. Only a fully accepted candidate may advance the scalar ceiling. Synthetic
   presentation consumes the finalized exact endpoints and never feeds a
   different collision solution back into source state.

This rule is blocker-agnostic and coordinate-agnostic. It also respects the
rejected `0.0.85` boundary: the rollback point is not an independently solved
camera translation but the existing current native result, so native walls,
floors, sector placement and orientation remain authoritative. A current
publication which itself has mesh contact continues through the ordinary
post-native shorter-prefix correction. That exact already-committed safe prefix
is the transaction result; the previous scalar ceiling must not be rebuilt on
the requested orbit in this rejection path either.

The `0.0.150` runtime confirms that scalar reconstruction was one real defect,
but exposes a separate ownership violation in a two-obstacle squeeze. With a
stationary focus and requested orbit, exact publication alternates every source
tick between `-827/400/14655` (339.7 units) and `-648/400/14724` (147.8 units).
The pre-native pass selects a near-pivot escape around resource `12613`; the
resulting publication then intersects resource `11432` in the post-native pass.
On the following delayed-history tick the shorter point is clear, but the code
discards its active gate solely because the pre-native resource key differs and
recommits the longer escape. The next tick clips it again.

Resource identity therefore cannot define collision ownership across pipeline
stages. The two resources are simultaneous members of one constraint set, not
alternative camera modes. Once a post-native ceiling exists, every pre-native
obstruction keeps it active. Only a complete pre-native-clear desired arm plus
successful post-native release validation may retire that constraint.

### 0.0.151 follow-up: full-pose coherence

The final `0.0.151` corner sequence is not another lost pivot or blocker-key
cycle. Player and focus stay fixed and valid, while collision ownership holds
one exact translation for dozens of ticks and the matrix basis continues to
rotate. Large mesh-clear position changes with a stationary player are native
history publications, but the off-player rotation is caused specifically by
combining an exact collision translation with the orientation of a different
publication.

Static analysis maps the retail pose tail as
`0x2E950 -> 0x30790 -> 0x30730`, followed by the ordinary
`0x3A980/0x3AC00` node-matrix update. Version `0.0.152` uses only the verified
`0x30730` leaf when an exceptional exact collision commit is already required.
Position and look-at angles are installed together before the existing cache
tail. It does not replay `0x3860`, invoke another configure pass, or alter
ordinary and authored camera publications.

### 0.0.152 rejection and 0.0.153 pivot-exit transaction

The runtime disproves the `0x30730(position, current_focus)` assumption. Its
retail caller receives a resolver-owned look target and can apply authored
corrections; the direct call therefore replaced orientation with a different
camera contract 542 times without resolving the fixed translation.

The same log proves that the fixed narrow-space point begins earlier. Resource
`12613` repeatedly reports `initial_overlap` because the player-side focus is
inside its 96-unit-expanded conservative OBB. Supporting-axis selection then
maps every rotating request back to one boundary point. This is pivot
containment, not repeated camera-endpoint penetration.

Version `0.0.153` treats containment as a ray interval. It finds the first OBB
exit on the current request, starts the real triangle sweep just beyond that
exit, and keeps any later re-entry as a collision. Exact writes first test the
complete position transaction and become no-ops at an already accepted
boundary. Finally, an outward post-native probe can advance/release its gate
only by the radius actually published and post-mesh verified on that tick;
the submitted radius alone is no longer evidence of release.

### 0.0.154 rejection: publication rollback cannot erase future state

The completed runtime confirms native pose capture and reduces direct A/B
returns, but still contains 30 fixed-publication runs. In the largest
stationary-focus example the desired orbit travels 2,422.6 units while the
publication remains `160/564/15539` for 25 source records. Of 323 exact
commits, 290 are pending-history normalizations and none is idempotent.

The prior rollback invariant conflated two time domains. The current
mesh-clear publication is the correct result for this source tick, but the
new desired/history sample is input to a future native tick. Flattening the
latter into the former recreates the same absolute point after every orbit
input. Version 0.0.155 therefore defers release and preserves the current
publication without rewriting desired, cached average or the four-sample
history. Future samples remain subject to the full post-native sweep when
they become current; full history collapse is reserved for a newly contracted
mesh result.

- Replace the per-Present synchronous 336-cell readback with an asynchronous or
  risk-gated diagnostic path.
- Do not suppress Present as a routine response to camera collision.
- Measure source-tick time, Present time and 1% lows with diagnostics disabled.

### Phase 7: narrow authored-camera arbitration

- Run the complete retail mode-3 candidate only after explicit operate input or
  during a detected owner transition.
- On authored reveal entry and exit, perform a clean camera cut and reset chase
  velocity/history coherently.
- Instrument every ownership transition and require exactly one ordinary
  source-camera owner per tick.

## Runtime test matrix

Each behavioural phase receives one isolated build and a short scenario:

1. straight open run without mouse/right-stick input;
2. repeated open-space 90/180-degree turns;
3. parallel travel along a wall and entry into a corner;
4. approach, orbit and withdrawal from a dynamic block;
5. close pass around a thin lever;
6. stairs, door, elevator and location transition;
7. authored lever camera and return to third person.

No phase should combine chase mathematics, contact topology and presentation
guard changes in one runtime experiment. A failed criterion must identify the
owning stage from the compact source-tick record before the next build.

## Acceptance criteria

- A fixed chase target converges monotonically at the native 60 ms step.
- No unexplained same-stage clear-path translation lag remains after native
  placement and source/publication timing are separated.
- Clear running has no repeated adjacent-tick relative-delta reversals.
- One unchanged blocker cannot alternate contact axes/faces on adjacent ticks.
- Collision contraction is one-way until blocker identity or clearance really
  changes.
- Synthetic phases never alternate previous/current camera translation inside
  one source tick.
- Ordinary scenarios produce no D3D-rejected midpoint Presents.
- Authored camera entry/exit remains correct and ordinary gameplay has one
  camera position owner.
- Normal runtime uses buffered, opt-in diagnostics and does not perform a
  blocking GPU readback on every Present.

## 0.0.155 runtime rejection and 0.0.156 targeted pending repair

The completed 0.0.155 session is
`logs/deathtrap-native-20260801-195359-012-pid10316.log`. It contains 146
pending-history deferrals. Of those, 143 are followed by a scene-mesh commit
within three camera records and 142 on the immediately following source tick.
There are 99 deferred-to-commit translation jumps of at least 20 world units,
47 of at least 64, and 29 of at least 128; the maximum is 387.2. The focus and
requested orbit can remain stationary during these pairs. The native 60 ms
source cadence is consequently presented as an alternating clear/deferred
sequence near 8.3 Hz, which explains the reported increase in visible jerks.

All but one detected pending contacts are the raw desired position
(`position_index=1`); preservation allowed that already-proven unsafe value to
become the next native publication. Version 0.0.156 enumerates the resolved,
desired, average and four ring positions but writes only independently unsafe
future slots. A slot is first clipped along its own current-focus ray, then
validated by the native room-volume predicate, a second full scene sweep and
an endpoint-clear test. A second scene clip may not shorten the accepted value
by more than two integer world units, making the boundary result idempotent.
Validation failure falls back only that slot to the already verified current
publication. Index zero, the visible resolved position, is never written by
this path, and safe average/ring samples are never flattened.

The x86 0.0.156 build, camera state test, DirectInput proxy smoke test and
installer CRLF test pass. The installed/build/dist SHA-256 is
`592047A0871BCC6F3E423C672784DC4FD2B0A6E859D150C17C73ED294D9568C5`.
Runtime acceptance still requires a short reproduction around the same flag,
wall and narrow corner; build-time success is not treated as proof of the
behavioural fix.

## 0.0.156 runtime result and 0.0.157 pre-history ownership

The completed 0.0.156 session is
`logs/deathtrap-native-20260801-201330-561-pid5520.log` with 420 source-camera
records and 152 deferred pending contacts. All 152 targeted writes succeeded,
but every following configure recreated the same unsafe native point and all
152 required a scene-mesh commit. Thus post-history sanitation cannot remove
the internal clear/contact alternation.

It did substantially reduce final-publication amplitude: the number of
defer-to-next-final movements >=20 units fell from 102 in 0.0.155 to three,
and 138 of 152 final positions were held exactly. That is mitigation, not
completion: it converts large oscillation into half-rate holding and occasional
catch-up movement. The final stationary run proves the remaining internal
cycle by alternating initial publications `-451/400/14764` and
`-323/613/14756` while the corrected final publication stays fixed.

Static flow explains the failed ownership assumption. `0x2F380` begins with
`0x2F340 -> 0x2DC40 -> 0x2DC80`, which resets all ring metadata before it
installs and resolves the new request. The final native candidate reaches
`0x2EDC0`, and at `0x2EFFF` calls the generic ring-adder `0x2DE30` with
`controller+0x204`; its returned average is copied directly into the camera
node at `0x2F015`. Version 0.0.157 applies the scene-mesh veto at that insertion
boundary, before the unsafe value can participate in averaging or publication.
The hook is gated by modern-configure thread-local scope and exact ring address.
Every replacement must pass minimum-distance, native-volume, repeat scene-mesh
and endpoint-clear validation; otherwise the unchanged post-native fallback
retains ownership.

The x86 0.0.157 build, camera state test, DirectInput proxy smoke test and
installer CRLF test pass. Installed/build/dist SHA-256 is
`D4FB37A0E3F8A875378451C342DA97CDB79B7BEE85B2DC0A32EB01D0659E4CB0`.

## 0.0.161 runtime and 0.0.162 ownership cleanup

The completed 0.0.161 session is
`<game-directory>\logs\deathtrap-native-20260801-214739-882-pid39536.log`.
Smooth x3 presentation is restored, but the stationary flag/door case still
contains a strict source-tick A/B cycle. With fixed focus `-588/409/14412`,
fixed orbit `-369/812/15735` and zero input, `camera_probe` alternates
`-588/626/14899` and `-588/556/14741` on every tick.

The first point intersects resource 12613. The real swept-sphere solver clips
it to the second point with an eight-unit contact backoff. Pre-history
validation nevertheless rejects that same point because the endpoint test also
treated containment in the resource's conservative expanded OBB as occupied.
Post-native correction uses the real triangles and commits it. Thus two stages
implemented contradictory definitions of the same safe boundary.

The log also records `camera_post_native_history ... targeted=3 held=3
full_history=preserved` on the clear half of every pair. This is the same
post-history sanitizer already disproved by 0.0.156: `0x2F380` reconstructs the
candidate before each insertion, so repairing later desired/ring slots cannot
own creation and only obscures the active A/B transaction.

Version 0.0.162 makes the ownership rules explicit:

1. the current orbit and native room-volume clip produce one submitted arm;
2. the scoped `0x2DE30(controller+0x204, candidate)` hook is the sole
   scene-mesh candidate owner before native averaging;
3. a repeated real-triangle sweep plus direct triangle endpoint clearance
   validates its backed-off boundary; expanded OBB containment is broad-phase
   information only;
4. the retail ring and downstream sector/look-target code retain native
   averaging and pose ownership;
5. post-native scene correction remains only a positive-contact safety net;
6. synthetic presentation reads finalized exact endpoints and cannot write
   source collision state.

The unused render-only mesh latch and source-rate presentation follow, plus the
0.0.156 targeted post-history sanitizer, have been removed. This deletes the
historical alternate translation owners rather than adding another flag-specific
threshold. The x86 build, camera state test, DirectInput proxy smoke test and
installer CRLF test pass. Installed/build/dist SHA-256 is
`4A0121D2C2205E8FA9CE202E7C6B2FEBB517846EFC7D2DE7B5FC02F3E3C26AB3`.

## 0.0.162 runtime: post-native ghost gate

The 22:06:36 v0.0.162 run disproves the remaining post-native radius gate.
The pre-history replacement itself worked: 569 candidates were applied, no
submitted fallback was used, and no old post-history repair ran. Nevertheless,
448 `camera_native_mesh_pushout` records retained `post_gate=1` while all
current evidence was clear (`blocked=0/0`, `mesh=0/0/0`). The longest
continuous ghost ownership series was 215 records. Across the run the stale
gate issued 389 probes, 203 deferrals, 74 regressions and eight rejections, but
only one release.

The feedback path was deterministic:

1. an earlier positive contact activated the post-native ceiling;
2. the gate submitted a 64-unit outward probe;
3. retail's four-position history published a lagged, sometimes shorter point;
4. the gate interpreted that output radius as fresh obstruction evidence and
   lowered its own ceiling;
5. the next probes repeated the same contraction/recovery cycle although both
   current collision passes were clear.

This is source-camera state, so the same defect appears in x1 and x3; frame
interpolation can make it more visible but is not its cause. Version 0.0.163
deletes the post-native gate state, planner, feedback helpers and tests. The
single spring-arm state now consumes only current native/scene sweep evidence.
Post-native mesh correction remains a stateless positive-contact exact commit;
the delayed retail publication is output and cannot become a collision sensor.

The x86 build, spring-arm state test, DirectInput proxy smoke test and installer
CRLF test pass. Installed/build/dist v0.0.163 SHA-256 is
`D9AABAA44A4AB9982C0DD732CF5C13E164CF5938FBDD2169D4769ACE0CEA2E34`.

## 0.0.163 runtime: incompatible multi-object escape

The 22:18:54 v0.0.163 run confirms that the stationary flag gate cycle is
gone, but exposes a distinct current-geometry A/B conflict at the end. With
fixed focus `-9102/-1400/15967` and zero orbit input, resource 13676 selects a
near-pivot OBB escape `-9104/-1400/16708` at radius 741. The current configure
call then detects resource 13679 and selects `-9102/-1400/16096` at radius 129.
The stale-publication exception nevertheless exact-commits the first escape on
the next source tick, producing the observed `741 -> 129 -> 741` loop.

The same trace explains abrupt vertical motion during turns. The requested
orbit height differs from focus height by as much as 960 units, but every
horizontal OBB escape discarded that component and emitted the focus height.
This is a collision-solver discontinuity, not presentation interpolation.

Version 0.0.164 applies two constraint rules:

1. any positive mesh result from the scoped current configure call vetoes a
   pre-native escape when its accepted target differs from that escape;
2. a horizontal OBB escape reapplies requested vertical progress in proportion
   to its horizontal progress, bounded by the requested pitch endpoint.

No temporal smoothing or location-specific resource exclusion is added. The
x86 build, spring-arm test, DirectInput smoke test and installer CRLF test pass.
Installed/build/dist SHA-256 is
`0FC6D6D6F43779116B02B54ED131E06F949E6EC8184A1111BB6DCD1A90724554`.

## 0.0.164 runtime: thin flag and periodic fall reset

The 22:28:36 run confirms that the remaining flag case is admitted by the
classification boundary rather than missed collision geometry. Resource
12613 is approximately `309x1036x309`; the old rule required only two axes at
least one 192-unit camera diameter wide. It therefore participates in 724
mesh-pushout records and the final fixed-focus loop alternates two valid
current-object constraints. Version 0.0.165 changes the general clutter rule
to two axes at least 384 units wide. There is no resource-ID exception.

The fall segment is a separate deterministic follow defect. Probe ticks
52--60 show steady player Y deltas of -400 and repeating camera Y deltas of
about `-785/-165/-249`. Scene contact is absent. A simple reproduction of
`StepCameraChase` plus the caller's `chase_error > 900` branch produces the
same reset on every third target sample. Since `BuildThirdPersonOrbitPosition`
already reinitializes on a real single-tick focus jump over 2500 units, the
900-unit accumulated-lag branch is removed. The implicit integrator now
follows a fast moving target continuously and remains bounded on landing.

The x86 build, camera spring-arm test, DirectInput proxy smoke test and
installer CRLF test pass. Installed/build/dist SHA-256 is
`B5D13CB513B27B6CEE0879D3C39B8580F5F054A875BDCD06A42E73897A57E982`.

## 0.0.165 runtime: four-position history transaction

The completed run is
`<game-directory>\logs\deathtrap-native-20260801-224032-399-pid14152.log`.
The final stationary focus/player remain `-433/400/14725` and
`-433/0/14725`. Camera publication repeats exactly:

1. `-548/414/14768`;
2. `-715/544/14828`;
3. `-523/461/14636`;
4. `-523/458/14804`.

The requested orbit and pre-configure submitted endpoint remain constant. The
scoped history hook repeatedly sees resource 12613 at candidate
`-715/544/14828`, but candidate-derived replacement validation fails and logs
`FALLBACK_POST`. Post-native correction then alternates resources 12613 and
11432. This is a temporal ownership loop, not random triangle noise and not
evidence that thin objects should be globally ignored.

Version 0.0.166 restores the 192-unit obstacle threshold to preserve thin
doors. On candidate validation failure it revalidates the exact submitted
endpoint and uses it only when the full validator returns it unchanged. New
`submitted_check=attempted/valid/unchanged` diagnostics distinguish a genuine
fixed-point rollback from the broad submitted fallback rejected in 0.0.161.

The same run confirms that removal of the periodic fall reset worked, but one
fall-entry reversal remains. The damped chase pivot trails vertical player
motion and expands the effective arm to roughly 2182 units. Version 0.0.166
follows vertical focus directly at source rate and retains damping only on the
horizontal pivot.

The x86 build, spring-arm state test, DirectInput proxy smoke test and installer
CRLF test pass. The installed first v0.0.166 build SHA-256 is
`6BF68DABAA36574228175DE75A2F042E0EF1F68E2EAFF695C20EE3D6140B03AE`.
The latest build/dist adds submitted-validation coordinates and has SHA-256
`0FC4C08438B5021AA244B1245146A2AB83E2AE6B0C344335348D9F23F828C3AF`;
it is not installed while `DD_CD` is running.
Rollback v0.0.165 is
`<game-directory>\back\deathtrap-native50-overlay-20260801-225957`.

## 0.0.166 runtime: idempotence failure

The v0.0.166 trace
`<game-directory>\logs\deathtrap-native-20260801-230007-111-pid41168.log`
contains 143 applied pre-history corrections and 41 exact submitted
fixed-point rollbacks, so the intended transaction is active. The remaining
flag and two-block-corner freezes occur later. Post-native scene collision
repeatedly returns exactly the already published boundary, but the code still
exact-commits it and resets all temporal recovery state. At the final corner,
orbit yaw changes continuously while desired/resolved/published stay fixed at
`-8844/-884/16304`; arm radius stays 9 with zero clear ticks.

Version 0.0.167 makes zero/materially sub-integer post-native corrections
idempotent. A contact at no more than two units of the current publication is
logged without history, spring-state or face-preference mutation and cannot
veto a validated current-orbit escape. Material corrections preserve the
existing exact fail-closed path. Build and all three local verification tests
pass; build/dist SHA-256 is
`9C854ECD5276BE186272015D09C461DD6EDD3A2F7B8047C2A4C5CE48415FA4F7`.

## 0.0.169 runtime: clean-orbit resolver deformation

The v0.0.169 trace
`<game-directory>\logs\deathtrap-native-20260801-235908-214-pid21704.log`
separates the remaining stationary stutter from stick leakage and collision.
On the final bridge circles the filtered input is exactly horizontal, the
focus is fixed, the submitted endpoint is a constant-radius circle and there
are no scene contacts or rejected synthetic phases. The downstream retail
position nevertheless changes height and horizontal radius and reverses yaw
for one tick at repeatable angles.

The safety difference missed by rejected v0.0.158 is in the native predicate:
`0x30910` succeeds when any one of seven `0x4E760` room traces succeeds. Version
0.0.170 uses a separate all-seven validator only to authorize final ownership
of a full-radius, scene-clear modern endpoint. Collision, contraction and
ambiguous ticks remain native-owned. Build and all three local verification
tests pass; installed/build/dist SHA-256 is
`B0A28F1B5D2FE6FB70CD7160A9F200FDE6FEFF74E68CBA48ED0E60BDCD6800E1`.
