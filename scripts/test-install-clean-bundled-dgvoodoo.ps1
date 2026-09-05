$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("deathtrap-installer-clean-dgvoodoo-" + [guid]::NewGuid().ToString('N'))
$game = Join-Path $testRoot 'Custom games\Dungeon copy'
$payload = Join-Path $game 'payload'
$asylum = Join-Path $game 'ASYLUM'

try {
    New-Item -ItemType Directory -Path (Join-Path $payload 'dgVoodoo') `
        -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $payload 'dxwrapper') `
        -Force | Out-Null
    New-Item -ItemType Directory -Path $asylum -Force | Out-Null
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
    Copy-Item -LiteralPath (Join-Path $repoRoot 'config\keys.cfg') `
        -Destination (Join-Path $payload 'keys.cfg')
    Copy-Item -LiteralPath (Join-Path $repoRoot 'config\dgVoodoo-recommended.conf') `
        -Destination (Join-Path $payload 'dgVoodoo.conf')
    Copy-Item -LiteralPath (Join-Path $repoRoot 'config\dxwrapper-dgvoodoo.ini') `
        -Destination (Join-Path $payload 'dxwrapper.ini')
    foreach ($wrapper in @('D3D9.dll', 'D3DImm.dll')) {
        Copy-Item -LiteralPath (Join-Path $repoRoot `
            "third_party\dgVoodoo2-2.86.2\x86\$wrapper") `
            -Destination (Join-Path $payload "dgVoodoo\$wrapper")
    }
    foreach ($wrapper in @('DDraw.dll', 'dxwrapper.dll')) {
        Copy-Item -LiteralPath (Join-Path $repoRoot `
            "third_party\deathtrap-dxwrapper-release225\x86\$wrapper") `
            -Destination (Join-Path $payload "dxwrapper\$wrapper")
    }

    # Missing inputs must fail before backups or patch files are written.
    foreach ($required in @('Dungeon.dll', 'DD_CD.EXE',
                             'ASYLUM\keys.cfg', 'ASYLUM\config.dat')) {
        $requiredPath = Join-Path $game $required
        $parkedPath = $requiredPath + '.test-missing'
        Move-Item -LiteralPath $requiredPath -Destination $parkedPath
        try {
            # Windows PowerShell 5 promotes redirected native stderr to an
            # error record. This child is expected to fail; inspect its exit
            # code and message instead of terminating the test on that record.
            $previousPreference = $ErrorActionPreference
            try {
                $ErrorActionPreference = 'Continue'
                $failure = & powershell.exe -NoProfile -ExecutionPolicy Bypass `
                    -File (Join-Path $game 'install.ps1') -GameDirectory $game `
                    -SkipGameHashCheck 2>&1 | Out-String
                $failureExitCode = $LASTEXITCODE
            } finally {
                $ErrorActionPreference = $previousPreference
            }
            if ($failureExitCode -eq 0 -or
                $failure -notmatch 'Required game file was not found') {
                throw "Missing-file validation failed for $required`: $failure"
            }
            if ((Test-Path -LiteralPath (Join-Path $game 'back')) -or
                (Test-Path -LiteralPath (Join-Path $game 'DINPUT.dll'))) {
                throw 'Invalid game folder was modified before validation.'
            }
        }
        finally {
            Move-Item -LiteralPath $parkedPath -Destination $requiredPath
        }
    }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $game 'install.ps1') -SkipGameHashCheck
    if ($LASTEXITCODE -ne 0) {
        throw "Clean installer exited with $LASTEXITCODE."
    }

    foreach ($wrapper in @('D3D9.dll', 'D3DImm.dll')) {
        $installed = (Get-FileHash -LiteralPath (Join-Path $game $wrapper) `
            -Algorithm SHA256).Hash
        $expected = (Get-FileHash -LiteralPath `
            (Join-Path $payload "dgVoodoo\$wrapper") -Algorithm SHA256).Hash
        if ($installed -ne $expected) {
            throw "Clean installation produced the wrong $wrapper."
        }
    }
    foreach ($wrapper in @('DDraw.dll', 'dxwrapper.dll')) {
        $installed = (Get-FileHash -LiteralPath (Join-Path $game $wrapper) `
            -Algorithm SHA256).Hash
        $expected = (Get-FileHash -LiteralPath `
            (Join-Path $payload "dxwrapper\$wrapper") -Algorithm SHA256).Hash
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
    if ((Get-FileHash -LiteralPath (Join-Path $game 'dxwrapper.ini') `
            -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath (Join-Path $payload 'dxwrapper.ini') `
            -Algorithm SHA256).Hash) {
        throw 'Clean installation produced the wrong dxwrapper.ini.'
    }
    $installedKeys = [System.IO.File]::ReadAllText(
        (Join-Path $asylum 'keys.cfg'))
    $expectedKeys = [regex]::Replace(
        [System.IO.File]::ReadAllText((Join-Path $payload 'keys.cfg')),
        "\r?\n", "`r`n")
    if (-not $expectedKeys.EndsWith("`r`n")) {
        $expectedKeys += "`r`n"
    }
    if ($installedKeys -cne $expectedKeys) {
        throw 'Clean installation did not install the exact bundled keys.cfg.'
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
