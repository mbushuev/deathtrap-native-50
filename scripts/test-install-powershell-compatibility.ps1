[CmdletBinding()]
param([string]$InstallerPath = '')

# This is a static guard, not execution on Windows 7/PowerShell 2.0.
# Command parameters are checked against Microsoft's PowerShell 2.0 inventory:
# https://github.com/PowerShell/PSScriptAnalyzer/blob/1.24.0/Engine/Settings/desktop-2.0-windows.json
$ErrorActionPreference = 'Stop'
if (-not $InstallerPath) {
    $InstallerPath = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) 'install.ps1'
}
$legacyParameters = @{
    'Add-Type' = 'TypeDefinition AssemblyName CodeDomProvider CompilerParameters Debug ErrorAction ErrorVariable IgnoreWarnings Language MemberDefinition Name Namespace OutBuffer OutVariable OutputAssembly OutputType PassThru Path ReferencedAssemblies UsingNamespace Verbose WarningAction WarningVariable'
    'Copy-Item' = 'Confirm Container Credential Debug Destination ErrorAction ErrorVariable Exclude Filter Force Include LiteralPath OutBuffer OutVariable PassThru Path Recurse UseTransaction Verbose WarningAction WarningVariable WhatIf'
    'Get-Date' = 'Date Day Debug DisplayHint ErrorAction ErrorVariable Format Hour Minute Month OutBuffer OutVariable Second UFormat Verbose WarningAction WarningVariable Year'
    'Join-Path' = 'Path ChildPath Resolve Credential Verbose Debug ErrorAction WarningAction ErrorVariable WarningVariable OutVariable OutBuffer'
    'Move-Item' = 'Confirm Credential Debug Destination ErrorAction ErrorVariable Exclude Filter Force Include LiteralPath OutBuffer OutVariable PassThru Path UseTransaction Verbose WarningAction WarningVariable WhatIf'
    'New-Item' = 'Confirm Credential Debug ErrorAction ErrorVariable Force ItemType Name OutBuffer OutVariable Path UseTransaction Value Verbose WarningAction WarningVariable WhatIf'
    'New-Object' = 'ArgumentList ComObject Debug ErrorAction ErrorVariable OutBuffer OutVariable Property Strict TypeName Verbose WarningAction WarningVariable'
    'Out-Null' = 'Debug ErrorAction ErrorVariable InputObject OutBuffer OutVariable Verbose WarningAction WarningVariable'
    'Resolve-Path' = 'Credential Debug ErrorAction ErrorVariable LiteralPath OutBuffer OutVariable Path Relative UseTransaction Verbose WarningAction WarningVariable'
    'Select-String' = 'AllMatches CaseSensitive Context Debug Encoding ErrorAction ErrorVariable Exclude Include InputObject List NotMatch OutBuffer OutVariable Path Pattern Quiet SimpleMatch Verbose WarningAction WarningVariable'
    'Split-Path' = 'Credential Debug ErrorAction ErrorVariable IsAbsolute Leaf LiteralPath NoQualifier OutBuffer OutVariable Parent Path Qualifier Resolve UseTransaction Verbose WarningAction WarningVariable'
    'Test-Path' = 'Credential Debug ErrorAction ErrorVariable Exclude Filter Include IsValid LiteralPath OutBuffer OutVariable Path PathType UseTransaction Verbose WarningAction WarningVariable'
    'Write-Host' = 'BackgroundColor Debug ErrorAction ErrorVariable ForegroundColor NoNewline Object OutBuffer OutVariable Separator Verbose WarningAction WarningVariable'
    'Write-Warning' = 'Debug ErrorAction ErrorVariable Message OutBuffer OutVariable Verbose WarningAction WarningVariable'
}

function Get-LegacyInstallerIssues([string]$Text) {
    $tokens = $null
    $errors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseInput(
        $Text, [ref]$tokens, [ref]$errors)
    foreach ($errorRecord in $errors) { "Parse: $($errorRecord.Message)" }
    $functions = @($ast.FindAll({ param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst]
    }, $true) | ForEach-Object { $_.Name })
    foreach ($node in $ast.FindAll({ param($item) $true }, $true)) {
        $kind = $node.GetType().Name
        if ($kind -eq 'NamedAttributeArgumentAst' -and $node.ExpressionOmitted) {
            "Line $($node.Extent.StartLineNumber): attribute requires an explicit value"
        }
        if ($kind -eq 'VariableExpressionAst' -and
                $node.VariablePath.UserPath -in @('PSScriptRoot', 'PSCommandPath')) {
            "Line $($node.Extent.StartLineNumber): newer automatic path variable"
        }
        if ($kind -eq 'InvokeMemberExpressionAst' -and $node.Static -and
                $node.Member.Value -eq 'new') {
            "Line $($node.Extent.StartLineNumber): newer constructor syntax"
        }
        if ($kind -eq 'BinaryExpressionAst' -and
                [string]$node.Operator -in @('Iin', 'Cin', 'Inotin', 'Cnotin')) {
            "Line $($node.Extent.StartLineNumber): newer membership operator"
        }
        if ($kind -ne 'CommandAst') { continue }
        $name = $node.GetCommandName()
        if ($functions -contains $name) { continue }
        if (-not $name -or -not $legacyParameters.ContainsKey($name)) {
            "Line $($node.Extent.StartLineNumber): command missing from legacy inventory: $name"
            continue
        }
        $parameters = $legacyParameters[$name] -split ' '
        foreach ($element in $node.CommandElements) {
            if ($element -is [System.Management.Automation.Language.CommandParameterAst] -and
                    $parameters -notcontains $element.ParameterName) {
                "Line $($element.Extent.StartLineNumber): $name -$($element.ParameterName) is not a PowerShell 2.0 parameter"
            }
        }
    }
}

# These regressions precede any game writes, so exercise each guard explicitly.
foreach ($regression in @(
    '[CmdletBinding(SupportsShouldProcess)] param()',
    '$directory = $PSScriptRoot',
    '$encoding = [System.Text.UTF8Encoding]::new($false)',
    'if ($hash -notin $known) {}',
    'Get-Content -LiteralPath "config.ini" -Raw',
    'Select-String -LiteralPath "config.ini" -Pattern "setting"',
    'Get-FileHash "game.dll"'
)) {
    if (@(Get-LegacyInstallerIssues $regression).Count -eq 0) {
        throw "Compatibility guard missed a known regression: $regression"
    }
}
$issues = @(Get-LegacyInstallerIssues ([System.IO.File]::ReadAllText($InstallerPath)))
if ($issues.Count) { throw ($issues -join "`n") }
Write-Host 'Installer legacy syntax and PowerShell 2.0 command-parameter checks passed (static).'
