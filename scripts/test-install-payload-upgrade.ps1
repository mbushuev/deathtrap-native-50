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

    $actions = @(
        'ACTION_TURN_LEFT', 'ACTION_TURN_RIGHT',
        'ACTION_TURN_FAST_LEFT', 'ACTION_TURN_FAST_RIGHT',
        'ACTION_WALK_FORWARD', 'ACTION_WALK_BACKWARD',
        'ACTION_RUN_FORWARD', 'ACTION_RUN_BACKWARD',
        'ACTION_JUMP_FORWARD', 'ACTION_JUMP_BACKWARD',
        'ACTION_JUMP_LEFT', 'ACTION_JUMP_RIGHT',
        'ACTION_ATTACK_1', 'ACTION_PARRY', 'ACTION_1ST_PERSON_VIEW',
        'ACTION_LEFT_SIDESTEP', 'ACTION_RIGHT_SIDESTEP'
    )
    $originalKeys = (($actions | ForEach-Object {
        "define    $_    DOWN      KEY_F24"
    }) -join "`r`n") + "`r`n"
    $keys = Join-Path $keysDirectory 'keys.cfg'
    [System.IO.File]::WriteAllText($keys, $originalKeys, [System.Text.Encoding]::ASCII)
    $oldKeysHash = Get-TestHash $keys

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
