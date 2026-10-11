[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ArchivePath,
    [Parameter(Mandatory = $true)][hashtable]$GameDirectories,
    [string]$BuildDirectory = 'build'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$build = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))
$profileTest = Join-Path $build 'Release\controller_base_bindings_test.exe'
$smokeTest = Join-Path $build 'Release\dinput_proxy_smoke_test.exe'
$version = (Get-Content -LiteralPath (Join-Path $repoRoot 'VERSION') -Raw).Trim()
$profiles = @{
    steam = 'english'; gog_en = 'english'; gog_fr = 'gog_french'
    gog_de = 'gog_german'; gog_it = 'gog_italian'
}
foreach ($name in $profiles.Keys) {
    if (-not $GameDirectories.ContainsKey($name)) {
        throw "A real game directory is required for $name."
    }
}
foreach ($test in @($profileTest, $smokeTest)) {
    if (-not (Test-Path -LiteralPath $test -PathType Leaf)) {
        throw "Build the Release test executables first: $test"
    }
}

$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    ('deathtrap-release-matrix-' + [guid]::NewGuid().ToString('N'))
$patch = Join-Path $testRoot 'Extracted patch with spaces'
function Hash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}
function Check([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory(
        (Resolve-Path -LiteralPath $ArchivePath).Path, $patch)
    foreach ($name in @('steam', 'gog_en', 'gog_fr', 'gog_de', 'gog_it')) {
        $source = (Resolve-Path -LiteralPath $GameDirectories[$name]).Path
        $game = Join-Path $testRoot "Custom games\$name Dungeon copy"
        New-Item -ItemType Directory -Path (Join-Path $game 'ASYLUM') `
            -Force | Out-Null
        foreach ($file in @('DD_CD.EXE', 'Dungeon.dll', 'ASYLUM\keys.cfg',
                            'ASYLUM\config.dat', 'MSS32.DLL', 'WIN32.dll',
                            'MUSIC\track02.ogg')) {
            $original = Join-Path $source $file
            if (Test-Path -LiteralPath $original -PathType Leaf) {
                $destination = Join-Path $game $file
                New-Item -ItemType Directory -Force `
                    -Path (Split-Path -Parent $destination) | Out-Null
                Copy-Item -LiteralPath $original -Destination $destination
            }
        }
        # Steam covers updating an existing installation; the GOG fixtures
        # cover clean overlay installation, including untouched Miles DLLs.
        if ($name -eq 'steam') {
            foreach ($file in @('DINPUT.dll', 'deathtrap_native.ini', 'DDraw.dll',
                                'dxwrapper.dll', 'dxwrapper.ini', 'D3D9.dll',
                                'D3DImm.dll', 'dgVoodoo.conf')) {
                $original = Join-Path $source $file
                if (Test-Path -LiteralPath $original -PathType Leaf) {
                    Copy-Item -LiteralPath $original -Destination $game
                }
            }
        }
        $before = @{}
        Get-ChildItem -LiteralPath $game -File -Recurse | ForEach-Object {
            $relative = $_.FullName.Substring($game.Length + 1)
            $before[$relative] = Hash $_.FullName
        }
        $profile = & $profileTest (Join-Path $game 'Dungeon.dll') | Out-String
        Check ($LASTEXITCODE -eq 0 -and
            $profile.Contains("profile: $($profiles[$name]) passed")) `
            "Production compatibility gate failed for $name`: $profile"
        $output = & powershell.exe -NoProfile -ExecutionPolicy Bypass `
            -File (Join-Path $patch 'install.ps1') -GameDirectory $game |
            Out-String
        Check ($LASTEXITCODE -eq 0) "Packaged installer failed for $name`: $output"
        Check (-not $output.Contains('does not match the version tested')) `
            "Known $name game was reported as unknown: $output"
        Check ((Hash (Join-Path $game 'DD_CD.EXE')) -eq $before['DD_CD.EXE']) `
            "Installer changed the $name executable."
        Check ((Hash (Join-Path $game 'Dungeon.dll')) -eq $before['DUNGEON.DLL']) `
            "Installer changed the $name Dungeon.dll."
        $backup = @(Get-ChildItem -LiteralPath (Join-Path $game 'back') -Directory)
        Check ($backup.Count -eq 1) "Missing or ambiguous $name rollback copy."
        foreach ($file in @('ASYLUM\keys.cfg', 'ASYLUM\config.dat',
                            'DINPUT.dll', 'deathtrap_native.ini', 'DDraw.dll',
                            'dxwrapper.dll', 'dxwrapper.ini', 'D3D9.dll',
                            'D3DImm.dll', 'dgVoodoo.conf')) {
            if ($before.ContainsKey($file)) {
                $saved = Join-Path $backup[0].FullName ($file -replace '[\\/]', '_')
                Check ((Hash $saved) -eq $before[$file]) "Wrong $name backup: $file"
            }
        }
        foreach ($mapping in @(
            @('DINPUT.dll', 'DINPUT.dll'),
            @('DDraw.dll', 'dxwrapper\DDraw.dll'),
            @('dxwrapper.dll', 'dxwrapper\dxwrapper.dll'),
            @('dxwrapper.ini', 'dxwrapper.ini'),
            @('D3D9.dll', 'dgVoodoo\D3D9.dll'),
            @('D3DImm.dll', 'dgVoodoo\D3DImm.dll'),
            @('dgVoodoo.conf', 'dgVoodoo.conf')
        )) {
            Check ((Hash (Join-Path $game $mapping[0])) -eq
                (Hash (Join-Path $patch "payload\$($mapping[1])"))) `
                "Wrong installed $name runtime: $($mapping[0])"
        }
        $dllVersion = (Get-Item -LiteralPath (Join-Path $game 'DINPUT.dll')).VersionInfo
        Check ($dllVersion.ProductVersion -eq $version) "Wrong $name patch version."
        $ini = Get-Content -LiteralPath (Join-Path $game 'deathtrap_native.ini') -Raw
        foreach ($line in @('MovementThresholdPercent=45', 'RunThresholdPercent=90',
                            'VibrationOutputGainPercent=100', 'DebugLog=0')) {
            Check ($ini -match "(?m)^$([regex]::Escape($line))\s*$") `
                "Wrong $name default: $line"
        }
        $miles = Join-Path $game 'MSS32.DLL'
        if ($before.ContainsKey('MSS32.DLL') -and
            $before['MSS32.DLL'] -eq
                'CC7E8D381B21049175FF25F2F628347718DF7C8070661DFB31EC4C71FC47AB85') {
            Check ((Hash $miles) -eq
                '43C879A24FF746674A959B85C0223C73F92CCBD4BB7E81D81100BEEC232FB410') `
                "Localized $name music redirect failed."
            Check ((Hash (Join-Path $backup[0].FullName 'MSS32.DLL')) -eq
                $before['MSS32.DLL']) "Wrong original $name music DLL backup."
        }
        & $smokeTest (Join-Path $game 'DINPUT.dll')
        Check ($LASTEXITCODE -eq 0) "Installed $name DirectInput smoke test failed."
        Write-Host "$name`: profile, packaged install, runtime, rollback and DirectInput PASS"
    }
    Write-Host 'Windows release installation matrix passed (Steam + GOG EN/FR/DE/IT).'
}
finally {
    $resolved = [System.IO.Path]::GetFullPath($testRoot)
    $temporary = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    if ($resolved.StartsWith($temporary,
            [System.StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolved).StartsWith('deathtrap-release-matrix-')) {
        Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
    }
}
