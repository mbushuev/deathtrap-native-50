# User-facing changelog

## 0.0.199 — changes from the original Steam release

This is a consolidated list of the changes players can see or use. Reverted
experiments and internal development changes are intentionally omitted.

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
- Resolves the animated head on both Red Lotus and Chaindog rather than
  mistaking Red Lotus's braid child for a universal skeleton requirement.
- Restores native keyboard steering immediately after leaving immersive first
  person and uses a conventional physical-mouse vertical look direction.

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
