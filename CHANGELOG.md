# User-facing changelog

## Acknowledgements

Thanks to the community members who helped test public builds:

- `dbdk422a`
- `517342` — for testing and identifying the directional-control problems
  fixed in version 0.0.221.

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
- Keeps the original MP3 files and installed audio system; no soundtrack
  conversion or replacement music package is required.

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
