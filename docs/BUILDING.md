# Building and packaging

## Supported toolchain

- Visual Studio 2022 Build Tools, MSVC x86 compiler
- Windows SDK 10.0.26100 or compatible
- CMake 3.21+
- PowerShell 5.1+

The output must be Win32. A 64-bit DLL cannot be loaded by `DD_CD.EXE`.

## Automated build

```powershell
.\scripts\build-x86.ps1
```

Optional arguments:

```powershell
.\scripts\build-x86.ps1 -Configuration Debug -BuildDirectory out\debug
```

## Manual build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release --parallel
.\build\Release\dinput_proxy_smoke_test.exe `
  "$PWD\build\Release\dinput.dll"
```

MinHook 1.3.4 is fetched from its official GitHub repository during the first
CMake configure. It is statically linked into `DINPUT.dll`; no MinHook runtime
DLL is required.

## Result

The runtime package consists of only:

```text
DINPUT.dll
deathtrap_native.ini
```

dgVoodoo is installed separately by the user. The verified configuration uses
the x86 `DDraw.dll`, `D3DImm.dll`, `D3D9.dll` and `dgVoodoo.conf`. Version 2.86
is the runtime compatibility baseline; 2.86.2 is the validated reference, not
a hard-coded binary dependency.

## Verification

The build script performs these checks:

1. the DLL is built for x86;
2. the DirectInput export proxy loads;
3. `DirectInputCreateA` returns success and creates an object;
4. the deterministic camera spring-arm and room-collision tests pass;
5. the immersive first-person eye/orientation pose tests pass;
6. all CD tracks `2..16` map to the fifteen Steam MP3 indices `0..14`;
7. the control-binding installer preserves CRLF without producing `CR-CR-LF`;
8. the verified binary is copied to `dist/DINPUT.dll`.

For a diagnostic game run, set `DebugLog=1`. Return it to `0` after validation
because synchronous file logging is intentionally excluded from normal play.
