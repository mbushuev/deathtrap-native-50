# User-facing changelog

## Acknowledgements

Thanks to the community members who helped test public builds:

- `dbdk422a`
- `517342` — for testing and identifying the directional-control problems
  fixed in version 0.0.221.

## 0.0.237 — Standalone Windows installer and menu version

Installer-only update to the existing 0.0.237 release; the runtime ZIP and
gameplay DLL are unchanged.

- Finish the Windows installer with a single **Finish** action after success,
  instead of re-enabling Install. Keep retry available after an error or
  cancellation, and block repeat installation commands on the completed screen.
- Compress the standalone Windows installer's embedded files with LZX.
  Decode them in memory through Windows Cabinet APIs, preserving SHA-256
  verification and the exact installed files without an external archive tool.

Changes since 0.0.236. This release also includes all changes from 0.0.235 and
0.0.236, detailed in the sections below.

- Add a separate, self-contained Windows EXE installer with folder selection,
  embedded file verification, Steam/GOG game checks, preserved Native 50
  preferences, timestamped backups and automatic rollback on failure or
  cancellation. It requires neither PowerShell nor .NET. Keep the complete ZIP
  and Steam Deck installation script as a separate download. Windows 7 is an
  installer target. Neither installation nor gameplay has been tested on a
  real Windows 7 system; operation of the patch on Win7 is not guaranteed.
- Make the common Windows installer compatible with Windows PowerShell 2.0
  syntax and built-in command parameters. Fix the first-line
  `SupportsShouldProcess` parser error and the subsequent path, configuration
  reading, constructor and .NET overload incompatibilities. Preserve payload
  verification, backups, WhatIf and the existing Windows installation flow.
  This installer change does not establish Windows 7 gameplay compatibility.
- Display the Native 50 build version above the original game's 1.0 label in
  the main menu, using native menu glyphs and the build's VERSION value.
- Update the Halloween release poster for 0.0.237.

Included from 0.0.236: saved selector slow motion and world-speed settings,
75–150% interface scaling for the full HUD including stamina/status bars,
and E3 wound, injured-face, filtering and burned-hand animation fixes.

Included from 0.0.235: the optional E3 1997 heroine reconstruction, the
localized Native 50 settings book, movement and camera inversion settings,
optional persistent enemy bodies, Show wounds, death recovery and all prior
Windows/Steam Deck improvements. To enable E3, select **Setup → Native 50 →
Heroine model → E3 1997**. Fresh installations select Original; the new EXE
preserves an existing model preference by default.

## 0.0.236 — Interface scaling, selector settings and E3 injury fixes

Changes since the last published release, 0.0.235.

- Add a second page to **Setup → Native 50** with a selector slow-motion
  toggle and selection world speed from 10% to 75% in 5-point steps.
  The default remains 25% world speed; disabling slow motion restores normal
  world speed while retaining the selected percentage for later.
- Add a saved **Interface scale** setting from 75% to 150% in 5-point steps,
  defaulting to 100%. Health/portrait, inventory icons, counters, messages,
  keyboard selectors and controller radial selectors scale together while
  retaining their screen anchors. Full-screen menus and movies keep their layout.
- Scale the running/stamina and temporary status bars too, including their
  fill, background, position and height.
- All three new settings apply immediately, save automatically, and support
  keyboard, mouse and controller input in English, French, German and Italian.
- Rebuild the E3 heroine's partial injuries in the skin and armour materials,
  removing floating wound polygons and their black outlines while retaining
  the accepted body shape, normals, costume and native injury progression.
- Preserve the original wound drawing and brown detail lines. Match exposed
  skin in injured faces to the clean face instead of carrying over pale patches
  from the retail damage charts. Keep the separate full-burn artwork.
- Resample injury colours bilinearly when building E3 skin and armour charts,
  smoothing the blocky resampled wounds and facial details.
- Keep E3 hand appearance consistent with a fully burned torso when a death
  or weapon animation restores a default hand. Cover both hands and all five
  team variants, respecting Show wounds and Original without carrying burn
  appearance into a new life.
- Support the rebuilt injury atlas within the renderer's existing texture-page
  limit while retaining the exact Original model and compatibility with older
  reconstruction packs.

Includes the E3 heroine, settings book, optional persistent bodies, death
recovery and all Windows/Steam Deck improvements introduced in 0.0.235 and
earlier releases. See the 0.0.235 section below for those features and defaults.

## 0.0.235 — Halloween release: E3 heroine and in-game settings

Changes since the last published release, 0.0.234.

- Retains the Windows and Steam Deck improvements from 0.0.234, including
  approximately 50 FPS presentation, widescreen, modern camera and controls,
  controller menus, radial selectors and Deck-tuned vibration.
- Adds a dedicated **Native 50** settings page, opened through an animated
  book on the original Setup table. It uses the game's parchment and fonts,
  supports keyboard, mouse and controller navigation, and follows the game's
  English, French, German or Italian language.
- Adds a saved, immediately applied switch between modern camera-relative
  movement and original tank movement for keyboard and controller. Modern
  movement remains the installation default. Camera X and Y inversion can be
  changed and saved independently on the same page.
- Adds optional persistent enemy bodies. Item drops, encounter progression
  and enemy cleanup still occur at their original times, and retained bodies
  can still be dismembered. Retention is off by default; older bodies may be
  reclaimed to keep graphics memory bounded.
- Adds **Show wounds**, enabled by default. Disabling it immediately hides
  existing and subsequent damage marks on either playable character;
  enabling it again restores the current injury appearance. Health, damage
  and save data are unaffected.
- Includes an optional **E3 1997** heroine reconstruction, selectable in
  Setup → Native 50. Both models are resident, so switching applies immediately
  without restarting the level. Original remains the installation default.
  The reconstruction includes injury variants, modeled chains and a corrected
  torso/pelvis attachment during animation. It is a reconstruction from
  references, rather than a recovered original E3 model file.
- Refines the E3 silhouette and rounded torso surfaces, shoulder attachments
  and hip/leg transitions. Reworks fitted metal caps, their short connectors
  and separate neck/front chains, with seven links across the front.
- Repaints E3 skin details for the final geometry: chest and under-bust
  shading, abdomen and navel, shoulder blades, arms, hands and visible legs.
  Coordinates skin colors and texture placement across body parts; refines
  leather panels, side fastenings, bracers and armor highlights. Preserves
  the model's team and injury variants, including damaged hands.
- Installs E3 as a compact resource delta. Original resources are read directly
  from the installed game; a separate verified cache stores only model changes.
  No complete game archive is copied. Metal chains are shared across injury
  and team variants, with identical vertices reused without changing the model's
  appearance. Original archives are not replaced or included in the download.
- Loads and switches the bundled E3 model under Steam Deck's Proton/Steam
  integration without relying on global Windows file-function detours. Avoids
  a false "E3 model is missing" status when the model and cache are present.
- After death, offers **Load last save** or **Restart level** when a successful
  save or load was recorded in the current level run. Loading a save establishes
  the initial checkpoint; later successful saves replace it. Without an eligible
  checkpoint, the level restarts automatically. The dialog uses the original
  menu presentation and accepts keyboard, mouse and controller input.
- Restores ordinary attacks while the F1–F4 item selectors or controller radial
  selectors are open. Short controller attack presses are retained until the
  next gameplay input tick; selector navigation and slow motion remain active.
- Expands the native render queue and texture-page pool together with their
  allocation, lookup and cleanup boundaries, avoiding the original small limits
  when additional model geometry and injury variants are displayed.
- Extends opt-in diagnostic logs with the graphics adapter, rendering settings,
  and wall-time/CPU-time breakdowns around rendering, presentation and GPU
  readback waits. **ExtendedLogs remains disabled by default.**

## 0.0.234 — Steam Deck, controller camera and localized interface improvements

- Retains the complete Windows and Steam Deck experience from 0.0.233,
  including approximately 50 FPS presentation, widescreen, modern camera,
  controller menus, radial selectors, cautious movement and Deck-tuned vibration.
- Adds localized names for selected weapons, spells and items in the center of
  controller radial selectors. Names follow the game's language, use the
  original yellow gameplay font and wrap long entries to two bounded lines.
  The native icon/blank state determines presence, so melee weapons and other
  available entries are labeled without depending on a quantity display.
- Adds a smooth camera return on `Y` in modern third person: the camera moves
  behind the character, slightly above the head, without turning the character
  or changing the selected distance. Normal collision checks remain active.
  Mouse/right-stick look interrupts the return; menus, selectors, first-person
  views and scripted shots retain their existing input ownership.
- Restores the original accented font glyphs in gameplay messages and menus
  without replacing the game's fonts, text files or other assets.
- Localizes the added keyboard-action labels and the first-person inversion
  caption for the supported French, German and Italian GOG releases, while
  preserving their original keyboard-action labels and custom bindings.
- Adds optional extended support logs for diagnosing timing and rendering
  problems. Change only `ExtendedLogs=0` to `ExtendedLogs=1` under
  `[Diagnostics]`, restart and reproduce the issue; measurements distinguish
  normal play, selector slow motion, render-rate modes, rendering waits and
  observed DXGI submissions. Extended logs and development diagnostics are
  disabled by default; compact per-launch support logs remain automatic.
- Preserves the portable ZIP layout and Steam Deck installation options:
  automatic Steam discovery, installation inside the game folder and an
  explicit custom path containing `DD_CD.EXE`.

## 0.0.233 — Steam Deck and controller movement refinements

**Important: gamepad controls have changed on Steam Deck and Windows.** Spell
casting moves from `RB` to `B`; `LB` is cautious camera-relative movement and
`RB` is native side-steps/cautious forward-backward steps. `B` remains Back in
menus and selectors; other button assignments are unchanged.

- Retains the complete Steam Deck experience: approximately 50 FPS presentation,
  widescreen, modern camera and controls, controller menus and selectors,
  Proton/DXVK rendering and Deck-tuned vibration.
- Separates camera-relative controller turning from walking: small left-stick
  deflections turn the character in place without issuing movement input.
- Raises walk/run entry to 45%/90% of the post-deadzone stick range, with
  release at 40%/85%, so moderate input stays turn-only and running requires
  a deliberate near-full tilt. Cautious-step and combat thresholds are unchanged.
- Eases rising left-stick magnitude and turn strength, with independent
  walk-entry/release thresholds to avoid chatter. Stick release/deceleration
  remains immediate; original walk/run animation timing is unchanged.
- Adds original cautious steps on `LB` with the complete ordinary modern
  camera-relative stick course, including diagonals. Immersive first person
  keeps its full vector; the retail/tank fallback retains the original cardinal
  steps. Cautious mode never emits a run command.
- Keeps controller steps independent of keyboard rebinding and preserves
  directional attacks, jump inputs, selector ownership and menu controls.
- Moves spell casting from `RB` to `B`, keeping menu/selector Back on `B`
  without leaking a cancelled/held press into gameplay casting.
- Adds native cautious actions on `RB`: original `J`/`K` side-step states for
  left/right and `Ctrl` + `W`/`S` steps for forwards/backwards, without turning
  the body or running. Diagonal stick input selects the dominant axis; the
  forward-step animation is not redirected sideways. `LB` retains its full
  camera-relative cautious course, including diagonals.
- Includes the portable ZIP layout and clear incomplete-extraction diagnostics
  from the 0.0.232 packaging hotfix. Steam Deck installation supports automatic
  Steam discovery, installation inside the game directory and custom paths.

## 0.0.232 — GOG localization and launcher stability fixes

- Packaging hotfix: fixes Steam Deck ZIP extraction by storing portable folder
  paths, keeping the installer and its `payload` directory together in Ark and
  Dolphin. An incomplete extraction now produces clear instructions instead of
  looking for files in an unrelated `Downloads/config` directory. Game binaries
  and gameplay are unchanged by this packaging hotfix.
- Restores music in the French, German and Italian GOG releases by safely
  enabling their installed GOG OGG soundtrack backend. The installer verifies
  the exact known `MSS32.DLL`, preserves it in the timestamped rollback copy
  and validates the patched result before completing installation.
- Fixes intermittent GOG Galaxy desktop-shortcut crashes by deferring both
  mouse and keyboard acquisition until the game owns foreground input. Native
  initialization now starts synchronously on the first proxy call instead of
  racing the legacy frontend from a loader-time worker thread.
- Brings launcher-started games to the foreground reliably and keeps the
  borderless game above the Windows taskbar across both intro movies. The
  temporary topmost state is removed immediately on focus loss, preserving
  normal Alt+Tab behavior.
- Corrects keyboard rebinding on French AZERTY layouts. Captured keys remain
  tied to their physical DirectInput positions, conflict detection uses the
  same physical mapping, and the translated key label refreshes immediately
  without leaving and reopening Keyboard Setup.
- Migrates keyboard assignments written by 0.0.231 away from its incorrect
  double layout translation while retaining the standard physical `WASD` /
  `ZQSD` movement cluster.
- Retains the complete Steam Deck support and every gameplay, camera,
  controller, widescreen and presentation improvement from earlier releases.

## 0.0.231 — Steam Deck support and input refinements

- Carries the complete Steam Deck experience forward in the current release:
  SteamOS installation, Proton/DXVK rendering, automatic controls through the
  standard Steam Input **Gamepad** layout, approximately 50 FPS presentation,
  widescreen output and Deck-tuned vibration.
- Makes the standard physical movement cluster work as `WASD` on QWERTY and
  `ZQSD` on French AZERTY while preserving layout-aware custom letter
  bindings. Synthetic keyboard commands from the controller and modern camera
  path are translated at the same boundary, so turning and locomotion cannot
  disagree on localized Windows layouts.
- Keeps camera-relative keyboard steering active while the jump key is held,
  allowing the character to change direction in the air without first
  releasing the key. Gamepad jumping remains unchanged.
- Defers the legacy exclusive mouse acquisition when GOG Galaxy temporarily
  retains foreground focus, then reacquires automatically as soon as the game
  window becomes active. The game no longer reports a fatal DirectInput mouse
  error during this normal launcher transition.
- Normalizes the process working directory to the directory containing
  `DD_CD.EXE`, keeping movies and other relative game files available when the
  game is started through a desktop or launcher shortcut.

## 0.0.230 — Localized GOG compatibility

- Adds dedicated compatibility profiles for the French, German and Italian GOG
  releases, including their localized `Dungeon.dll` code and data layouts. The
  installer now recognizes all four tested GOG language builds.

## 0.0.229 — Full Steam Deck support

- Brings the complete Native 50 experience to Steam Deck, including the modern
  camera and controls, widescreen rendering, controller menus and selectors,
  and every gameplay and presentation improvement already available on
  Windows.
- Adds a SteamOS installer that can automatically locate the purchased Steam
  copy, install directly beside `DD_CD.EXE`, or use an explicit custom game
  directory without changing the game's Steam launch command.
- Supports the standard Steam Input **Gamepad** layout in Gaming Mode. Select
  that layout for comfortable play and Native 50 maps the Deck controls
  automatically; no manual per-button bindings are required. Keyboard-and-
  mouse Steam Input templates are not the recommended Deck configuration.
- Runs the Deathtrap Dd7to9 renderer directly through Proton/DXVK, avoiding
  the dgVoodoo backend that hangs or flashes under Proton while preserving the
  native canvas, widescreen world rendering and HUD mapping.
- Restores the intended approximately 50 FPS presentation cadence on Deck by
  exposing one Present per visible synthetic or exact frame and hiding the
  internal page-chain restore. Clear-on-flip remains enabled to prevent stale
  scene fragments from appearing inside black fog and portal regions.
- Applies a Deck-only 300% final vibration-output gain. Windows retains its
  existing 100% output, and all motor commands saturate safely at the XInput
  maximum.

## 0.0.228 — Camera-relative controls and camera-facing attacks

- Enables camera-relative `W`/`S`/`A`/`D` movement by default in modern third
  person, including correctly normalized diagonals, while retaining native
  running, root motion and collision. Set `CameraRelativeMovement=0` under
  `[KeyboardMouse]` in `deathtrap_native.ini` to restore the original
  character-relative keyboard movement.
- Smoothly turns the character toward the camera heading captured on a
  physical left click, then releases the selected retail attack once facing
  is ready. Quick clicks are preserved, while holding the button does not
  continuously steer the character.
- Keeps directional attack chords, steps, jumps, explicit sidesteps,
  selectors, first-person modes, menus and controller input on their existing
  paths, with focused regression coverage for movement and attack routing.

## 0.0.227 — Firefly and widescreen message fixes

- Fixes the Firefly spell during selector slow motion so its illumination
  effect is applied once and the item is not consumed by a discarded
  presentation pass.
- Corrects the horizontal position of centered pickup names, applied-effect
  notifications and level-script messages during widescreen gameplay.

## 0.0.226 — DPI-safe borderless and windowed modes

- Adds `borderless` and `windowed` presentation modes selectable in
  `deathtrap_native.ini`.
- Detects the primary monitor's physical resolution during installation and
  uses Per-Monitor DPI Awareness V2 to avoid Windows scaling mismatches.
- Keeps borderless presentation out of exclusive fullscreen and provides an
  exact, centered client size for framed-window mode.
- Keeps mouse camera control working in windowed mode.
- Updates the tested dgVoodoo profile to windowed, unforced resolution with
  8x MSAA, and refreshes the setup documentation and screenshots.

## 0.0.225 — widescreen and camera refinements

- Adds automatic widescreen gameplay while preserving the proportions of
  menus, videos and the HUD, including the stamina bar.
- Fixes the black 3D scene on the bundled dgVoodoo rendering path.
- Makes first-person turning and movement smooth during inventory slow motion.
- Improves third-person follow and collision recovery during slow motion;
  camera recovery no longer pauses when mouse input stops.
- Fixes repeated item activation during selector slow motion and separates
  camera input from keyboard bindings and ranged-fire movement.
- Allows installation into any game folder without a Steam manifest or
  Steam-specific directory layout; required game files are checked first.

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
