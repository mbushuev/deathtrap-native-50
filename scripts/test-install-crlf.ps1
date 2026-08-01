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
    'define    ACTION_TURN_FAST_LEFT   DOWN      KEY_LSHIFT + KEY_A',
    'define    ACTION_JUMP_FORWARD     DOWN      KEY_SPACE + KEY_W',
    'define    ACTION_JUMP_BACKWARD    DOWN      KEY_SPACE + KEY_S',
    'define    ACTION_JUMP_LEFT        DOWN      KEY_SPACE + KEY_A',
    'define    ACTION_JUMP_RIGHT       DOWN      KEY_SPACE + KEY_D',
    ''
) -join "`r`n"

$bindings = @(
    @('ACTION_LEFT_SIDESTEP', 'KEY_J'),
    @('ACTION_RIGHT_SIDESTEP', 'KEY_K'),
    @('ACTION_TURN_FAST_LEFT', 'KEY_LSHIFT + JOY_HORIZ_LEFT'),
    @('ACTION_JUMP_FORWARD', 'KEY_SPACE + JOY_VERT_FORWARDS'),
    @('ACTION_JUMP_BACKWARD', 'KEY_SPACE + JOY_VERT_BACKWARDS'),
    @('ACTION_JUMP_LEFT', 'KEY_SPACE + JOY_HORIZ_LEFT'),
    @('ACTION_JUMP_RIGHT', 'KEY_SPACE + JOY_HORIZ_RIGHT'),
    @('ACTION_JUMP_LEFT', 'KEY_SPACE + KEY_J'),
    @('ACTION_JUMP_RIGHT', 'KEY_SPACE + KEY_K')
)
foreach ($binding in $bindings) {
    $sample = Add-NativeBindingForTest -Text $sample `
        -Action $binding[0] -Expression $binding[1]
}
# A second pass must not duplicate any binding.
foreach ($binding in $bindings) {
    $sample = Add-NativeBindingForTest -Text $sample `
        -Action $binding[0] -Expression $binding[1]
}

if ($sample.Contains("`r`r`n")) {
    throw 'Installer binding insertion produced CR-CR-LF.'
}
foreach ($binding in $bindings) {
    $actionPattern = [regex]::Escape($binding[0])
    $expressionPattern = [regex]::Escape($binding[1])
    $pattern = "(?m)^define\s+$actionPattern\s+DOWN\s+$expressionPattern\r?$"
    $count = [regex]::Matches($sample, $pattern).Count
    if ($count -ne 1) {
        throw "Expected exactly one $($binding[0]) / $($binding[1]) binding, found $count."
    }
}

Write-Host 'Installer CRLF binding test passed.'
