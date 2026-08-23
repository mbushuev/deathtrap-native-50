[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$GameDirectory = '',
    [string]$DllPath = '',
    [string]$IniPath = '',
    [switch]$SkipGameHashCheck
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $GameDirectory) {
    $localExecutable = Join-Path $PSScriptRoot 'DD_CD.EXE'
    if (-not (Test-Path -LiteralPath $localExecutable -PathType Leaf)) {
        throw 'GameDirectory is required unless install.ps1 is beside DD_CD.EXE.'
    }
    $GameDirectory = $PSScriptRoot
}
$game = (Resolve-Path -LiteralPath $GameDirectory).Path
$dungeon = Join-Path $game 'Dungeon.dll'
$executable = Join-Path $game 'DD_CD.EXE'
$expectedDungeon = '95FE9CE0FFF387F00704548F152E4340815213FCB3833DBE1B5C42871E7D2E56'
$expectedExecutable = '0C644A00E62652E046C5DAD2960F0F6C8C1998F4CA065780FBD7811D9908BF1F'
$commonDirectory = Split-Path -Parent $game
$steamAppsDirectory = Split-Path -Parent $commonDirectory
$manifest = Join-Path $steamAppsDirectory 'appmanifest_245010.acf'

if ((Split-Path -Leaf $commonDirectory) -ine 'common' -or
    (Split-Path -Leaf $steamAppsDirectory) -ine 'steamapps' -or
    -not (Test-Path -LiteralPath $manifest)) {
    throw 'GameDirectory must point to the Steam installation of Deathtrap Dungeon.'
}
$manifestText = Get-Content -LiteralPath $manifest -Raw
if ($manifestText -notmatch '"appid"\s+"245010"') {
    throw "Steam manifest does not describe Deathtrap Dungeon: $manifest"
}

if (-not (Test-Path -LiteralPath $dungeon)) {
    throw "Dungeon.dll was not found in $game"
}
if (-not (Test-Path -LiteralPath $executable)) {
    throw "DD_CD.EXE was not found in $game"
}

function Get-FileSha256 {
    param([Parameter(Mandatory = $true)][string]$Path)

    # Use the .NET stream directly so the installer's -WhatIf preference can't
    # suppress read-only hashing through the FileSystem provider.
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $hasher = [System.Security.Cryptography.SHA256]::Create()
        try {
            return ([System.BitConverter]::ToString(
                $hasher.ComputeHash($stream))).Replace('-', '')
        }
        finally {
            $hasher.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

if (-not $SkipGameHashCheck) {
    foreach ($binary in @(
        @{ Name = 'Dungeon.dll'; Path = $dungeon; Expected = $expectedDungeon },
        @{ Name = 'DD_CD.EXE'; Path = $executable; Expected = $expectedExecutable }
    )) {
        $actual = Get-FileSha256 -Path $binary.Path
        if ($actual -ne $binary.Expected) {
            Write-Warning (
                "$($binary.Name) does not match the version tested by the " +
                "project. Expected SHA-256: $($binary.Expected); actual: " +
                "$actual. Installation will continue, but the patch may be " +
                'partially or completely incompatible with these game files.')
        }
    }
}

foreach ($wrapper in @('DDraw.dll', 'D3DImm.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $game $wrapper))) {
        throw "External dgVoodoo wrapper is missing: $wrapper"
    }
}
$ddrawVersionText = (Get-Item -LiteralPath (Join-Path $game 'DDraw.dll')).VersionInfo.ProductVersion
$ddrawVersion = $null
if ($ddrawVersionText) {
    $normalizedVersion = ($ddrawVersionText -replace '[^0-9.].*$', '')
    $parsedVersion = [version]'0.0'
    if ([version]::TryParse($normalizedVersion, [ref]$parsedVersion)) {
        $ddrawVersion = $parsedVersion
    }
}
if ($ddrawVersion -and $ddrawVersion -lt [version]'2.8.6') {
    throw "dgVoodoo 2.86 or newer is required; found $ddrawVersionText"
}
if (-not $ddrawVersion) {
    Write-Warning 'Could not identify the dgVoodoo version; 2.86 or newer is required.'
}
$dgConfig = Join-Path $game 'dgVoodoo.conf'
if (-not (Test-Path -LiteralPath $dgConfig)) {
    throw 'dgVoodoo.conf is missing.'
}
if (-not (Select-String -LiteralPath $dgConfig -Pattern '^OutputAPI\s*=\s*d3d11_fl11_0\s*$' -Quiet)) {
    throw 'dgVoodoo OutputAPI must be d3d11_fl11_0.'
}

if (-not $DllPath) {
    $payloadDll = Join-Path $PSScriptRoot 'payload\DINPUT.dll'
    $flatDll = Join-Path $PSScriptRoot 'DINPUT.dll'
    $DllPath = if (Test-Path -LiteralPath $payloadDll -PathType Leaf) {
        $payloadDll
    } elseif (Test-Path -LiteralPath $flatDll -PathType Leaf) {
        $flatDll
    } else {
        Join-Path $repoRoot 'dist\DINPUT.dll'
    }
}
$dll = (Resolve-Path -LiteralPath $DllPath).Path
if (-not $IniPath) {
    $payloadIni = Join-Path $PSScriptRoot 'payload\deathtrap_native.ini'
    $flatIni = Join-Path $PSScriptRoot 'deathtrap_native.ini'
    $IniPath = if (Test-Path -LiteralPath $payloadIni -PathType Leaf) {
        $payloadIni
    } elseif (Test-Path -LiteralPath $flatIni -PathType Leaf) {
        $flatIni
    } else {
        Join-Path $repoRoot 'config\deathtrap_native.ini'
    }
}
$ini = (Resolve-Path -LiteralPath $IniPath).Path
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $game "back\deathtrap-native50-overlay-$stamp"
$keys = Join-Path $game 'ASYLUM\keys.cfg'

function Add-NativeBinding {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Action,
        [Parameter(Mandatory = $true)][string]$Expression
    )

    $actionPattern = [regex]::Escape($Action)
    $expressionPattern = [regex]::Escape($Expression)
    $existingPattern = "(?m)^\s*define\s+$actionPattern\s+DOWN\s+$expressionPattern\s*\r?$"
    if ([regex]::IsMatch($Text, $existingPattern)) {
        return $Text
    }

    $anchorPattern = "(?m)^\s*define\s+$actionPattern\b[^\r\n]*\r?$"
    $matches = [regex]::Matches($Text, $anchorPattern)
    if ($matches.Count -eq 0) {
        throw "Could not locate $Action in $keys"
    }
    $anchor = $matches[$matches.Count - 1]
    $insertAt = $anchor.Index + $anchor.Length
    if ($insertAt -gt 0 -and $Text[$insertAt - 1] -eq "`r") {
        --$insertAt
    }
    $newline = if ($Text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $line = "define    $Action    DOWN      $Expression"
    return $Text.Insert($insertAt, $newline + $line)
}

if ($PSCmdlet.ShouldProcess($game, 'Install Deathtrap Native 50 overlay')) {
    New-Item -ItemType Directory -Force -Path $backup | Out-Null
    foreach ($existing in @('DINPUT.dll', 'deathtrap_native.ini', 'ASYLUM\keys.cfg')) {
        $path = Join-Path $game $existing
        if (Test-Path -LiteralPath $path) {
            $backupName = $existing -replace '[\\/]', '_'
            Copy-Item -LiteralPath $path -Destination (Join-Path $backup $backupName) -Force
        }
    }
    $logsDirectory = Join-Path $game 'logs'
    New-Item -ItemType Directory -Force -Path $logsDirectory | Out-Null
    foreach ($legacyLog in @('deathtrap_native_render.log', 'deathtrap_native_present.log')) {
        $legacyPath = Join-Path $game $legacyLog
        if (Test-Path -LiteralPath $legacyPath) {
            $legacyBase = [System.IO.Path]::GetFileNameWithoutExtension($legacyLog)
            $legacyDestination = Join-Path $logsDirectory "$legacyBase-legacy-$stamp.log"
            Move-Item -LiteralPath $legacyPath -Destination $legacyDestination
        }
    }
    $dllDestination = Join-Path $game 'DINPUT.dll'
    $iniDestination = Join-Path $game 'deathtrap_native.ini'
    if (-not $dll.Equals($dllDestination,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        Copy-Item -LiteralPath $dll -Destination $dllDestination -Force
    }
    if (-not $ini.Equals($iniDestination,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        Copy-Item -LiteralPath $ini -Destination $iniDestination -Force
    }
    if (-not (Test-Path -LiteralPath $keys)) {
        throw "Retail control file was not found: $keys"
    }
    $keyText = [System.IO.File]::ReadAllText($keys)
    foreach ($binding in @(
        @('ACTION_TURN_LEFT', 'MOUSE_HORIZ_LEFT'),
        @('ACTION_TURN_RIGHT', 'MOUSE_HORIZ_RIGHT'),
        @('ACTION_TURN_FAST_LEFT', 'KEY_LSHIFT + MOUSE_HORIZ_LEFT'),
        @('ACTION_TURN_FAST_RIGHT', 'KEY_LSHIFT + MOUSE_HORIZ_RIGHT'),
        @('ACTION_WALK_FORWARD', 'JOY_VERT_FORWARDS'),
        @('ACTION_WALK_BACKWARD', 'JOY_VERT_BACKWARDS'),
        @('ACTION_RUN_FORWARD', 'KEY_LSHIFT + JOY_VERT_FORWARDS'),
        @('ACTION_RUN_BACKWARD', 'KEY_LSHIFT + JOY_VERT_BACKWARDS'),
        @('ACTION_TURN_LEFT', 'JOY_HORIZ_LEFT'),
        @('ACTION_TURN_RIGHT', 'JOY_HORIZ_RIGHT'),
        @('ACTION_TURN_FAST_LEFT', 'KEY_LSHIFT + JOY_HORIZ_LEFT'),
        @('ACTION_TURN_FAST_RIGHT', 'KEY_LSHIFT + JOY_HORIZ_RIGHT'),
        @('ACTION_JUMP_FORWARD', 'KEY_SPACE + JOY_VERT_FORWARDS'),
        @('ACTION_JUMP_BACKWARD', 'KEY_SPACE + JOY_VERT_BACKWARDS'),
        @('ACTION_JUMP_LEFT', 'KEY_SPACE + JOY_HORIZ_LEFT'),
        @('ACTION_JUMP_RIGHT', 'KEY_SPACE + JOY_HORIZ_RIGHT'),
        @('ACTION_JUMP_LEFT', 'KEY_SPACE + KEY_J'),
        @('ACTION_JUMP_RIGHT', 'KEY_SPACE + KEY_K'),
        @('ACTION_ATTACK_1', 'MOUSE_LBUTTON'),
        @('ACTION_PARRY', 'MOUSE_RBUTTON'),
        @('ACTION_1ST_PERSON_VIEW', 'KEY_TAB'),
        @('ACTION_LEFT_SIDESTEP', 'KEY_J'),
        @('ACTION_RIGHT_SIDESTEP', 'KEY_K')
    )) {
        $keyText = Add-NativeBinding -Text $keyText -Action $binding[0] -Expression $binding[1]
    }
    if ($keyText.Contains("`r`r`n")) {
        throw 'Refusing to write ASYLUM/keys.cfg with invalid CR-CR-LF line endings.'
    }
    [System.IO.File]::WriteAllText($keys, $keyText, [System.Text.Encoding]::ASCII)
    Write-Host "Installed overlay into: $game"
Write-Host 'Installed native mouse bindings, wheel bridge and XInput test layer.'
    Write-Host "Rollback copy: $backup"
}
