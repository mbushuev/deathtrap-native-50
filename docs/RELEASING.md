# Publishing GitHub releases

`VERSION` is the repository's single release-version source. The project stays
on the `0.x` line while it remains incomplete; no `1.0.0` release is implied by
this workflow.

## Local release verification

Use a clean worktree and run:

```powershell
.\scripts\package-release.ps1 -OutputDirectory artifacts
```

The script performs the complete x86 build and deterministic test suite,
validates that the DLL metadata, changelog and configuration match `VERSION`,
then creates:

```text
artifacts/Deathtrap-Native-50-<version>.zip
artifacts/Deathtrap-Native-50-<version>-SHA256.txt
```

Inspect the archive and verify its checksum before creating a tag. The release
configuration must keep `Diagnostics/DebugLog=0`.

The ZIP must contain only the runtime DLL and INI in a `payload` directory, the
reviewed optional dgVoodoo user preset and its two setup screenshots in an
`optional` directory, plus the top-level one-click launcher, PowerShell
installer, short user README, user-facing changelog, per-file checksum
manifest, required third-party notices and the MIT licence. Keeping the
payload separate prevents archive extraction from overwriting an installed DLL
before the installer creates its rollback copy. The ZIP must not contain the
repository directory tree or internal development documentation.

## Tagging

Commit the accepted source and release documentation, then create an annotated
tag that exactly matches `v` plus the contents of `VERSION`:

```powershell
$version = (Get-Content -LiteralPath VERSION -Raw).Trim()
git tag -a "v$version" -m "Deathtrap Native 50 $version"
git push origin HEAD
git push origin "v$version"
```

Do not reuse or move a published release tag. Increment `VERSION` for every
new public build, including a rebuilt DLL.

## GitHub Actions result

Pushing the tag starts `.github/workflows/release.yml` on a Windows runner. It:

1. verifies that the tag matches `VERSION`;
2. configures an x86 Visual Studio build;
3. builds the DLL and all test programs;
4. runs DirectInput, camera, first-person, music and installer tests;
5. creates the ZIP and checksum with `scripts/package-release.ps1`;
6. uploads the files as workflow artifacts;
7. creates a **draft** GitHub Release whose description is the current version
   section extracted from `CHANGELOG.md`.

The draft is intentionally not published automatically. Before publishing it,
review the generated notes, attach screenshots or video links, confirm the two
assets and compare the published checksum with the locally verified archive.

## Release contents policy

Release archives contain only this project's DLL, configuration, installer and
documentation. Never add:

- `Dungeon.dll`, `DD_CD.EXE` or other files from the game;
- music, textures, models, levels or other game assets;
- dgVoodoo binaries or any unreviewed/machine-specific configuration; the
  repository's reviewed optional preset is the only permitted exception;
- save files, logs, crash dumps, backups or local filesystem paths;
- a complete user `ASYLUM/keys.cfg`.

dgVoodoo remains a separately downloaded external dependency. The installer
backs up the retail control and rendering files, applies the verified keyboard
profile, preserves mouse/joystick and unrelated bindings, and normalizes only
the required game rendering values. It never modifies the active
`dgVoodoo.conf`.
