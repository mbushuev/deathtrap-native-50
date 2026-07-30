# Deathtrap Native 50 Overlay

Current development version: `0.0.77` (same-tick camera collision commit).

The `modern-third-person-camera` branch contains the first opt-in native orbit
prototype. It takes ownership at the mode-3 dispatcher before the retail
fixed-camera pre-check, then submits its desired position through the original
collision/smoothing pipeline; it never overwrites the final view matrix. See
[Camera reverse-engineering notes](docs/CAMERA-REVERSE-ENGINEERING.md) for the
verified call path and [modern camera design](docs/MODERN-CAMERA-DESIGN.md) for
the safety boundary.

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
- XInput controller 0 is enabled in the test config. The left stick drives the
  original forward/backward and tank-turn actions; hold LB to change its
  horizontal axis to the game's native side-step actions. A jumps/climbs, X
  operates, RT attacks, LT blocks, RB casts, and Start opens the menu. Gameplay
  uses one persistent modern third-person camera; R3 and SELECT no longer
  switch to separate retail/head camera state machines.
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
- `XInput/MovementThresholdPercent=14` restores responsive turning. Running
  engages at `RunThresholdPercent=50` and remains latched until the stick falls
  below `RunReleaseThresholdPercent=30`, preventing brief diagonal/noisy stick
  samples from repeatedly dropping the character back to a walk.
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
- `Diagnostics/DebugLog=1`: write `deathtrap_native_render.log` and
  `deathtrap_native_present.log`. Logging is normally off, but remains enabled
  in the 0.0.41 diagnostic test config.

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
