$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $repositoryRoot 'build'
$visualStudioCMakeRoot =
    '<visual-studio-build-tools>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$cmake = Join-Path $visualStudioCMakeRoot 'cmake.exe'

if (-not (Test-Path -LiteralPath $cmake -PathType Leaf)) {
    throw "Visual Studio CMake was not found at the configured path: $cmake"
}

& $cmake -S $repositoryRoot -B $buildDirectory -A Win32
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $cmake --build $buildDirectory --config Release
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$releaseDirectory = Join-Path $buildDirectory 'Release'
& (Join-Path $releaseDirectory 'camera_spring_arm_test.exe')
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& (Join-Path $releaseDirectory 'dinput_proxy_smoke_test.exe') `
    (Join-Path $releaseDirectory 'dinput.dll')
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& (Join-Path $repositoryRoot 'scripts\test-install-crlf.ps1')
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$dll = Join-Path $releaseDirectory 'dinput.dll'
$distDirectory = Join-Path $repositoryRoot 'dist'
New-Item -ItemType Directory -Force -Path $distDirectory | Out-Null
Copy-Item -LiteralPath $dll `
    -Destination (Join-Path $distDirectory 'DINPUT.dll') -Force
$hash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
Write-Host "Built, verified and staged: $dll"
Write-Host "SHA-256: $hash"
exit 0
