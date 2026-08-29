[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$GameDirectory = '',
    [string]$DllPath = '',
    [string]$IniPath = '',
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
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $game "back\deathtrap-native50-overlay-$stamp"
$keys = Join-Path $game 'ASYLUM\keys.cfg'
$retailConfig = Join-Path $game 'ASYLUM\config.dat'

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

function Remove-NativeLeftMouseCombatBindings {
    param(
        [Parameter(Mandatory = $true)][string]$Text
    )

    $pattern = '(?m)^\s*define\s+ACTION_(?:ATTACK_[A-Z0-9_]+|PARRY)' +
        '\s+DOWN\s+MOUSE_LBUTTON(?:\s*\+[^\r\n]*)?\s*\r?\n?'
    return [regex]::Replace($Text, $pattern, '')
}

function Set-NativeKeyboardBinding {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Action,
        [Parameter(Mandatory = $true)][string]$Trigger,
        [Parameter(Mandatory = $true)][string]$Expression
    )

    $newline = if ($Text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($line in [regex]::Split($Text, "\r?\n")) {
        $lines.Add($line)
    }
    $actionPattern = [regex]::Escape($Action)
    $triggerPattern = [regex]::Escape($Trigger)
    $pattern = "^(?<prefix>\s*define\s+$actionPattern\s+$triggerPattern\s+)" +
        '(?<expression>.*?)\s*$'
    $rewritten = [System.Collections.Generic.List[string]]::new()
    $inserted = $false
    $lastAction = -1
    foreach ($line in $lines) {
        $match = [regex]::Match($line, $pattern)
        if ($match.Success) {
            $boundExpression = $match.Groups['expression'].Value.Trim()
            if ($boundExpression -notmatch '\b(?:JOY|MOUSE)_') {
                if (-not $inserted) {
                    $rewritten.Add($match.Groups['prefix'].Value + $Expression)
                    $inserted = $true
                    $lastAction = $rewritten.Count - 1
                }
                continue
            }
        }
        $rewritten.Add($line)
        if ($line -match "^\s*define\s+$actionPattern\b") {
            $lastAction = $rewritten.Count - 1
        }
    }
    if (-not $inserted) {
        if ($lastAction -lt 0) {
            throw "Could not locate $Action in $keys"
        }
        $triggerColumn = if ($Trigger -eq 'PRESS') { 'PRESS     ' } else { 'DOWN      ' }
        $rewritten.Insert(
            $lastAction + 1,
            "define    $Action    $triggerColumn$Expression")
    }
    return [string]::Join($newline, $rewritten)
}

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
    $keyText = [System.IO.File]::ReadAllText($keys)
    # The patch's camera-relative input and documented controls require one
    # deterministic keyboard profile. Preserve mouse and joystick expressions,
    # but replace each action's keyboard-only expression with the accepted one.
    foreach ($binding in @(
        @('ACTION_WALK_FORWARD', 'DOWN', 'KEY_W'),
        @('ACTION_WALK_BACKWARD', 'DOWN', 'KEY_S'),
        @('ACTION_RUN_FORWARD', 'DOWN', 'KEY_LSHIFT + KEY_W'),
        @('ACTION_RUN_BACKWARD', 'DOWN', 'KEY_LSHIFT + KEY_S'),
        @('ACTION_STEP_FORWARD', 'DOWN', 'KEY_CTRL + KEY_W'),
        @('ACTION_STEP_BACKWARD', 'DOWN', 'KEY_CTRL + KEY_S'),
        @('ACTION_LEFT_SIDESTEP', 'DOWN', 'KEY_CTRL + KEY_A'),
        @('ACTION_RIGHT_SIDESTEP', 'DOWN', 'KEY_CTRL + KEY_D'),
        @('ACTION_TURN_LEFT', 'DOWN', 'KEY_A'),
        @('ACTION_TURN_RIGHT', 'DOWN', 'KEY_D'),
        @('ACTION_TURN_FAST_LEFT', 'DOWN', 'KEY_LSHIFT + KEY_A'),
        @('ACTION_TURN_FAST_RIGHT', 'DOWN', 'KEY_LSHIFT + KEY_D'),
        @('ACTION_ATTACK_RANGED', 'DOWN', 'KEY_F'),
        @('ACTION_ATTACK_1', 'DOWN', 'KEY_F + KEY_W'),
        @('ACTION_ATTACK_2', 'DOWN', 'KEY_F + KEY_A'),
        @('ACTION_ATTACK_3', 'DOWN', 'KEY_F + KEY_D'),
        @('ACTION_ATTACK_BACK', 'DOWN', 'KEY_F + KEY_A + KEY_D'),
        @('ACTION_PARRY', 'DOWN', 'KEY_F + KEY_S'),
        @('ACTION_CAST_SPELL', 'DOWN', 'KEY_Q'),
        @('ACTION_JUMP_CLIMB', 'DOWN', 'KEY_SPACE'),
        @('ACTION_JUMP_LEFT', 'DOWN', 'KEY_SPACE + KEY_A'),
        @('ACTION_JUMP_RIGHT', 'DOWN', 'KEY_SPACE + KEY_D'),
        @('ACTION_JUMP_FORWARD', 'DOWN', 'KEY_SPACE + KEY_W'),
        @('ACTION_JUMP_BACKWARD', 'DOWN', 'KEY_SPACE + KEY_S'),
        @('ACTION_OPERATE', 'PRESS', 'KEY_E')
    )) {
        $keyText = Set-NativeKeyboardBinding -Text $keyText `
            -Action $binding[0] -Trigger $binding[1] -Expression $binding[2]
    }
    # Mouse combat is translated to the verified F+direction grammar at the
    # DirectInput keyboard boundary. Remove every older left-button combat
    # experiment, including both WASD and retail-arrow variants, on upgrade.
    $keyText = Remove-NativeLeftMouseCombatBindings -Text $keyText
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
