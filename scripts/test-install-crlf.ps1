$ErrorActionPreference = 'Stop'

function Add-NativeBindingForTest {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Action,
        [Parameter(Mandatory = $true)][string]$Expression
    )

    $actionPattern = [regex]::Escape($Action)
    $expressionPattern = [regex]::Escape($Expression)
    $existingPattern = "(?m)^\s*define\s+$actionPattern\s+DOWN\s+$expressionPattern\s*\r?$"
    if ([regex]::IsMatch($Text, $existingPattern)) {
        return $Text
    }
    $anchorPattern = "(?m)^\s*define\s+$actionPattern\b[^\r\n]*\r?$"
    $matches = [regex]::Matches($Text, $anchorPattern)
    if ($matches.Count -eq 0) {
        throw "Missing test anchor: $Action"
    }
    $anchor = $matches[$matches.Count - 1]
    $insertAt = $anchor.Index + $anchor.Length
    if ($insertAt -gt 0 -and $Text[$insertAt - 1] -eq "`r") {
        --$insertAt
    }
    $newline = if ($Text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $line = "define    $Action    DOWN      $Expression"
    return $Text.Insert($insertAt, $newline + $line)
}

$sample = @(
    'define    ACTION_LEFT_SIDESTEP    DOWN      KEY_CTRL + KEY_A',
    'define    ACTION_RIGHT_SIDESTEP   DOWN      KEY_CTRL + KEY_D',
    ''
) -join "`r`n"

$sample = Add-NativeBindingForTest -Text $sample `
    -Action 'ACTION_LEFT_SIDESTEP' -Expression 'KEY_J'
$sample = Add-NativeBindingForTest -Text $sample `
    -Action 'ACTION_RIGHT_SIDESTEP' -Expression 'KEY_K'

if ($sample.Contains("`r`r`n")) {
    throw 'Installer binding insertion produced CR-CR-LF.'
}
if ($sample -notmatch '(?m)^define\s+ACTION_LEFT_SIDESTEP\s+DOWN\s+KEY_J\r?$') {
    throw 'KEY_J binding was not inserted.'
}
if ($sample -notmatch '(?m)^define\s+ACTION_RIGHT_SIDESTEP\s+DOWN\s+KEY_K\r?$') {
    throw 'KEY_K binding was not inserted.'
}

Write-Host 'Installer CRLF binding test passed.'
