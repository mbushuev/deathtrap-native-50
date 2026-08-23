$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("deathtrap-installer-hash-warning-" + [guid]::NewGuid().ToString('N'))
$steamApps = Join-Path $testRoot 'steamapps'
$game = Join-Path $steamApps 'common\Deathtrap Dungeon'

try {
    New-Item -ItemType Directory -Path $game -Force | Out-Null
    [System.IO.File]::WriteAllText(
        (Join-Path $steamApps 'appmanifest_245010.acf'),
        '"AppState" { "appid" "245010" }')
    foreach ($name in @('Dungeon.dll', 'DD_CD.EXE', 'DDraw.dll', 'D3DImm.dll')) {
        [System.IO.File]::WriteAllText(
            (Join-Path $game $name), "deliberately unverified $name")
    }
    [System.IO.File]::WriteAllText(
        (Join-Path $game 'dgVoodoo.conf'),
        "[General]`r`nOutputAPI = d3d11_fl11_0`r`n")

    $output = & powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $PSScriptRoot 'install.ps1') `
        -GameDirectory $game `
        -DllPath (Join-Path $repoRoot 'dist\DINPUT.dll') `
        -IniPath (Join-Path $repoRoot 'config\deathtrap_native.ini') `
        -WhatIf 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "Installer rejected unverified hashes:`n$output"
    }
    foreach ($name in @('Dungeon.dll', 'DD_CD.EXE')) {
        if ($output -notmatch ([regex]::Escape(
                "$name does not match the version tested by the project"))) {
            throw "Installer did not warn for $name.`n$output"
        }
    }
    Write-Host 'Installer unknown-hash warning test passed.'
}
finally {
    $resolvedTestRoot = [System.IO.Path]::GetFullPath($testRoot)
    $temporaryRoot = [System.IO.Path]::GetFullPath(
        [System.IO.Path]::GetTempPath())
    if ($resolvedTestRoot.StartsWith(
            $temporaryRoot, [System.StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedTestRoot).StartsWith(
            'deathtrap-installer-hash-warning-',
            [System.StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force `
            -ErrorAction SilentlyContinue
    }
}
