param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = 'build'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))

$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
$cmake = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }
if (-not $cmake) {
    $candidates = @(
        '<visual-studio-build-tools>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        '<visual-studio-community>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        '<visual-studio-professional>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    )
    $cmake = $candidates | Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1
}
if (-not $cmake) {
    throw 'CMake was not found. Install Visual Studio 2022 C++ Build Tools or add cmake.exe to PATH.'
}

& $cmake -S $repoRoot -B $buildPath -G 'Visual Studio 17 2022' -A Win32
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

& $cmake --build $buildPath --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

$dll = Join-Path $buildPath "$Configuration\dinput.dll"
$smoke = Join-Path $buildPath "$Configuration\dinput_proxy_smoke_test.exe"
if (-not (Test-Path -LiteralPath $dll)) { throw "Missing build output: $dll" }
if (-not (Test-Path -LiteralPath $smoke)) { throw "Missing smoke test: $smoke" }

& $smoke $dll
if ($LASTEXITCODE -ne 0) { throw 'DirectInput forwarding smoke test failed.' }

$dist = Join-Path $repoRoot 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $dist 'DINPUT.dll') -Force
$hash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
Write-Host "Built and verified: $dll"
Write-Host "SHA-256: $hash"
