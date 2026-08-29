[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$GameDirectory = '',
    [string]$DllPath = '',
    [string]$IniPath = '',
    [string]$KeysPath = '',
    [string]$DgVoodooRuntimeDirectory = '',
    [string]$DgVoodooConfigPath = '',
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

if (-not $DgVoodooRuntimeDirectory) {
    $payloadRuntime = Join-Path $PSScriptRoot 'payload\dgVoodoo'
    $repositoryRuntime = Join-Path $repoRoot 'third_party\dgVoodoo2-2.86.2\x86'
    $DgVoodooRuntimeDirectory = if (
        (Test-Path -LiteralPath (Join-Path $payloadRuntime 'DDraw.dll') -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $payloadRuntime 'D3DImm.dll') -PathType Leaf)
    ) {
        $payloadRuntime
    } else {
        $repositoryRuntime
    }
}
$dgRuntime = (Resolve-Path -LiteralPath $DgVoodooRuntimeDirectory).Path
$dgVoodooPayload = @(
    @{
        Name = 'DDraw.dll'
        Sha256 = '9EDACB27DE03EA2D0C104DE2CE255D4C992A46E2867BCCB3713A8995D56F84A5'
    },
    @{
        Name = 'D3DImm.dll'
        Sha256 = '8B2850D0AF5F07CF2928AC9666192C3ADCB0290F10ED8F942F1594F3A4F51C73'
    }
)
foreach ($wrapper in $dgVoodooPayload) {
    $path = Join-Path $dgRuntime $wrapper.Name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Bundled dgVoodoo runtime file is missing: $path"
    }
    $actualHash = Get-FileSha256 -Path $path
    if ($actualHash -ne $wrapper.Sha256) {
        throw "Bundled dgVoodoo runtime file failed verification: $($wrapper.Name)"
    }
}
if (-not $DgVoodooConfigPath) {
    $payloadConfig = Join-Path $PSScriptRoot 'payload\dgVoodoo.conf'
    $DgVoodooConfigPath = if (Test-Path -LiteralPath $payloadConfig -PathType Leaf) {
        $payloadConfig
    } else {
        Join-Path $repoRoot 'config\dgVoodoo-recommended.conf'
    }
}
$dgConfigSource = (Resolve-Path -LiteralPath $DgVoodooConfigPath).Path
if (-not (Select-String -LiteralPath $dgConfigSource `
        -Pattern '^OutputAPI\s*=\s*d3d11_fl11_0\s*$' -Quiet)) {
    throw 'Bundled dgVoodoo configuration must use OutputAPI = d3d11_fl11_0.'
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
if (-not $KeysPath) {
    $payloadKeys = Join-Path $PSScriptRoot 'payload\keys.cfg'
    $flatKeys = Join-Path $PSScriptRoot 'keys.cfg'
    $KeysPath = if (Test-Path -LiteralPath $payloadKeys -PathType Leaf) {
        $payloadKeys
    } elseif (Test-Path -LiteralPath $flatKeys -PathType Leaf) {
        $flatKeys
    } else {
        Join-Path $repoRoot 'config\keys.cfg'
    }
}
$keysSource = (Resolve-Path -LiteralPath $KeysPath).Path
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $game "back\deathtrap-native50-overlay-$stamp"
$keys = Join-Path $game 'ASYLUM\keys.cfg'
$retailConfig = Join-Path $game 'ASYLUM\config.dat'

function Set-RetailConfigValue {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Value
    )

    $newline = if ($Text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $namePattern = [regex]::Escape($Name)
    # The retail DDCONFIG utility can append its first Direct3D field without
    # a newline (for example, "RESOLUTION 5RENDERING_PLATFORM 0").  Split that
    # malformed boundary before normalizing the setting.
    $Text = [regex]::Replace(
        $Text, "(?m)(?<=\S)(?=$namePattern\s+)", $newline)
    $hadTerminalNewline = $Text.EndsWith("`n")
    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($existingLine in [regex]::Split($Text, "\r?\n")) {
        $lines.Add($existingLine)
    }
    if ($hadTerminalNewline -and $lines.Count -gt 0 -and
            $lines[$lines.Count - 1] -eq '') {
        $lines.RemoveAt($lines.Count - 1)
    }

    $pattern = "^\s*$namePattern(?:\s+.*)?\s*$"
    $rewritten = [System.Collections.Generic.List[string]]::new()
    $inserted = $false
    foreach ($existingLine in $lines) {
        if ([regex]::IsMatch($existingLine, $pattern)) {
            if (-not $inserted) {
                $rewritten.Add("$Name $Value")
                $inserted = $true
            }
            continue
        }
        $rewritten.Add($existingLine)
    }
    if (-not $inserted) {
        $rewritten.Add("$Name $Value")
    }

    $result = [string]::Join($newline, $rewritten)
    if ($hadTerminalNewline -or $Text.Length -eq 0) {
        $result += $newline
    }
    return $result
}

if ($PSCmdlet.ShouldProcess($game, 'Install Deathtrap Native 50 overlay')) {
    New-Item -ItemType Directory -Force -Path $backup | Out-Null
    foreach ($existing in @(
        'DINPUT.dll',
        'deathtrap_native.ini',
        'DDraw.dll',
        'D3DImm.dll',
        'dgVoodoo.conf',
        'ASYLUM\keys.cfg',
        'ASYLUM\config.dat'
    )) {
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
    foreach ($wrapper in $dgVoodooPayload) {
        Copy-Item -LiteralPath (Join-Path $dgRuntime $wrapper.Name) `
            -Destination (Join-Path $game $wrapper.Name) -Force
    }
    Copy-Item -LiteralPath $dgConfigSource `
        -Destination (Join-Path $game 'dgVoodoo.conf') -Force
    if (-not (Test-Path -LiteralPath $keys)) {
        throw "Retail control file was not found: $keys"
    }
    if (-not (Test-Path -LiteralPath $retailConfig)) {
        throw "Retail rendering configuration was not found: $retailConfig"
    }
    # The DLL's modern input routing and the action table are one tested unit.
    # Back up the user's previous file above, then install the exact profile
    # shipped with this build instead of trying to merge arbitrary remaps.
    $keyText = [System.IO.File]::ReadAllText($keysSource)
    $keyText = [regex]::Replace($keyText, "\r?\n", "`r`n")
    if (-not $keyText.EndsWith("`r`n")) {
        $keyText += "`r`n"
    }
    if ($keyText.Contains("`r`r`n")) {
        throw 'Bundled keys.cfg normalized to invalid CR-CR-LF line endings.'
    }
    [System.IO.File]::WriteAllText(
        $keys, $keyText, [System.Text.Encoding]::ASCII)

    $configText = [System.IO.File]::ReadAllText($retailConfig)
    foreach ($setting in @(
        @('RENDERING_PLATFORM', '13'),
        @('D3D_ALLOW_MIPMAP', '1'),
        @('D3D_ALLOW_PALETTISED', '0'),
        @('D3D_TYPE1_SHADOWS', '1')
    )) {
        $configText = Set-RetailConfigValue -Text $configText `
            -Name $setting[0] -Value $setting[1]
    }
    if ($configText.Contains("`r`r`n")) {
        throw 'Refusing to write ASYLUM/config.dat with invalid CR-CR-LF line endings.'
    }
    [System.IO.File]::WriteAllText(
        $retailConfig, $configText, [System.Text.Encoding]::ASCII)
    Write-Host "Installed overlay into: $game"
    Write-Host 'Installed the tested dgVoodoo 2.86.2 x86 runtime and configuration.'
    Write-Host 'Installed the modern keyboard, mouse and XInput control profile.'
    Write-Host 'Applied the required Direct3D rendering profile.'
    Write-Host "Rollback copy: $backup"
}
