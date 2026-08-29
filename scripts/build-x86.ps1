param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = 'build'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))
. (Join-Path $PSScriptRoot 'resolve-cmake.ps1')
$cmake = Resolve-CMakeExecutable

& $cmake -S $repoRoot -B $buildPath -G 'Visual Studio 17 2022' -A Win32
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

& $cmake --build $buildPath --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

$dll = Join-Path $buildPath "$Configuration\dinput.dll"
$smoke = Join-Path $buildPath "$Configuration\dinput_proxy_smoke_test.exe"
$cameraSpring = Join-Path $buildPath "$Configuration\camera_spring_arm_test.exe"
$cameraRoom = Join-Path $buildPath "$Configuration\camera_room_collision_test.exe"
$immersiveFirstPerson = Join-Path $buildPath "$Configuration\immersive_first_person_test.exe"
$musicRouting = Join-Path $buildPath "$Configuration\music_track_routing_test.exe"
$mouseCombat = Join-Path $buildPath "$Configuration\mouse_combat_routing_test.exe"
$safeSave = Join-Path $buildPath "$Configuration\safe_save_test.exe"
if (-not (Test-Path -LiteralPath $dll)) { throw "Missing build output: $dll" }
if (-not (Test-Path -LiteralPath $smoke)) { throw "Missing smoke test: $smoke" }
if (-not (Test-Path -LiteralPath $cameraSpring)) { throw "Missing camera spring test: $cameraSpring" }
if (-not (Test-Path -LiteralPath $cameraRoom)) { throw "Missing camera room test: $cameraRoom" }
if (-not (Test-Path -LiteralPath $immersiveFirstPerson)) { throw "Missing immersive first-person test: $immersiveFirstPerson" }
if (-not (Test-Path -LiteralPath $musicRouting)) { throw "Missing music routing test: $musicRouting" }
if (-not (Test-Path -LiteralPath $mouseCombat)) { throw "Missing mouse combat test: $mouseCombat" }
if (-not (Test-Path -LiteralPath $safeSave)) { throw "Missing safe save test: $safeSave" }

& $smoke $dll
if ($LASTEXITCODE -ne 0) { throw 'DirectInput forwarding smoke test failed.' }

& $cameraSpring
if ($LASTEXITCODE -ne 0) { throw 'Camera spring-arm test failed.' }

& $cameraRoom
if ($LASTEXITCODE -ne 0) { throw 'Camera room-collision test failed.' }

& $immersiveFirstPerson
if ($LASTEXITCODE -ne 0) { throw 'Immersive first-person test failed.' }

& $musicRouting
if ($LASTEXITCODE -ne 0) { throw 'Music track routing test failed.' }

& $mouseCombat
if ($LASTEXITCODE -ne 0) { throw 'Mouse combat routing test failed.' }

& $safeSave
if ($LASTEXITCODE -ne 0) { throw 'Safe save eligibility test failed.' }

& (Join-Path $PSScriptRoot 'test-install-crlf.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Installer CRLF binding test failed.' }

& (Join-Path $PSScriptRoot 'test-install-hash-warning.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Installer hash warning test failed.' }

& (Join-Path $PSScriptRoot 'test-install-payload-upgrade.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Installer payload upgrade test failed.' }

& (Join-Path $PSScriptRoot 'test-install-clean-bundled-dgvoodoo.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Clean bundled dgVoodoo installation test failed.' }

$dist = Join-Path $repoRoot 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $dist 'DINPUT.dll') -Force
$hash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
Write-Host "Built and verified: $dll"
Write-Host "SHA-256: $hash"
