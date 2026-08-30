# User-facing changelog

## Acknowledgements

Thanks to the community members who helped test public builds:

- `dbdk422a`
- `517342` — for testing and identifying the directional-control problems
  fixed in version 0.0.221.

## 0.0.224 — remappable controls and selector slow motion

- Expands the original Keyboard Setup screen into two pages covering all
  twenty keyboard actions used by the modern patch, including side-step,
  selectors, both first-person modes, the native-rate toggle and pause.
- Adds working `<<` and `>>` page controls, hides unused rows and keeps the
  existing Keyboard Default control as the single way to restore the complete
  modern default profile.
- Supports assigning `F1` through `F12` in the original key-capture screen and
  redraws changed bindings correctly after page changes and default restore.
- Keeps the fixed XInput layout in patch-owned command space. Changing a
  keyboard binding no longer remaps controller buttons, D-pad actions or menu
  navigation.
- Restores `Start` and `B` navigation on the controller after entering the
  rebinding screen. In the root menu, `Esc`, `Start` or `B` returns to an
  active game instead of selecting Quit; at startup it does nothing, while
  submenus retain their original back behavior.
- Maps `P` and controller `View/Back` to gameplay pause and preserves the
  current music position while paused. Playback resumes from the same point
  instead of falling silent until the menu is reopened.
- Adds Quake-style 25% world speed while any `F1`-`F4` selector or held
  controller radial wheel is open. The slowdown percentage is configurable.
- Keeps selector polling, closing and category switching responsive at the
  full presentation rate instead of delaying keyboard releases to the next
  slowed gameplay tick.
- Keeps third-person and immersive first-person camera input at full speed
  during selector slow motion. Physical mouse axes remain owned by the modern
  camera, so rotating the view no longer turns the character; first person no
  longer collapses to a stuck 4 FPS view.
- Adds deterministic coverage for paged rebinding, command isolation,
  function-key assignments and selector time-dilation state.

## 0.0.223 — safe saving and deterministic control installation

- Enables the retail Save command away from authored save points when the
  living player has remained in a native grounded movement state on a stable
  surface. Airborne movement, scripted camera sequences, death and unstable
  transitions remain ineligible.
- Keeps the original pause menu, save/load screens, slots, screenshots and
  save-file format. There are no experimental quick-save or quick-load keys;
  saving and loading are performed through the retail menu.
- Preserves every original authored save point and its native cost. The new
  safe-position path is free and is used only when the original query rejects
  the current position.
- Verifies the exact native save-query routine before installing the hook. An
  unknown executable layout falls back to the untouched retail save-point
  behavior instead of patching an unverified address.
- Ships the complete tested `keys.cfg` with the release and installs it as one
  unit with the DLL after backing up the previous control file. This prevents
  clean installs and upgrades from retaining incompatible bindings that could
  turn left mouse attack into forward movement or disable directional combat.
- Normalizes and validates the bundled control profile during packaging and
  installation, including the complete keyboard, mouse and original joystick
  expressions required by the modern input layer.
- Adds deterministic safe-save eligibility tests and extends clean-install
  and upgrade tests to compare the installed control profile byte-for-byte
  with the reviewed release payload.

## 0.0.222 — native collision and close-camera transparency

- Keeps pure forward/back movement in immersive first person on Dungeon's
  unmodified W/S locomotion path, including native step-up, airborne movement
  and edge collision. A small longitudinal stick cone absorbs axial noise.
- Retains the custom full-speed vector transaction only for deliberate
  lateral and diagonal movement, preserving the accepted first-person strafe.
- Removes the rejected post-collision vertical camera offset that could place
  the view inside the animated player model at a fully contracted spring arm.
- Adds a stable analytic player capsule built from the live root and
  structurally resolved head instead of animated body/weapon triangles.
- Keeps one exact user-controlled yaw/pitch ray for world collision. The
  player capsule can no longer choose another angle or camera mode.
- Makes the complete player subtree half-transparent when the modern
  third-person camera volume enters it, using Dungeon's own alpha-blended
  render packets and release hysteresis rather than body collision.
- Keeps the complete body, arms and weapon opaque in the custom immersive
  first-person view, retail first person and scripted camera reveals.
- Uses the game's transformed, closed convex collision volumes for moving
  gameplay blocks and gives each such volume ownership over its matching
  render subtree. Static room planes and props without a gameplay volume keep
  their existing collision paths.
- Lets an orbit ray that starts inside a moving volume leave it once, while
  still rejecting an endpoint that remains inside or a later re-entry. Stable
  gameplay-object identity makes repeated boundary results idempotent.
- Keeps third-person collision in third person. Only the explicit F10/SELECT
  and retail Tab/R3 controls may select a first-person camera.
- Preserves user yaw, pitch and radius across large focus teleports instead of
  re-seeding from a delayed retail-camera point; old-room collision history is
  discarded before one validated presentation cut.
- Treats narrow render-only props as visual clutter, so flags, posts and small
  housings no longer contract the camera even when they remain on the orbit
  ray. A hard room or scene obstacle behind them still blocks normally.
- Applies the render-only rule to thin sheets as well as narrow poles. The
  measured flag mesh (`21x1071x676`) no longer becomes a wall merely because
  its height and width are large; no resource ID is special-cased.
- Keeps narrow objects with a real gameplay collision volume provisional: the
  same object must obstruct the orbit for three consecutive source ticks
  before it may contract the spring arm. Room, wall and large-block collision
  remains immediate.
- Applies the same temporal decision to every internal revalidation in a
  source tick, so repeated solver passes cannot turn one visual contact into
  false persistence. Thin persistent obstacles still become authoritative.
- Grows the room-collision sphere over the first 288 units of the spring arm.
  A player pivot inside the final camera margin can now produce a continuous
  positive wall distance around the tangent instead of alternating a full arm
  and a zero-radius cut. The camera centre still cannot cross a room plane.
- Predicts the player's connected room path before narrow door transitions.
  When the future spring arm is shorter, the camera begins approaching in
  bounded source-tick steps instead of waiting for one large doorway snap.
  The current exact room sweep remains an absolute safety limit.
- Keeps the revalidated near endpoint when an intermediate radius stops before
  a portal instead of cutting to a distant candidate for one frame. This
  removes the repeated close/far/close loop seen in narrow connected rooms.
- Drives near-pivot hysteresis from the final revalidated publication radius,
  so its state cannot disagree with the camera that is actually displayed.
- Predicts up to four source ticks of current orbit rotation, capped at 18
  degrees, only while the exact current ray is already constrained. A clear
  orbit can no longer shorten at the same distant wall sector on every turn,
  which removes the repeating rise/fall wave in open rooms. Translation-only
  motion cannot activate this path.
- Integrates right-stick yaw and pitch with Dungeon's fixed 60 ms source tick
  instead of the jittering Windows wall clock. Constant stick input therefore
  produces equal source arcs before native-frame interpolation.
- Removes the remaining stationary-orbit wave without changing persistent
  camera state. For ordinary distant third-person rotation, the raster call
  temporarily rebuilds its basis from the accepted camera-to-focus vector and
  immediately restores the native matrices afterward; movement, collision,
  close views and authored cameras keep their existing owners.
- Holds an already-contracted ordinary third-person radius during continuous
  manual rotation. The camera may still contract immediately at a closer
  wall, but it no longer extends in every clear sector only to snap inward at
  the same wall sector on the next revolution. Distance recovery resumes when
  rotation stops; close-camera escape remains responsive.
- Derives that rotation exclusively from the user-controlled yaw/pitch state.
  Chase-focus lag while running can no longer masquerade as an 18-degree
  orbit turn and repeatedly pull the camera into a nearby wall.
- Restricts angular look-ahead to ordinary third-person distance. A predicted
  future ray cannot speculatively collapse the spring into the near-pivot
  state, and prediction stays disabled until current collision leaves it.
- Applies the same near-pivot guard to player-motion prediction. Running along
  a wall can no longer use a hypothetical future focus to force the current
  camera inside the character while the current ray still has usable room.
- Requires 30 consecutive clear source ticks before a near-pivot spring begins
  recovering. Repeated contact with the same wall every 22--24 ticks now holds
  one close radius instead of pumping between roughly 90 and 450 units.
- Slows only near-pivot recovery from 64 to 16 units per source tick, reducing
  the visible amplitude if a real wall contact returns during the exit.
- Keeps that long near-pivot hold only for passive wall-following. Active
  mouse/right-stick rotation confirms a newly clear current ray in two source
  ticks and recovers at up to 96 units per tick until the camera leaves the
  player volume; renewed wall contact still contracts immediately.
- Drives near-pivot release evidence from current direct clearance rather than
  the already-contracted published radius, preventing the hold from proving
  itself blocked and trapping a rotating camera inside the character.
- Keeps a 48-unit collision cushion while a blocked spring arm recovers. A
  moving or quantized boundary can no longer pull the camera exactly onto
  itself and then contract it again on the following source tick.
- Adds bounded `camera_character_fade`, `camera_character_probe` and
  `camera_native_contact` and `camera_near_pivot` diagnostics, plus a compact
  `camera_soft_obstacle` decision record, automatic large-radius event records,
  a rapid radius-reversal detector and player/camera presentation-coherence
  samples, with deterministic fade, capsule, convex-sweep, spring and
  temporal-gate tests.

## 0.0.221 — directional combat and controller camera

- Corrects only the third-person right-stick vertical convention while
  preserving the accepted horizontal direction and immersive head view.
- Speeds up third-person controller orbit by 40% and disables the old
  cardinal-axis lock, removing the slow, sticky response on diagonal turns.
- Makes left mouse attack immediately while using movement directions as
  modern attack modifiers: `A`/`D` select the side attacks and `S` selects the
  original turning attack. Right mouse remains block/parry.
- Gives `RT` the same behavior on XInput controllers: it attacks immediately,
  while the left-stick sector selects forward, side or turning attacks.
- Resolves the stick continuously through dominant-axis sectors while `RT` is
  held, matching keyboard/mouse chord changes without interrupting the active
  attack.
- Keeps the native joystick override active with centered axes throughout
  combat. This prevents the same physical pad leaking through the original
  DirectInput poll and moving the character while the stick selects an attack.
- Removes the old direct left-mouse `ACTION_ATTACK_1` binding that forced every
  mouse attack to the overhead swing before directional intent was resolved.
- Preserves those physical direction keys while attacking in immersive first
  person so its custom strafe path cannot replace the selected attack.

## 0.0.220 — self-contained dgVoodoo installation

This update removes manual dgVoodoo setup from the normal installation path.
It does not intentionally change camera, input or gameplay behavior.

- Bundles the two unmodified dgVoodoo 2.86.2 x86 runtime files required by
  Deathtrap Dungeon: `DDraw.dll` and `D3DImm.dll`.
- Installs the exact high-quality `dgVoodoo.conf` used during project testing,
  including D3D11 FL11, 3x internal resolution, 8x MSAA, 16x anisotropic
  filtering, automatic mipmaps and VSync.
- Backs up any existing `DDraw.dll`, `D3DImm.dll` and `dgVoodoo.conf` together
  with the patch and retail configuration before replacing them.
- Verifies the bundled runtime hashes during packaging and installation so a
  damaged or substituted wrapper cannot be installed silently.
- Removes the separate dgVoodoo download and control-panel configuration from
  the normal quick-install procedure. Advanced users can still download the
  complete dgVoodoo package separately to tune the installed profile.
- Adds the dgVoodoo attribution, exact bundled version, file hashes and
  redistribution terms to the public third-party notices.

## 0.0.219 — automatic support logs

This update does not intentionally change camera, input or gameplay behavior.
It makes public bug reports self-contained and much easier to investigate.

- Creates one compact support log automatically on every launch; users no
  longer need to edit `deathtrap_native.ini` before reproducing a problem.
- Records the exact patch version, Windows build and display scaling, desktop
  geometry, the effective mouse-related dgVoodoo settings and whether Steam
  Overlay was detected.
- Records DirectInput mouse creation, data format, cooperative mode,
  acquire/unacquire results, camera ownership transitions and one bounded
  mouse/window/cursor summary every five seconds.
- Detects and labels mismatches between the fullscreen client area and the
  legacy cursor clipping rectangle, including a cursor trapped at its edge.
- Keeps heavy camera, render and reverse-engineering telemetry behind the
  existing `DebugLog`, `CameraProbe` and `HeadJointProbe` switches.

## 0.0.218 — installation and Steam launch fixes

This update focuses on making the public package reproduce the tested local
installation. It does not intentionally change camera or gameplay behavior.

### Steam launch compatibility

- Fixes an immediate black flash and process exit when starting the patched
  game through Steam on systems where Steam Overlay is injected.
- Prevents the patch and Steam Overlay from recursively treating each other's
  DirectX presentation hook as the original function.
- Keeps both normal Steam launch and direct `DD_CD.EXE` launch supported. A
  connected gamepad is not required.

### Reliable clean installation

- Applies the verified keyboard, mouse and native joystick control profile on
  a fresh Steam installation instead of assuming that the controls were
  configured during development.
- Backs up both `ASYLUM/keys.cfg` and `ASYLUM/config.dat` before changing them.
- Preserves mouse, joystick and unrelated retail bindings while replacing the
  keyboard actions required by the documented modern layout.
- Applies the verified game-side hardware Direct3D profile: primary D3D
  renderer, mipmapping, 16-bit texture conversion and subtractive shadows.
- Replaces old values, removes duplicate values and adds missing values rather
  than blindly appending another setting.
- Repairs the malformed `RESOLUTION 5RENDERING_PLATFORM ...` line that the
  retail configuration utility can produce.

### Recommended dgVoodoo graphics

- Includes the exact high-quality `dgVoodoo.conf` used for project testing as
  an optional preset. It uses D3D11 FL11, 3x internal resolution, 8x MSAA, 16x
  anisotropic filtering, automatic mipmaps and VSync.
- Includes clear screenshots of the required `General` and `DirectX` tabs.
- Keeps preset application manual. `INSTALL.cmd` validates but never modifies
  the active `dgVoodoo.conf`.
- Documents lighter 2x resolution and 4x MSAA alternatives for systems that
  cannot maintain full performance with the tested preset.

### Release diagnostics

- Ships with `DebugLog=0`. Normal play does not create the large per-session
  development logs; diagnostics remain available when explicitly enabled for
  a requested support run.

## 0.0.217 — changes from the original Steam release

This is a consolidated list of the changes players can see or use. Reverted
experiments and internal development changes are intentionally omitted.

### Distribution and compatibility

- Adds the MIT licence and complete public attribution for the projects used as
  dependencies or engineering references.
- Documents the complete default mouse, keyboard and Xbox-compatible
  controller layout.
- Treats unknown `Dungeon.dll` and `DD_CD.EXE` hashes as compatibility
  warnings rather than refusing installation. The known Steam hashes remain
  documented so untested game builds can be identified in support reports.

### Smoother rendering

- Raises unique rendered output from roughly 16.7 FPS to approximately 50 FPS.
- Keeps gameplay, combat, movement, AI and audio at their original speed.
- Provides a conservative approximately 33 FPS option.
- `F11` toggles the higher render rate at any time for a direct comparison with
  the original presentation.
- Camera and controller improvements remain available when the higher render
  rate is disabled.

### Modern third-person camera

- Adds a freely controlled third-person orbit camera for mouse and right stick.
- Provides configurable horizontal/vertical sensitivity, axis inversion, pitch
  limits and preferred distance.
- Smoothly follows the player and restores its normal distance after passing an
  obstruction.
- Handles walls, floors, ceilings, narrow passages, moving blocks, platforms,
  doors, stairs, levers and other scene objects.
- Pulls closer in confined spaces instead of intentionally changing the
  player's viewing direction.
- Preserves authored camera sequences that show doors, lifts and mechanisms,
  then returns to the player-controlled camera.

### First-person views

- Preserves the original first-person view on keyboard `Tab` and gamepad `R3`.
- Adds a separate body-visible immersive first-person view on keyboard `F10`
  and gamepad `View/Back` (`SELECT`).
- Immersive first person supports walking, running, combat and normal
  interaction.
- Keeps the character's hands, body and equipped weapon visible.
- Attaches the viewpoint to the animated head while leaving look direction
  under player control.
- Supports looking up and down through the full configured 75-degree range
  without dropping back to third person.
- Resolves the distinct animated head geometry of both Red Lotus and Chaindog
  rather than treating Red Lotus's braid and shorter head offset as universal
  skeleton requirements.
- Keeps immersive first person attached during attacks, jumps and other
  animations instead of treating the animated head translation as identity.
- Keeps immersive first person attached when arrows or other temporary
  projectiles remain embedded in the character model.
- Restores native keyboard steering immediately after leaving immersive first
  person and uses a conventional physical-mouse vertical look direction.
- Allows simultaneous forward/back and lateral movement in immersive first
  person on keyboard and gamepad. Pure side movement now uses the normal
  walking/running pace instead of the deliberately slow retail side-step;
  third person, the original Tab/R3 view and LB strafe remain unchanged.

### Mouse and keyboard controls

- Uses physical mouse movement for the modern camera instead of retail tank
  turning during ordinary third-person play.
- Maps left click to primary attack and right click to block/parry.
- Uses the mouse wheel to select the previous or next available melee weapon.
- Adds explicit left and right side-step bindings on `J` and `K`.
- Retains the original keyboard movement, combat and interaction paths.

### XInput gamepad support

- Adds complete Xbox-compatible controller support in gameplay and menus.
- Makes left-stick movement relative to the current camera direction while
  retaining the game's original locomotion, animation and collision handling.
- Provides responsive bounded turning instead of slow tank-style circles.
- Uses the following default gameplay layout:

  | Control | Action |
  |---|---|
  | Left stick | Move; run past the configured threshold |
  | Right stick | Move the camera |
  | `A` | Jump, climb, confirm |
  | `X` | Operate/interact |
  | `RT` | Attack |
  | `LT` | Block/parry |
  | `RB` | Cast/use the contextual spell action |
  | `LB` | Hold for native side-step movement |
  | `Start/Menu` | Open or leave the menu |
  | `R3` | Toggle the original first-person view |
  | `View/Back` | Toggle immersive first person |

- In menus, the right stick controls the pointer, the left stick and D-pad
  navigate, `A` confirms, and `B`/`Start` goes back.
- Supports movie and loading-screen skipping from the controller.
- Prevents camera movement while the right stick is being used by an inventory
  selector or menu.

### Radial inventory selection

- A short D-pad press cycles the next available item in that category.
- Holding a D-pad direction opens a radial selector operated by the right
  stick.
- Maps D-pad up to melee weapons, right to ranged items, down to spells, and
  left to potions/charms.
- Uses the game's own icons, quantities and selection highlights.
- Exposes the PC version's chalk action through the ranged selector.

### Controller vibration

- Adds event-based vibration for weapon swings, confirmed hits, blocking,
  successful parries, spells, ranged shots, healing, landing, player damage,
  heavy damage and death.
- Adds light selection feedback in the radial inventory.
- Stops the motors in menus, after focus loss and when the controller is
  disconnected.

### Music fix

- Fixes the Steam-release bug that can repeat the same background music across
  different levels.
- Restores routing for all fifteen music tracks already shipped with the game.
- Plays the original MP3 files through the built-in Windows media backend,
  while Miles continues to own game sounds and movies. No soundtrack
  conversion or replacement music package is required.
- Preserves the current music position across gameplay pause, fixing the
  Steam Audiere failure that could silence the track until the menu was
  reopened.

### Messages and prompts

- Extends short gameplay messages and level-script prompts to three times their
  original display duration by default.
- Allows the original duration to be restored in the configuration file.

### Compatibility and installation

- Supports the verified 32-bit Steam release of *Deathtrap Dungeon*.
- Does not change save files, level resources, models, textures or soundtrack
  files.
- Installs the overlay DLL and configuration, then adds the required bindings
  to the existing `ASYLUM/keys.cfg` without replacing the whole control file.
- Requires the external x86 dgVoodoo `DDraw.dll`/`D3DImm.dll` D3D11 path;
  `D3D9.dll` is optional for this game and dgVoodoo is not included here.
