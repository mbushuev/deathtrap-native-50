$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("deathtrap-installer-payload-upgrade-" + [guid]::NewGuid().ToString('N'))
$steamApps = Join-Path $testRoot 'steamapps'
$game = Join-Path $steamApps 'common\Deathtrap Dungeon'
$payload = Join-Path $game 'payload'
$keysDirectory = Join-Path $game 'ASYLUM'

function Get-TestHash {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

try {
    New-Item -ItemType Directory -Path $payload -Force | Out-Null
    New-Item -ItemType Directory -Path $keysDirectory -Force | Out-Null
    [System.IO.File]::WriteAllText(
        (Join-Path $steamApps 'appmanifest_245010.acf'),
        '"AppState" { "appid" "245010" }')
    foreach ($name in @('Dungeon.dll', 'DD_CD.EXE', 'DDraw.dll', 'D3DImm.dll')) {
        [System.IO.File]::WriteAllText((Join-Path $game $name), "test $name")
    }
    [System.IO.File]::WriteAllText(
        (Join-Path $game 'dgVoodoo.conf'),
        "[General]`r`nOutputAPI = d3d11_fl11_0`r`n")

    $oldDll = Join-Path $game 'DINPUT.dll'
    $oldIni = Join-Path $game 'deathtrap_native.ini'
    [System.IO.File]::WriteAllText($oldDll, 'previous installed DLL')
    [System.IO.File]::WriteAllText($oldIni, 'previous installed configuration')
    $oldDllHash = Get-TestHash $oldDll
    $oldIniHash = Get-TestHash $oldIni

    $originalKeys = (@(
        'define    ACTION_WALK_FORWARD       DOWN      KEY_UP',
        'define    ACTION_WALK_BACKWARD      DOWN      KEY_DOWN',
        'define    ACTION_RUN_FORWARD        DOWN      KEY_A + KEY_UP',
        'define    ACTION_RUN_BACKWARD       DOWN      KEY_A + KEY_DOWN',
        'define    ACTION_STEP_FORWARD       DOWN      KEY_Z + KEY_UP',
        'define    ACTION_STEP_BACKWARD      DOWN      KEY_Z + KEY_DOWN',
        'define    ACTION_LEFT_SIDESTEP      DOWN      KEY_Z + KEY_LEFT',
        'define    ACTION_RIGHT_SIDESTEP     DOWN      KEY_Z + KEY_RIGHT',
        'define    ACTION_TURN_LEFT          DOWN      KEY_LEFT',
        'define    ACTION_TURN_RIGHT         DOWN      KEY_RIGHT',
        'define    ACTION_TURN_FAST_LEFT     DOWN      KEY_A + KEY_LEFT',
        'define    ACTION_TURN_FAST_RIGHT    DOWN      KEY_A + KEY_RIGHT',
        'define    ACTION_ATTACK_RANGED      DOWN      KEY_CAPS',
        'define    ACTION_ATTACK_1           DOWN      KEY_CAPS + KEY_UP',
        'define    ACTION_ATTACK_2           DOWN      KEY_CAPS + KEY_LEFT',
        'define    ACTION_ATTACK_3           DOWN      KEY_CAPS + KEY_RIGHT',
        'define    ACTION_ATTACK_BACK        DOWN      KEY_CAPS + KEY_LEFT + KEY_RIGHT',
        'define    ACTION_PARRY              DOWN      KEY_CAPS + KEY_DOWN',
        'define    ACTION_CAST_SPELL         DOWN      KEY_S',
        'define    ACTION_JUMP_CLIMB         DOWN      KEY_ENTER',
        'define    ACTION_JUMP_LEFT          DOWN      KEY_ENTER + KEY_LEFT',
        'define    ACTION_JUMP_RIGHT         DOWN      KEY_ENTER + KEY_RIGHT',
        'define    ACTION_JUMP_FORWARD       DOWN      KEY_ENTER + KEY_UP',
        'define    ACTION_JUMP_BACKWARD      DOWN      KEY_ENTER + KEY_DOWN',
        'define    ACTION_1ST_PERSON_VIEW    DOWN      KEY_F24',
        'define    ACTION_OPERATE            PRESS     KEY_SPACE'
    ) -join "`r`n") + "`r`n"
    $keys = Join-Path $keysDirectory 'keys.cfg'
    [System.IO.File]::WriteAllText($keys, $originalKeys, [System.Text.Encoding]::ASCII)
    $oldKeysHash = Get-TestHash $keys
    $retailConfig = Join-Path $keysDirectory 'config.dat'
    [System.IO.File]::WriteAllText(
        $retailConfig,
        "CFG_FILE asylum\keys.cfg`r`n" +
        "RESOLUTION 5RENDERING_PLATFORM 0`r`n" +
        "D3D_ALLOW_MIPMAP 0`r`n" +
        "RENDERING_PLATFORM 12`r`n" +
        "D3D_ALLOW_MIPMAP 99`r`n" +
        "CUSTOM_TEST_VALUE 7`r`n",
        [System.Text.Encoding]::ASCII)
    $oldRetailConfigHash = Get-TestHash $retailConfig

    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'install.ps1') `
        -Destination (Join-Path $game 'install.ps1')
    Copy-Item -LiteralPath (Join-Path $repoRoot 'dist\DINPUT.dll') `
        -Destination (Join-Path $payload 'DINPUT.dll')
    Copy-Item -LiteralPath (Join-Path $repoRoot 'config\deathtrap_native.ini') `
        -Destination (Join-Path $payload 'deathtrap_native.ini')

    & powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $game 'install.ps1') -SkipGameHashCheck
    if ($LASTEXITCODE -ne 0) {
        throw "Payload upgrade installer exited with $LASTEXITCODE."
    }

    if ((Get-TestHash $oldDll) -ne (Get-TestHash (Join-Path $payload 'DINPUT.dll'))) {
        throw 'Installer did not replace the old DLL with the payload DLL.'
    }
    if ((Get-TestHash $oldIni) -ne (Get-TestHash (Join-Path $payload 'deathtrap_native.ini'))) {
        throw 'Installer did not replace the old configuration with the payload configuration.'
    }

    $backups = @(Get-ChildItem -LiteralPath (Join-Path $game 'back') -Directory |
        Where-Object { $_.Name -like 'deathtrap-native50-overlay-*' })
    if ($backups.Count -ne 1) {
        throw "Expected one rollback directory; found $($backups.Count)."
    }
    $backup = $backups[0]
    if ((Get-TestHash (Join-Path $backup.FullName 'DINPUT.dll')) -ne $oldDllHash) {
        throw 'Rollback directory does not contain the previous DLL.'
    }
    if ((Get-TestHash (Join-Path $backup.FullName 'deathtrap_native.ini')) -ne $oldIniHash) {
        throw 'Rollback directory does not contain the previous configuration.'
    }
    if ((Get-TestHash (Join-Path $backup.FullName 'ASYLUM_keys.cfg')) -ne $oldKeysHash) {
        throw 'Rollback directory does not contain the original control file.'
    }
    if ((Get-TestHash (Join-Path $backup.FullName 'ASYLUM_config.dat')) -ne
            $oldRetailConfigHash) {
        throw 'Rollback directory does not contain the original rendering configuration.'
    }

    $installedKeys = [System.IO.File]::ReadAllText($keys)
    if ($installedKeys.Contains("`r`r`n")) {
        throw 'Installer produced invalid CR-CR-LF line endings.'
    }
    foreach ($binding in @(
        @('ACTION_TURN_LEFT', 'MOUSE_HORIZ_LEFT'),
        @('ACTION_ATTACK_1', 'MOUSE_LBUTTON'),
        @('ACTION_1ST_PERSON_VIEW', 'KEY_TAB'),
        @('ACTION_LEFT_SIDESTEP', 'KEY_J'),
        @('ACTION_RIGHT_SIDESTEP', 'KEY_K')
    )) {
        $pattern = '(?m)^\s*define\s+' + [regex]::Escape($binding[0]) +
            '\s+DOWN\s+' + [regex]::Escape($binding[1]) + '\r?$'
        if ([regex]::Matches($installedKeys, $pattern).Count -ne 1) {
            throw "Expected one installed binding for $($binding[0]) / $($binding[1])."
        }
    }
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
        $pattern = '(?m)^\s*define\s+' + [regex]::Escape($binding[0]) +
            '\s+' + [regex]::Escape($binding[1]) + '\s+' +
            [regex]::Escape($binding[2]) + '\r?$'
        if ([regex]::Matches($installedKeys, $pattern).Count -ne 1) {
            throw "Expected modern control binding for $($binding[0]) / $($binding[2])."
        }
    }
    $installedConfig = [System.IO.File]::ReadAllText($retailConfig)
    if ([regex]::Matches(
            $installedConfig, '(?m)^RESOLUTION 5\r?$').Count -ne 1) {
        throw 'Installer did not repair the retail DDCONFIG glued-line defect.'
    }
    foreach ($setting in @(
        @('RENDERING_PLATFORM', '13'),
        @('D3D_ALLOW_MIPMAP', '1'),
        @('D3D_ALLOW_PALETTISED', '0'),
        @('D3D_TYPE1_SHADOWS', '1'),
        @('CUSTOM_TEST_VALUE', '7')
    )) {
        $pattern = '(?m)^' + [regex]::Escape($setting[0]) + '\s+' +
            [regex]::Escape($setting[1]) + '\r?$'
        if ([regex]::Matches($installedConfig, $pattern).Count -ne 1) {
            throw "Expected rendering setting $($setting[0]) $($setting[1])."
        }
        $namePattern = '(?m)^' + [regex]::Escape($setting[0]) + '\s+.*\r?$'
        if ([regex]::Matches($installedConfig, $namePattern).Count -ne 1) {
            throw "Expected exactly one rendering setting named $($setting[0])."
        }
    }

    Write-Host 'Installer payload upgrade and rollback test passed.'
}
finally {
    $resolvedTestRoot = [System.IO.Path]::GetFullPath($testRoot)
    $temporaryRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    if ($resolvedTestRoot.StartsWith(
            $temporaryRoot, [System.StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedTestRoot).StartsWith(
            'deathtrap-installer-payload-upgrade-',
            [System.StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force `
            -ErrorAction SilentlyContinue
    }
}
