[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PayloadDirectory,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [string]$BuildDirectory = 'build-installer'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$version = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'VERSION')).Trim()
$stage = (Resolve-Path -LiteralPath $PayloadDirectory).Path
$output = [System.IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $output) { throw "Installer output already exists: $output" }
$build = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    [System.IO.Path]::GetFullPath($BuildDirectory)
} else { [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory)) }
New-Item -ItemType Directory -Force -Path $build | Out-Null
$payload = Join-Path $build 'native50-payload.bin'
& python (Join-Path $PSScriptRoot 'build-installer-payload.py') --stage $stage --output $payload
if ($LASTEXITCODE -ne 0) { throw 'Installer payload validation failed.' }
. (Join-Path $PSScriptRoot 'resolve-cmake.ps1')
$cmake = Resolve-CMakeExecutable
# Normalize the case-duplicate PATH variables before MSBuild starts.
$processPath = $env:PATH
if ([string]::IsNullOrWhiteSpace($processPath)) { $processPath = $env:Path }
function Invoke-CMake([string[]]$Arguments) {
    $quoted = @($Arguments | ForEach-Object { '"' + $_.Replace('"', '""') + '"' })
    $command = 'set "MSBUILDDISABLENODEREUSE=1" & set "PATH=" & set "Path=" & set "PATH=' + $processPath + '" & "' + $cmake + '" ' + ($quoted -join ' ')
    & cmd.exe /d /s /c $command | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) { throw 'Native installer build failed.' }
}
Invoke-CMake @('-S', (Join-Path $repoRoot 'tools/installer'), '-B', $build,
    '-G', 'Visual Studio 17 2022', '-A', 'Win32',
    "-DPAYLOAD_FILE=$($payload.Replace('\', '/'))", "-DNATIVE50_VERSION=$version",
    "-DVERSION_COMMAS=$($version.Replace('.', ','))")
Invoke-CMake @('--build', $build, '--config', 'Release', '--parallel', '1', '--', '/nr:false')
& (Join-Path $build 'Release/installer_core_test.exe')
if ($LASTEXITCODE -ne 0) { throw 'Native installer rollback tests failed.' }
$exe = Join-Path $build 'Release/Native50Setup.exe'
$binary = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($exe))
# The resource includes pinned, unmodified vendor binaries. Their arbitrary
# packed bytes can resemble paths; inspect the installer itself separately.
$payloadText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($payload))
$binary = $binary.Replace($payloadText, '')
if ($binary -match '[A-Za-z]:\\[^\x00]{0,260}\.pdb') { throw 'Installer contains a local PDB path.' }
$verify = Start-Process -FilePath $exe -ArgumentList '--verify-payload' -Wait -PassThru -WindowStyle Hidden
if ($verify.ExitCode -ne 0) { throw 'Embedded installer payload verification failed.' }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $output) | Out-Null
Copy-Item -LiteralPath $exe -Destination $output
Write-Host "Windows installer: $output"
