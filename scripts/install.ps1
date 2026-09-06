[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$GameDirectory = '',
    [string]$DllPath = '',
    [string]$IniPath = '',
    [string]$KeysPath = '',
    [string]$DxWrapperRuntimeDirectory = '',
    [string]$DxWrapperConfigPath = '',
    [string]$DgVoodooRuntimeDirectory = '',
    [string]$DgVoodooConfigPath = '',
    [switch]$SkipGameHashCheck
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $GameDirectory) {
    $localExecutable = Join-Path $PSScriptRoot 'DD_CD.EXE'
    if (-not (Test-Path -LiteralPath $localExecutable -PathType Leaf)) {
        throw 'GameDirectory is required unless install.ps1 is beside DD_CD.EXE.'
    }
    $GameDirectory = $PSScriptRoot
}
$game = (Resolve-Path -LiteralPath $GameDirectory).Path
$dungeon = Join-Path $game 'Dungeon.dll'
$executable = Join-Path $game 'DD_CD.EXE'
$expectedDungeon = '95FE9CE0FFF387F00704548F152E4340815213FCB3833DBE1B5C42871E7D2E56'
$expectedExecutable = '0C644A00E62652E046C5DAD2960F0F6C8C1998F4CA065780FBD7811D9908BF1F'
# Validate only the files consumed by this installer, before any writes.
# The game may be copied anywhere; Steam's layout/manifest is not required.
foreach ($required in @('Dungeon.dll', 'DD_CD.EXE',
                         'ASYLUM\keys.cfg', 'ASYLUM\config.dat')) {
    $requiredPath = Join-Path $game $required
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required game file was not found: $requiredPath"
    }
}

function Get-FileSha256 {
    param([Parameter(Mandatory = $true)][string]$Path)

    # Use the .NET stream directly so the installer's -WhatIf preference can't
    # suppress read-only hashing through the FileSystem provider.
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $hasher = [System.Security.Cryptography.SHA256]::Create()
        try {
            return ([System.BitConverter]::ToString(
                $hasher.ComputeHash($stream))).Replace('-', '')
        }
        finally {
            $hasher.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

if (-not $SkipGameHashCheck) {
    foreach ($binary in @(
        @{ Name = 'Dungeon.dll'; Path = $dungeon; Expected = $expectedDungeon },
        @{ Name = 'DD_CD.EXE'; Path = $executable; Expected = $expectedExecutable }
    )) {
        $actual = Get-FileSha256 -Path $binary.Path
        if ($actual -ne $binary.Expected) {
            Write-Warning (
                "$($binary.Name) does not match the version tested by the " +
                "project. Expected SHA-256: $($binary.Expected); actual: " +
                "$actual. Installation will continue, but the patch may be " +
                'partially or completely incompatible with these game files.')
        }
    }
}

if (-not $DgVoodooRuntimeDirectory) {
    $payloadRuntime = Join-Path $PSScriptRoot 'payload\dgVoodoo'
    $repositoryRuntime = Join-Path $repoRoot 'third_party\dgVoodoo2-2.86.2\x86'
    $DgVoodooRuntimeDirectory = if (
        (Test-Path -LiteralPath (Join-Path $payloadRuntime 'D3D9.dll') -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $payloadRuntime 'D3DImm.dll') -PathType Leaf)
    ) {
        $payloadRuntime
    } else {
        $repositoryRuntime
    }
}
$dgRuntime = (Resolve-Path -LiteralPath $DgVoodooRuntimeDirectory).Path
$dgVoodooPayload = @(
    @{
        Name = 'D3DImm.dll'
        Sha256 = '8B2850D0AF5F07CF2928AC9666192C3ADCB0290F10ED8F942F1594F3A4F51C73'
    },
    @{
        Name = 'D3D9.dll'
        Sha256 = 'D8D2E15BF5D0E01C89317A733492997DE8F7F562A972FE564FD17D194EE5D1F3'
    }
)
foreach ($wrapper in $dgVoodooPayload) {
    $path = Join-Path $dgRuntime $wrapper.Name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Bundled dgVoodoo runtime file is missing: $path"
    }
    $actualHash = Get-FileSha256 -Path $path
    if ($actualHash -ne $wrapper.Sha256) {
        throw "Bundled dgVoodoo runtime file failed verification: $($wrapper.Name)"
    }
}
if (-not $DxWrapperRuntimeDirectory) {
    $payloadRuntime = Join-Path $PSScriptRoot 'payload\dxwrapper'
    $repositoryRuntime = Join-Path $repoRoot `
        'third_party\deathtrap-dxwrapper-release225\x86'
    $DxWrapperRuntimeDirectory = if (
        (Test-Path -LiteralPath (Join-Path $payloadRuntime 'DDraw.dll') -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $payloadRuntime 'dxwrapper.dll') -PathType Leaf)
    ) {
        $payloadRuntime
    } else {
        $repositoryRuntime
    }
}
$dxRuntime = (Resolve-Path -LiteralPath $DxWrapperRuntimeDirectory).Path
$dxWrapperPayload = @(
    @{
        Name = 'DDraw.dll'
        Sha256 = '8BAE794EB7506711F57B690CFB8660A5F008B0185E764F8CB56D5972E01A9F33'
    },
    @{
        Name = 'dxwrapper.dll'
        Sha256 = 'A01EC795A633643CEA61A7CDC62EE97793D1674733D31E8240CE98C33E145841'
    }
)
foreach ($wrapper in $dxWrapperPayload) {
    $path = Join-Path $dxRuntime $wrapper.Name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Bundled Deathtrap dxwrapper runtime file is missing: $path"
    }
    $actualHash = Get-FileSha256 -Path $path
    if ($actualHash -ne $wrapper.Sha256) {
        throw "Bundled Deathtrap dxwrapper file failed verification: $($wrapper.Name)"
    }
}
if (-not $DxWrapperConfigPath) {
    $payloadConfig = Join-Path $PSScriptRoot 'payload\dxwrapper.ini'
    $DxWrapperConfigPath = if (Test-Path -LiteralPath $payloadConfig -PathType Leaf) {
        $payloadConfig
    } else {
        Join-Path $repoRoot 'config\dxwrapper-dgvoodoo.ini'
    }
}
$dxConfigSource = (Resolve-Path -LiteralPath $DxWrapperConfigPath).Path
foreach ($requiredSetting in @(
    '^Dd7to9\s*=\s*1\s*$',
    '^DdrawUseExternalD3D9\s*=\s*1\s*$',
    '^DdrawInternalResolutionScale\s*=\s*[234]\s*$',
    '^DdrawWidescreenAspectX1000\s*=\s*1\s*$'
)) {
    if (-not (Select-String -LiteralPath $dxConfigSource `
            -Pattern $requiredSetting -Quiet)) {
        throw "Bundled dxwrapper configuration is missing: $requiredSetting"
    }
}
if (-not $DgVoodooConfigPath) {
    $payloadConfig = Join-Path $PSScriptRoot 'payload\dgVoodoo.conf'
    $DgVoodooConfigPath = if (Test-Path -LiteralPath $payloadConfig -PathType Leaf) {
        $payloadConfig
    } else {
        Join-Path $repoRoot 'config\dgVoodoo-recommended.conf'
    }
}
$dgConfigSource = (Resolve-Path -LiteralPath $DgVoodooConfigPath).Path
if (-not (Select-String -LiteralPath $dgConfigSource `
        -Pattern '^OutputAPI\s*=\s*d3d11_fl11_0\s*$' -Quiet)) {
    throw 'Bundled dgVoodoo configuration must use OutputAPI = d3d11_fl11_0.'
}
$dgConfigText = Get-Content -LiteralPath $dgConfigSource -Raw
if ($dgConfigText -notmatch `
        '(?ms)^\[DirectX\]\s*$.*?^Antialiasing\s*=\s*8x\s*$') {
    throw 'Bundled dgVoodoo DirectX configuration must use Antialiasing = 8x.'
}

if (-not $DllPath) {
    $payloadDll = Join-Path $PSScriptRoot 'payload\DINPUT.dll'
    $flatDll = Join-Path $PSScriptRoot 'DINPUT.dll'
    $DllPath = if (Test-Path -LiteralPath $payloadDll -PathType Leaf) {
        $payloadDll
    } elseif (Test-Path -LiteralPath $flatDll -PathType Leaf) {
        $flatDll
    } else {
        Join-Path $repoRoot 'dist\DINPUT.dll'
    }
}
$dll = (Resolve-Path -LiteralPath $DllPath).Path
if (-not $IniPath) {
    $payloadIni = Join-Path $PSScriptRoot 'payload\deathtrap_native.ini'
    $flatIni = Join-Path $PSScriptRoot 'deathtrap_native.ini'
    $IniPath = if (Test-Path -LiteralPath $payloadIni -PathType Leaf) {
        $payloadIni
    } elseif (Test-Path -LiteralPath $flatIni -PathType Leaf) {
        $flatIni
    } else {
        Join-Path $repoRoot 'config\deathtrap_native.ini'
    }
}
$ini = (Resolve-Path -LiteralPath $IniPath).Path
if (-not $KeysPath) {
    $payloadKeys = Join-Path $PSScriptRoot 'payload\keys.cfg'
    $flatKeys = Join-Path $PSScriptRoot 'keys.cfg'
    $KeysPath = if (Test-Path -LiteralPath $payloadKeys -PathType Leaf) {
        $payloadKeys
    } elseif (Test-Path -LiteralPath $flatKeys -PathType Leaf) {
        $flatKeys
    } else {
        Join-Path $repoRoot 'config\keys.cfg'
    }
}
$keysSource = (Resolve-Path -LiteralPath $KeysPath).Path
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $game "back\deathtrap-native50-overlay-$stamp"
$keys = Join-Path $game 'ASYLUM\keys.cfg'
$retailConfig = Join-Path $game 'ASYLUM\config.dat'

function Set-RetailConfigValue {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Value
    )

    $newline = if ($Text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $namePattern = [regex]::Escape($Name)
    # The retail DDCONFIG utility can append its first Direct3D field without
    # a newline (for example, "RESOLUTION 5RENDERING_PLATFORM 0").  Split that
    # malformed boundary before normalizing the setting.
    $Text = [regex]::Replace(
        $Text, "(?m)(?<=\S)(?=$namePattern\s+)", $newline)
    $hadTerminalNewline = $Text.EndsWith("`n")
    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($existingLine in [regex]::Split($Text, "\r?\n")) {
        $lines.Add($existingLine)
    }
    if ($hadTerminalNewline -and $lines.Count -gt 0 -and
            $lines[$lines.Count - 1] -eq '') {
        $lines.RemoveAt($lines.Count - 1)
    }

    $pattern = "^\s*$namePattern(?:\s+.*)?\s*$"
    $rewritten = [System.Collections.Generic.List[string]]::new()
    $inserted = $false
    foreach ($existingLine in $lines) {
        if ([regex]::IsMatch($existingLine, $pattern)) {
            if (-not $inserted) {
                $rewritten.Add("$Name $Value")
                $inserted = $true
            }
            continue
        }
        $rewritten.Add($existingLine)
    }
    if (-not $inserted) {
        $rewritten.Add("$Name $Value")
    }

    $result = [string]::Join($newline, $rewritten)
    if ($hadTerminalNewline -or $Text.Length -eq 0) {
        $result += $newline
    }
    return $result
}

function Get-NativePrimaryDisplayResolution {
    # EnumDisplaySettings reports the physical current mode. Unlike
    # SystemParameters/WinForms dimensions, it is not virtualized by the
    # desktop's 125/150/200 percent DPI scale.
    if (-not ('DeathtrapInstaller.DisplayMode' -as [type])) {
        Add-Type @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace DeathtrapInstaller {
    public static class DisplayMode {
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        private struct PointL {
            public int X;
            public int Y;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        private struct DevMode {
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
            public string DeviceName;
            public short SpecVersion;
            public short DriverVersion;
            public short Size;
            public short DriverExtra;
            public int Fields;
            public PointL Position;
            public int DisplayOrientation;
            public int DisplayFixedOutput;
            public short Color;
            public short Duplex;
            public short YResolution;
            public short TTOption;
            public short Collate;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
            public string FormName;
            public short LogPixels;
            public int BitsPerPel;
            public int PelsWidth;
            public int PelsHeight;
            public int DisplayFlags;
            public int DisplayFrequency;
            public int ICMMethod;
            public int ICMIntent;
            public int MediaType;
            public int DitherType;
            public int Reserved1;
            public int Reserved2;
            public int PanningWidth;
            public int PanningHeight;
        }

        [DllImport("user32.dll", CharSet = CharSet.Ansi)]
        private static extern bool EnumDisplaySettings(
            string deviceName, int modeNumber, ref DevMode mode);

        public static int[] CurrentPrimary() {
            DevMode mode = new DevMode();
            mode.Size = (short)Marshal.SizeOf(mode);
            if (!EnumDisplaySettings(null, -1, ref mode) ||
                    mode.PelsWidth < 640 || mode.PelsHeight < 480) {
                throw new Win32Exception(
                    "Unable to read the primary monitor's current mode.");
            }
            return new int[] { mode.PelsWidth, mode.PelsHeight };
        }
    }
}
'@
    }

    $resolution = [DeathtrapInstaller.DisplayMode]::CurrentPrimary()
    return @{
        Width = [int]$resolution[0]
        Height = [int]$resolution[1]
    }
}

function Set-NativeDisplayResolution {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][int]$Width,
        [Parameter(Mandatory = $true)][int]$Height
    )

    $text = [System.IO.File]::ReadAllText($Path)
    foreach ($setting in @(
        @('WindowWidth', $Width),
        @('WindowHeight', $Height)
    )) {
        $pattern = '(?m)^(\s*' + [regex]::Escape($setting[0]) +
            '\s*=\s*)\d+\s*$'
        if (-not [regex]::IsMatch($text, $pattern)) {
            throw "Display setting was not found in ${Path}: $($setting[0])"
        }
        $text = [regex]::Replace(
            $text, $pattern, '${1}' + [string]$setting[1], 1)
    }
    [System.IO.File]::WriteAllText(
        $Path, $text, [System.Text.UTF8Encoding]::new($false))
}

if ($PSCmdlet.ShouldProcess($game, 'Install Deathtrap Native 50 overlay')) {
    New-Item -ItemType Directory -Force -Path $backup | Out-Null
    foreach ($existing in @(
        'DINPUT.dll',
        'deathtrap_native.ini',
        'DDraw.dll',
        'dxwrapper.dll',
        'dxwrapper.ini',
        'D3D9.dll',
        'D3DImm.dll',
        'dgVoodoo.conf',
        'ASYLUM\keys.cfg',
        'ASYLUM\config.dat'
    )) {
        $path = Join-Path $game $existing
        if (Test-Path -LiteralPath $path) {
            $backupName = $existing -replace '[\\/]', '_'
            Copy-Item -LiteralPath $path -Destination (Join-Path $backup $backupName) -Force
        }
    }
    $logsDirectory = Join-Path $game 'logs'
    New-Item -ItemType Directory -Force -Path $logsDirectory | Out-Null
    foreach ($legacyLog in @('deathtrap_native_render.log', 'deathtrap_native_present.log')) {
        $legacyPath = Join-Path $game $legacyLog
        if (Test-Path -LiteralPath $legacyPath) {
            $legacyBase = [System.IO.Path]::GetFileNameWithoutExtension($legacyLog)
            $legacyDestination = Join-Path $logsDirectory "$legacyBase-legacy-$stamp.log"
            Move-Item -LiteralPath $legacyPath -Destination $legacyDestination
        }
    }
    $dllDestination = Join-Path $game 'DINPUT.dll'
    $iniDestination = Join-Path $game 'deathtrap_native.ini'
    if (-not $dll.Equals($dllDestination,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        Copy-Item -LiteralPath $dll -Destination $dllDestination -Force
    }
    if (-not $ini.Equals($iniDestination,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        Copy-Item -LiteralPath $ini -Destination $iniDestination -Force
    }
    $nativeDisplay = Get-NativePrimaryDisplayResolution
    Set-NativeDisplayResolution -Path $iniDestination `
        -Width $nativeDisplay.Width -Height $nativeDisplay.Height
    foreach ($wrapper in $dgVoodooPayload) {
        Copy-Item -LiteralPath (Join-Path $dgRuntime $wrapper.Name) `
            -Destination (Join-Path $game $wrapper.Name) -Force
    }
    foreach ($wrapper in $dxWrapperPayload) {
        Copy-Item -LiteralPath (Join-Path $dxRuntime $wrapper.Name) `
            -Destination (Join-Path $game $wrapper.Name) -Force
    }
    Copy-Item -LiteralPath $dxConfigSource `
        -Destination (Join-Path $game 'dxwrapper.ini') -Force
    Copy-Item -LiteralPath $dgConfigSource `
        -Destination (Join-Path $game 'dgVoodoo.conf') -Force
    if (-not (Test-Path -LiteralPath $keys)) {
        throw "Retail control file was not found: $keys"
    }
    if (-not (Test-Path -LiteralPath $retailConfig)) {
        throw "Retail rendering configuration was not found: $retailConfig"
    }
    # The DLL's modern input routing and the action table are one tested unit.
    # Back up the user's previous file above, then install the exact profile
    # shipped with this build instead of trying to merge arbitrary remaps.
    $keyText = [System.IO.File]::ReadAllText($keysSource)
    $keyText = [regex]::Replace($keyText, "\r?\n", "`r`n")
    if (-not $keyText.EndsWith("`r`n")) {
        $keyText += "`r`n"
    }
    if ($keyText.Contains("`r`r`n")) {
        throw 'Bundled keys.cfg normalized to invalid CR-CR-LF line endings.'
    }
    [System.IO.File]::WriteAllText(
        $keys, $keyText, [System.Text.Encoding]::ASCII)

    $configText = [System.IO.File]::ReadAllText($retailConfig)
    foreach ($setting in @(
        @('RENDERING_PLATFORM', '13'),
        @('D3D_ALLOW_MIPMAP', '1'),
        @('D3D_ALLOW_PALETTISED', '0'),
        @('D3D_TYPE1_SHADOWS', '1')
    )) {
        $configText = Set-RetailConfigValue -Text $configText `
            -Name $setting[0] -Value $setting[1]
    }
    if ($configText.Contains("`r`r`n")) {
        throw 'Refusing to write ASYLUM/config.dat with invalid CR-CR-LF line endings.'
    }
    [System.IO.File]::WriteAllText(
        $retailConfig, $configText, [System.Text.Encoding]::ASCII)
    Write-Host "Installed overlay into: $game"
    Write-Host 'Installed the tested native-canvas Dd7to9 layer and configuration.'
    Write-Host 'Installed dgVoodoo 2.86.2 x86 D3D9 as the final D3D11 backend.'
    Write-Host 'Installed the modern keyboard, mouse and XInput control profile.'
    Write-Host (
        'Configured the current primary monitor resolution: ' +
        "$($nativeDisplay.Width)x$($nativeDisplay.Height).")
    Write-Host 'Applied the required Direct3D rendering profile.'
    Write-Host "Rollback copy: $backup"
}
