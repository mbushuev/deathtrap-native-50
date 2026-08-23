$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $repositoryRoot 'scripts\package-release.ps1') @args
exit $LASTEXITCODE
