[CmdletBinding()]
param(
    [string]$OutputDirectory = 'artifacts',
    [string]$BuildDirectory = 'build',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $repoRoot 'VERSION') `
    -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Invalid VERSION value: $version"
}

$expectedChangelogHeading = "## $version "
$changelog = Get-Content -LiteralPath (Join-Path $repoRoot 'CHANGELOG.md') -Raw
if (-not $changelog.Contains($expectedChangelogHeading)) {
    throw "CHANGELOG.md does not contain the $version release heading."
}
$readme = Get-Content -LiteralPath (Join-Path $repoRoot 'README.md') -Raw
if (-not $readme.Contains("Current development version: **$version**")) {
    throw "README.md does not identify version $version."
}

$configPath = Join-Path $repoRoot 'config\deathtrap_native.ini'
$config = Get-Content -LiteralPath $configPath -Raw
if ($config -notmatch '(?m)^DebugLog=0\s*$') {
    throw 'Release configuration must set Diagnostics/DebugLog=0.'
}
$dgVoodooConfigPath = Join-Path $repoRoot 'config\dgVoodoo-recommended.conf'
$dgVoodooConfig = Get-Content -LiteralPath $dgVoodooConfigPath -Raw
if ($dgVoodooConfig -notmatch `
        '(?ms)^\[DirectX\]\s*$.*?^Antialiasing\s*=\s*8x\s*$') {
    throw 'Release dgVoodoo DirectX configuration must use Antialiasing = 8x.'
}

$dgVoodooFiles = @(
    @{
        Path = 'third_party\dgVoodoo2-2.86.2\x86\D3DImm.dll'
        Sha256 = '8B2850D0AF5F07CF2928AC9666192C3ADCB0290F10ED8F942F1594F3A4F51C73'
    },
    @{
        Path = 'third_party\dgVoodoo2-2.86.2\x86\D3D9.dll'
        Sha256 = 'D8D2E15BF5D0E01C89317A733492997DE8F7F562A972FE564FD17D194EE5D1F3'
    }
)
foreach ($dgVoodooFile in $dgVoodooFiles) {
    $path = Join-Path $repoRoot $dgVoodooFile.Path
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required dgVoodoo runtime file is missing: $path"
    }
    $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actualHash -ne $dgVoodooFile.Sha256) {
        throw "Unexpected dgVoodoo runtime hash for $path`: $actualHash"
    }
}

$dxWrapperFiles = @(
    @{
        Path = 'third_party\deathtrap-dxwrapper-release225\x86\DDraw.dll'
        Sha256 = '8BAE794EB7506711F57B690CFB8660A5F008B0185E764F8CB56D5972E01A9F33'
    },
    @{
        Path = 'third_party\deathtrap-dxwrapper-release225\x86\dxwrapper.dll'
        Sha256 = 'A01EC795A633643CEA61A7CDC62EE97793D1674733D31E8240CE98C33E145841'
    }
)
foreach ($dxWrapperFile in $dxWrapperFiles) {
    $path = Join-Path $repoRoot $dxWrapperFile.Path
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required Deathtrap dxwrapper file is missing: $path"
    }
    $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actualHash -ne $dxWrapperFile.Sha256) {
        throw "Unexpected Deathtrap dxwrapper hash for $path`: $actualHash"
    }

    $binaryText = [System.Text.Encoding]::ASCII.GetString(
        [System.IO.File]::ReadAllBytes($path))
    if ($binaryText -match '[A-Za-z]:\\[^\x00]{0,260}\.pdb') {
        throw "Deathtrap dxwrapper contains an absolute local PDB path: $path"
    }
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-x86.ps1') `
        -Configuration Release -BuildDirectory $BuildDirectory
    if ($LASTEXITCODE -ne 0) {
        throw 'Verified release build failed.'
    }
}

$dllPath = Join-Path $repoRoot 'dist\DINPUT.dll'
if (-not (Test-Path -LiteralPath $dllPath -PathType Leaf)) {
    throw "Release DLL is missing: $dllPath"
}
$dllText = [System.Text.Encoding]::ASCII.GetString(
    [System.IO.File]::ReadAllBytes($dllPath))
if ($dllText -match '[A-Za-z]:\\[^\x00]{0,260}\.pdb') {
    throw "Release DLL contains an absolute local PDB path: $dllPath"
}

$versionParts = $version.Split('.') | ForEach-Object { [int]$_ }
$dllVersionInfo = (Get-Item -LiteralPath $dllPath).VersionInfo
$numericVersionMatches =
    $dllVersionInfo.FileMajorPart -eq $versionParts[0] -and
    $dllVersionInfo.FileMinorPart -eq $versionParts[1] -and
    $dllVersionInfo.FileBuildPart -eq $versionParts[2] -and
    $dllVersionInfo.FilePrivatePart -eq 0
if ($dllVersionInfo.ProductVersion -ne $version -or
    -not $numericVersionMatches) {
    throw "DLL version $($dllVersionInfo.ProductVersion) does not match VERSION $version."
}

$outputRoot = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
    [System.IO.Path]::GetFullPath($OutputDirectory)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDirectory))
}
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$releaseName = "Deathtrap-Native-50-$version"
$stageRoot = Join-Path $outputRoot $releaseName
$archivePath = Join-Path $outputRoot "$releaseName.zip"
$archiveHashPath = Join-Path $outputRoot "$releaseName-SHA256.txt"
foreach ($path in @($stageRoot, $archivePath, $archiveHashPath)) {
    if (Test-Path -LiteralPath $path) {
        throw "Release output already exists; use an empty output directory: $path"
    }
}

New-Item -ItemType Directory -Path $stageRoot | Out-Null

$releaseFiles = @(
    @{ Source = 'dist\DINPUT.dll'; Destination = 'payload\DINPUT.dll' },
    @{ Source = 'config\deathtrap_native.ini'; Destination = 'payload\deathtrap_native.ini' },
    @{ Source = 'config\keys.cfg'; Destination = 'payload\keys.cfg' },
    @{ Source = 'third_party\dgVoodoo2-2.86.2\x86\D3DImm.dll'; Destination = 'payload\dgVoodoo\D3DImm.dll' },
    @{ Source = 'third_party\dgVoodoo2-2.86.2\x86\D3D9.dll'; Destination = 'payload\dgVoodoo\D3D9.dll' },
    @{ Source = 'third_party\deathtrap-dxwrapper-release225\x86\DDraw.dll'; Destination = 'payload\dxwrapper\DDraw.dll' },
    @{ Source = 'third_party\deathtrap-dxwrapper-release225\x86\dxwrapper.dll'; Destination = 'payload\dxwrapper\dxwrapper.dll' },
    @{ Source = 'config\dxwrapper-dgvoodoo.ini'; Destination = 'payload\dxwrapper.ini' },
    @{ Source = 'config\dgVoodoo-recommended.conf'; Destination = 'payload\dgVoodoo.conf' },
    @{ Source = 'docs\images\dgvoodoo-general.png'; Destination = 'optional\dgVoodoo-General.png' },
    @{ Source = 'docs\images\dgvoodoo-directx.png'; Destination = 'optional\dgVoodoo-DirectX.png' },
    @{ Source = 'scripts\install.ps1'; Destination = 'install.ps1' },
    @{ Source = 'scripts\INSTALL.cmd'; Destination = 'INSTALL.cmd' },
    @{ Source = 'CHANGELOG.md'; Destination = 'CHANGELOG.txt' },
    @{ Source = 'THIRD_PARTY_NOTICES.md'; Destination = 'THIRD_PARTY_NOTICES.txt' },
    @{ Source = 'LICENSE'; Destination = 'LICENSE.txt' },
    @{ Source = 'third_party\deathtrap-dxwrapper-release225\License.txt'; Destination = 'licenses\dxwrapper.txt' },
    @{ Source = 'third_party\deathtrap-dxwrapper-release225\source\README.md'; Destination = 'source\dxwrapper\README.md' },
    @{ Source = 'third_party\deathtrap-dxwrapper-release225\source\deathtrap-dxwrapper.patch'; Destination = 'source\dxwrapper\deathtrap-dxwrapper.patch' }
)

foreach ($file in $releaseFiles) {
    $destination = Join-Path $stageRoot $file.Destination
    $destinationDirectory = Split-Path -Parent $destination
    if (-not (Test-Path -LiteralPath $destinationDirectory)) {
        New-Item -ItemType Directory -Path $destinationDirectory | Out-Null
    }
    Copy-Item -LiteralPath (Join-Path $repoRoot $file.Source) `
        -Destination $destination
}

$releaseReadmeTemplate = Get-Content -LiteralPath `
    (Join-Path $repoRoot 'docs\RELEASE-README.txt.in') -Raw
if (-not $releaseReadmeTemplate.Contains('@VERSION@')) {
    throw 'Release README template does not contain @VERSION@.'
}
$releaseReadme = $releaseReadmeTemplate.Replace('@VERSION@', $version)
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText(
    (Join-Path $stageRoot 'README.txt'), $releaseReadme, $utf8NoBom)

$manifestLines = New-Object System.Collections.Generic.List[string]
$packagedFiles = Get-ChildItem -LiteralPath $stageRoot -Recurse -File |
    Sort-Object FullName
$forbiddenPayload = $packagedFiles | Where-Object {
    $_.Name -match '(?i)(multiplayer|relay|ipx)'
}
if ($forbiddenPayload) {
    throw "Forbidden experimental file in release package: $($forbiddenPayload.FullName -join ', ')"
}
foreach ($file in $packagedFiles) {
    $relativePath = $file.FullName.Substring($stageRoot.Length + 1).Replace('\', '/')
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifestLines.Add("$hash  $relativePath")
}
[System.IO.File]::WriteAllLines(
    (Join-Path $stageRoot 'SHA256SUMS.txt'), $manifestLines, $utf8NoBom)

Compress-Archive -Path (Join-Path $stageRoot '*') -DestinationPath $archivePath `
    -CompressionLevel Optimal
$archiveHash = (Get-FileHash -LiteralPath $archivePath `
    -Algorithm SHA256).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText(
    $archiveHashPath,
    "$archiveHash  $([System.IO.Path]::GetFileName($archivePath))`n",
    $utf8NoBom)

Write-Host "Release package: $archivePath"
Write-Host "Release checksum: $archiveHashPath"
