# Standalone Windows installer

The release has two independent installation downloads: `*-Setup.exe` for
Windows and the complete ZIP with `INSTALL-DECK.sh` for Steam Deck. The
Windows PowerShell scripts remain in the ZIP as an alternative.

The EXE follows Fighting Force's folder-selection, verification and rollback
flow, but uses native Win32 C++ instead of its .NET implementation. It embeds
the patch, model reconstruction delta, tested Windows graphics wrappers,
configuration, documentation and required licenses/source patches. No original
game files, screenshots, posters or local development resources are included.

The embedded files are LZX-compressed into one CAB. Windows' built-in
[FDI decoder](https://learn.microsoft.com/en-us/windows/win32/api/fdi/nf-fdi-fdicopy)
unpacks the data entirely in memory. The compressed CAB, original payload and
individual files are SHA-256 checked before installation writes begin. Both
compressed and expanded sizes are bounded; no external archive program runs
on the user's machine. The raw file list and installed contents are unchanged.

After a successful installation the window offers **Finish**, with Install
hidden and further installation commands blocked. Errors or cancellation
retain the option to retry. Run the EXE again to perform a later update or
reinstallation.

## Installation behavior

Select the installed directory containing `DD_CD.EXE`. Known Steam and GOG
English/French/German/Italian file hashes are recognized; an unrecognized game
requires explicit confirmation. Close the game before installing. A protected
game directory may require starting the EXE with **Run as administrator**.

The current Native 50 INI values are preserved by default, with newly supported
keys filled from the release defaults. Unchecking the settings box installs
defaults at the primary monitor's current physical resolution. The tested
keyboard profile and required retail rendering configuration are always
installed together. The exact known localized GOG Miles music fix is retained.
The EXE does not replace `DD_CD.EXE`, `Dungeon.dll`, assets or saved games.

All replacement files are prepared and hashed before committing. Previous files
are copied to a unique `back/native50-setup-<timestamp>-<id>` directory, retaining
their relative paths. Failure or cancellation rolls back replaced files and
removes new files. A successful backup contains `RESTORE.txt`; follow it to
restore the prior installation manually. Never delete a backup if setup reports
that rollback could not finish. Power loss/process termination is outside the
automatic rollback guarantee; the backup is retained for recovery.

Symbolic links/junctions in the selected path or modified subdirectories are
refused. File names and sizes in the embedded payload are allowlisted and
verified with SHA-256. This checks corruption, not publisher authenticity; the
EXE is unsigned. Build with MSVC 2022, static runtime and Win7 API/subsystem
targets. No Python, PowerShell, .NET or VC Redistributable is needed on the
end user's machine. Neither installation nor gameplay has been tested on a
real Windows 7 system; patch operation on that OS is not guaranteed.

## Build and checks

`scripts/package-release.ps1` builds both downloads, runs the installer tests,
and writes SHA-256 checksums for both. The EXE is not duplicated inside the ZIP.
An existing reviewed release staging directory can also be used:

```powershell
.\scripts\build-installer.ps1 -PayloadDirectory artifacts\Deathtrap-Native-50-<version> `
  -OutputPath artifacts\Deathtrap-Native-50-<version>-Setup.exe
python scripts/test-native-installer.py artifacts/Deathtrap-Native-50-<version>-Setup.exe
```

The build requires CMake, Python 3.10+, the Windows-supplied `makecab.exe`, and
Visual Studio 2022 C++/Windows SDK.
It uses a separate CMake project without downloading dependencies. The native
core test injects a failure/cancellation after file replacement has begun.
Python tests run the actual EXE against disposable fixtures, check preservation
and backup behavior, and corrupt compressed bytes, digests, size fields,
CAB headers, file names and streams to verify rejection before game writes.
The PE check
rejects a CLR dependency, dynamic VC runtime, CodeView paths or a newer subsystem.

For tests against five legitimate local editions, use
`scripts/test-install-release-matrix.ps1 -ArchivePath <zip> -GameDirectories
<hashtable> -InstallerPath <exe>`. It installs into temporary copies and checks
retail hashes, rollback contents, required profiles and DirectInput loading.
It never modifies the supplied game directories.

Read-only CLI: `--verify-payload`, or `--verify --game-dir "<folder>"`.
Installation: `--install --game-dir "<folder>"`, optionally `--reset-settings`.
Unrecognized games require `--accept-unknown`. `--log "<file>"` writes an
explicit report for automation. No unattended installation is performed when
launched without arguments; it opens the interactive window.
