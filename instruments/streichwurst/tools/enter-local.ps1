# Dot-source from PowerShell; modifies PATH for this process only.
$ErrorActionPreference = 'Stop'
$streichwurstRepo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../..')).Path
$streichwurstGitCommon = & git -C $streichwurstRepo rev-parse --path-format=absolute --git-common-dir
if ($LASTEXITCODE -ne 0) { throw 'Cannot locate the shared Daisy-Seed checkout.' }
$streichwurstCheckout = Split-Path -Parent $streichwurstGitCommon
$streichwurstTools = Join-Path $streichwurstCheckout '.tools'
$streichwurstBins = @(
    (Join-Path $streichwurstTools 'xpack-arm-none-eabi-gcc-13.2.1-1.1/bin'),
    (Join-Path $streichwurstTools 'xpack-windows-build-tools-4.4.1-3/bin'),
    (Join-Path $streichwurstTools 'llvm-mingw-20261006-ucrt-x86_64/bin')
)
foreach ($streichwurstBin in $streichwurstBins) {
    if (-not (Test-Path -LiteralPath $streichwurstBin -PathType Container)) {
        throw "Existing local toolchain missing: $streichwurstBin. See docs/BUILD.md."
    }
}
$env:PATH = ($streichwurstBins -join [IO.Path]::PathSeparator) + [IO.Path]::PathSeparator + $env:PATH
Write-Output "Streichwurst tools ready in this shell. Repository: $streichwurstRepo"
Write-Output 'Host C++: use HOST_CXX=clang++ when the project Makefile exists.'
