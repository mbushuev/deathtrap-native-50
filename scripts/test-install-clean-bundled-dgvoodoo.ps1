$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("deathtrap-installer-clean-dgvoodoo-" + [guid]::NewGuid().ToString('N'))
$steamApps = Join-Path $testRoot 'steamapps'
$game = Join-Path $steamApps 'common\Deathtrap Dungeon'
$payload = Join-Path $game 'payload'
$asylum = Join-Path $game 'ASYLUM'

try {
    New-Item -ItemType Directory -Path (Join-Path $payload 'dgVoodoo') `
        -Force | Out-Null
    New-Item -ItemType Directory -Path $asylum -Force | Out-Null
    [System.IO.File]::WriteAllText(
        (Join-Path $steamApps 'appmanifest_245010.acf'),
        '"AppState" { "appid" "245010" }')
    foreach ($name in @('Dungeon.dll', 'DD_CD.EXE')) {
        [System.IO.File]::WriteAllText((Join-Path $game $name), "test $name")
    }

    $keys = @(
        'define ACTION_WALK_FORWARD DOWN KEY_UP',
        'define ACTION_WALK_BACKWARD DOWN KEY_DOWN',
        'define ACTION_RUN_FORWARD DOWN KEY_A + KEY_UP',
        'define ACTION_RUN_BACKWARD DOWN KEY_A + KEY_DOWN',
        'define ACTION_STEP_FORWARD DOWN KEY_Z + KEY_UP',
        'define ACTION_STEP_BACKWARD DOWN KEY_Z + KEY_DOWN',
        'define ACTION_LEFT_SIDESTEP DOWN KEY_Z + KEY_LEFT',
        'define ACTION_RIGHT_SIDESTEP DOWN KEY_Z + KEY_RIGHT',
        'define ACTION_TURN_LEFT DOWN KEY_LEFT',
        'define ACTION_TURN_RIGHT DOWN KEY_RIGHT',
        'define ACTION_TURN_FAST_LEFT DOWN KEY_A + KEY_LEFT',
        'define ACTION_TURN_FAST_RIGHT DOWN KEY_A + KEY_RIGHT',
        'define ACTION_ATTACK_RANGED DOWN KEY_CAPS',
        'define ACTION_ATTACK_1 DOWN KEY_CAPS + KEY_UP',
        'define ACTION_ATTACK_2 DOWN KEY_CAPS + KEY_LEFT',
        'define ACTION_ATTACK_3 DOWN KEY_CAPS + KEY_RIGHT',
        'define ACTION_ATTACK_BACK DOWN KEY_CAPS + KEY_LEFT + KEY_RIGHT',
        'define ACTION_PARRY DOWN KEY_CAPS + KEY_DOWN',
        'define ACTION_CAST_SPELL DOWN KEY_S',
        'define ACTION_JUMP_CLIMB DOWN KEY_ENTER',
        'define ACTION_JUMP_LEFT DOWN KEY_ENTER + KEY_LEFT',
        'define ACTION_JUMP_RIGHT DOWN KEY_ENTER + KEY_RIGHT',
        'define ACTION_JUMP_FORWARD DOWN KEY_ENTER + KEY_UP',
        'define ACTION_JUMP_BACKWARD DOWN KEY_ENTER + KEY_DOWN',
        'define ACTION_1ST_PERSON_VIEW DOWN KEY_F24',
        'define ACTION_OPERATE PRESS KEY_SPACE'
    ) -join "`r`n"
    [System.IO.File]::WriteAllText(
        (Join-Path $asylum 'keys.cfg'), $keys + "`r`n",
        [System.Text.Encoding]::ASCII)
    [System.IO.File]::WriteAllText(
        (Join-Path $asylum 'config.dat'),
        "RENDERING_PLATFORM 0`r`nD3D_ALLOW_MIPMAP 0`r`n",
        [System.Text.Encoding]::ASCII)

    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'install.ps1') `
        -Destination (Join-Path $game 'install.ps1')
    Copy-Item -LiteralPath (Join-Path $repoRoot 'dist\DINPUT.dll') `
        -Destination (Join-Path $payload 'DINPUT.dll')
    Copy-Item -LiteralPath (Join-Path $repoRoot 'config\deathtrap_native.ini') `
        -Destination (Join-Path $payload 'deathtrap_native.ini')
    Copy-Item -LiteralPath (Join-Path $repoRoot 'config\dgVoodoo-recommended.conf') `
        -Destination (Join-Path $payload 'dgVoodoo.conf')
    foreach ($wrapper in @('DDraw.dll', 'D3DImm.dll')) {
        Copy-Item -LiteralPath (Join-Path $repoRoot `
            "third_party\dgVoodoo2-2.86.2\x86\$wrapper") `
            -Destination (Join-Path $payload "dgVoodoo\$wrapper")
    }

    & powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $game 'install.ps1') -SkipGameHashCheck
    if ($LASTEXITCODE -ne 0) {
        throw "Clean installer exited with $LASTEXITCODE."
    }

    foreach ($wrapper in @('DDraw.dll', 'D3DImm.dll')) {
        $installed = (Get-FileHash -LiteralPath (Join-Path $game $wrapper) `
            -Algorithm SHA256).Hash
        $expected = (Get-FileHash -LiteralPath `
            (Join-Path $payload "dgVoodoo\$wrapper") -Algorithm SHA256).Hash
        if ($installed -ne $expected) {
            throw "Clean installation produced the wrong $wrapper."
        }
    }
    $installedConfig = (Get-FileHash -LiteralPath `
        (Join-Path $game 'dgVoodoo.conf') -Algorithm SHA256).Hash
    $expectedConfig = (Get-FileHash -LiteralPath `
        (Join-Path $payload 'dgVoodoo.conf') -Algorithm SHA256).Hash
    if ($installedConfig -ne $expectedConfig) {
        throw 'Clean installation produced the wrong dgVoodoo.conf.'
    }
    Write-Host 'Clean bundled dgVoodoo installation test passed.'
}
finally {
    $resolved = [System.IO.Path]::GetFullPath($testRoot)
    $temporary = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    if ($resolved.StartsWith(
            $temporary, [System.StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolved).StartsWith(
            'deathtrap-installer-clean-dgvoodoo-',
            [System.StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force `
            -ErrorAction SilentlyContinue
    }
}
