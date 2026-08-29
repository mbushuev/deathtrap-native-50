$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$source = [System.IO.File]::ReadAllText(
    (Join-Path $repoRoot 'config\keys.cfg'))
$normalized = [regex]::Replace($source, "\r?\n", "`r`n")
if (-not $normalized.EndsWith("`r`n")) {
    $normalized += "`r`n"
}
if ($normalized.Contains("`r`r`n")) {
    throw 'Bundled keys.cfg normalization produced CR-CR-LF.'
}
foreach ($required in @(
    'define    ACTION_ATTACK_RANGED                        DOWN      KEY_F',
    'define    ACTION_ATTACK_1                             DOWN      KEY_F + KEY_W',
    'define    ACTION_ATTACK_2                             DOWN      KEY_F + KEY_A',
    'define    ACTION_ATTACK_3                             DOWN      KEY_F + KEY_D',
    'define    ACTION_ATTACK_BACK                          DOWN      KEY_F + KEY_A + KEY_D',
    'define    ACTION_PARRY                                DOWN      KEY_F + KEY_S'
)) {
    if (-not $normalized.Contains($required)) {
        throw "Bundled keys.cfg is missing required combat binding: $required"
    }
}
if ($normalized -match
        '(?m)^\s*define\s+ACTION_(?:ATTACK_[A-Z0-9_]+|PARRY)\s+DOWN\s+MOUSE_LBUTTON(?:\s|$)') {
    throw 'Bundled keys.cfg contains an obsolete direct left-mouse combat binding.'
}

Write-Host 'Bundled keys.cfg normalization test passed.'
