param(
    [string]$Python = "python",
    [string]$BuildDir = "build-win"
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$interpreter = Join-Path $repo "tools\ktrf\interpreter.py"
$interpreterTests = Join-Path $repo "tests\ktrf\test_interpreter.py"
$exportRunner = Join-Path $repo "tools\ktrf\run_sdhq_export.ps1"
$differential = Join-Path $repo "tools\ktrf\diff_sdhq_witnesses.py"
$ir = Join-Path $repo "build\ktrf\school-days-hq.routing.json"
$buildPath = Join-Path $repo $BuildDir

function Run-Native {
    param(
        [Parameter(Mandatory=$true)][string]$Exe,
        [Parameter(ValueFromRemainingArguments=$true)][string[]]$Args
    )

    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $Exe @Args
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $oldPreference
    }

    if ($exitCode -ne 0) {
        throw "$Exe failed with exit code $exitCode"
    }
}

Write-Host "=== KTRF / School Days differential conformance ==="
Write-Host ""

Write-Host "[1/5] Reference interpreter syntax"
Run-Native $Python -m py_compile $interpreter $differential $interpreterTests

Write-Host ""
Write-Host "[2/5] Reference interpreter regression tests"
Run-Native $Python $interpreterTests

Write-Host ""
Write-Host "[3/5] Fresh School Days oracle -> KTRF IR export"
Run-Native powershell -ExecutionPolicy Bypass -File $exportRunner -Python $Python

Write-Host ""
Write-Host "[4/5] Build compiled C++ oracle trace adapter"
if (!(Test-Path -LiteralPath $buildPath -PathType Container)) {
    throw "Build directory not found: $buildPath. Configure it with KOTONOHA_ROUTER_TESTS=ON first."
}
Run-Native cmake --build $buildPath --config Debug --target SchoolDaysOracleTrace

$oracleCandidates = @(
    (Join-Path $buildPath "Debug\SchoolDaysOracleTrace.exe"),
    (Join-Path $buildPath "SchoolDaysOracleTrace.exe"),
    (Join-Path $buildPath "SchoolDaysOracleTrace")
)
$oracleExe = $null
foreach ($candidate in $oracleCandidates) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $oracleExe = (Resolve-Path $candidate).Path
        break
    }
}
if ($null -eq $oracleExe) {
    throw "SchoolDaysOracleTrace executable was not found after build."
}

Write-Host ""
Write-Host "[5/5] 22-witness step-by-step differential"
Run-Native $Python $differential `
    --ir $ir `
    --oracle-exe $oracleExe `
    --witness-source (Join-Path $repo "tests\SchoolDaysFullRouterTest.cpp") `
    --interpreter $interpreter

Write-Host ""
Write-Host "=================================================="
Write-Host " KTRF / SCHOOL DAYS DIFFERENTIAL: PASS"
Write-Host "=================================================="
