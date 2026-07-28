# Deathtrap Native 50 Overlay

Current development version: `0.0.18`.

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
- an opt-in native mouse-to-character-turn and left-click attack layer;
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
- `ModernMouse/HorizontalTurn=1`: map relative horizontal mouse motion to
  the game's native left/right turn actions.
- `ModernMouse/LeftClickAttack=1`: map the left mouse button to the game's
  native primary melee attack action.
- `ModernMouse/RightClickParry=1`: map the right mouse button to the game's
  native parry action.
- `ModernMouse/RepairLegacyBindings=1`: remove the three incorrect mouse
  bindings persisted by development version 0.0.16.
- `Diagnostics/DebugLog=1`: write `deathtrap_native_render.log` and
  `deathtrap_native_present.log`, plus `deathtrap_native_input.log` for the
  mouse test build. Logging is normally off, but enabled in 0.0.18 test config.

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
