function Resolve-CMakeExecutable {
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $programFilesX86 = ${env:ProgramFiles(x86)}
    if ($programFilesX86) {
        $vswhere = Join-Path $programFilesX86 `
            'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
            $matches = @(& $vswhere -latest -products '*' `
                -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
            $match = $matches | Where-Object {
                $_ -and (Test-Path -LiteralPath $_ -PathType Leaf)
            } | Select-Object -First 1
            if ($match) {
                return $match
            }
        }
    }

    $roots = @($programFilesX86, $env:ProgramFiles) |
        Where-Object { $_ }
    $editions = @('BuildTools', 'Community', 'Professional', 'Enterprise')
    foreach ($root in $roots) {
        foreach ($edition in $editions) {
            $candidate = Join-Path $root `
                "Microsoft Visual Studio\2022\$edition\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                return $candidate
            }
        }
    }

    throw 'CMake was not found. Install Visual Studio 2022 C++ Build Tools or add cmake.exe to PATH.'
}
