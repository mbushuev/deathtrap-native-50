[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory = $true)]
    [string]$GameDirectory,
    [string]$DllPath = '',
    [switch]$SkipGameHashCheck
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$game = (Resolve-Path -LiteralPath $GameDirectory).Path
$dungeon = Join-Path $game 'Dungeon.dll'
$expectedDungeon = '95FE9CE0FFF387F00704548F152E4340815213FCB3833DBE1B5C42871E7D2E56'
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
if (-not $SkipGameHashCheck) {
    # Use the .NET stream directly so the installer's -WhatIf preference can't
    # suppress read-only hashing through the FileSystem provider.
    $stream = [System.IO.File]::OpenRead($dungeon)
    try {
        $hasher = [System.Security.Cryptography.SHA256]::Create()
        try {
            $actual = ([System.BitConverter]::ToString(
                $hasher.ComputeHash($stream))).Replace('-', '')
        }
        finally {
            $hasher.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
    if ($actual -ne $expectedDungeon) {
        throw "Unsupported Dungeon.dll SHA-256: $actual"
    }
}

foreach ($wrapper in @('DDraw.dll', 'D3DImm.dll', 'D3D9.dll')) {
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
    $DllPath = Join-Path $repoRoot 'dist\DINPUT.dll'
}
$dll = (Resolve-Path -LiteralPath $DllPath).Path
$ini = Join-Path $repoRoot 'config\deathtrap_native.ini'
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
    $newline = if ($Text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $line = "define    $Action    DOWN      $Expression"
    return $Text.Insert($anchor.Index + $anchor.Length, $newline + $line)
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
    Copy-Item -LiteralPath $dll -Destination (Join-Path $game 'DINPUT.dll') -Force
    Copy-Item -LiteralPath $ini -Destination (Join-Path $game 'deathtrap_native.ini') -Force
    if (-not (Test-Path -LiteralPath $keys)) {
        throw "Retail control file was not found: $keys"
    }
    $keyText = [System.IO.File]::ReadAllText($keys)
    foreach ($binding in @(
        @('ACTION_TURN_LEFT', 'MOUSE_HORIZ_LEFT'),
        @('ACTION_TURN_RIGHT', 'MOUSE_HORIZ_RIGHT'),
        @('ACTION_TURN_FAST_LEFT', 'KEY_LSHIFT + MOUSE_HORIZ_LEFT'),
        @('ACTION_TURN_FAST_RIGHT', 'KEY_LSHIFT + MOUSE_HORIZ_RIGHT'),
        @('ACTION_ATTACK_1', 'MOUSE_LBUTTON'),
        @('ACTION_PARRY', 'MOUSE_RBUTTON'),
        @('ACTION_LEFT_SIDESTEP', 'KEY_J'),
        @('ACTION_RIGHT_SIDESTEP', 'KEY_K')
    )) {
        $keyText = Add-NativeBinding -Text $keyText -Action $binding[0] -Expression $binding[1]
    }
    [System.IO.File]::WriteAllText($keys, $keyText, [System.Text.Encoding]::ASCII)
    Write-Host "Installed overlay into: $game"
Write-Host 'Installed native mouse bindings, wheel bridge and XInput test layer.'
    Write-Host "Rollback copy: $backup"
}
