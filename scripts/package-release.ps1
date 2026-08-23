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
    @{ Source = 'scripts\install.ps1'; Destination = 'install.ps1' },
    @{ Source = 'scripts\INSTALL.cmd'; Destination = 'INSTALL.cmd' },
    @{ Source = 'CHANGELOG.md'; Destination = 'CHANGELOG.txt' },
    @{ Source = 'THIRD_PARTY_NOTICES.md'; Destination = 'THIRD_PARTY_NOTICES.txt' },
    @{ Source = 'LICENSE'; Destination = 'LICENSE.txt' }
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
