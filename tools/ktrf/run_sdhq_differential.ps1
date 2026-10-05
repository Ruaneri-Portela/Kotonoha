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
$coverageAudit = Join-Path $repo "tools\ktrf\audit_sdhq_differential_coverage.py"
$ir = Join-Path $repo "build\ktrf\school-days-hq.routing.json"
$coverageReport = Join-Path $repo "build\ktrf\school-days-hq.differential-coverage.json"
$witnessSource = Join-Path $repo "tests\SchoolDaysFullRouterTest.cpp"
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

Write-Host "[1/6] Reference interpreter/differential syntax"
Run-Native $Python -m py_compile `
    $interpreter `
    $differential `
    $coverageAudit `
    $interpreterTests

Write-Host ""
Write-Host "[2/6] Reference interpreter regression tests"
Run-Native $Python $interpreterTests

Write-Host ""
Write-Host "[3/6] Fresh School Days oracle -> KTRF IR export"
Run-Native powershell -ExecutionPolicy Bypass -File $exportRunner -Python $Python

Write-Host ""
Write-Host "[4/6] Build compiled C++ oracle trace adapter"
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
Write-Host "[5/6] 22-witness step-by-step differential"
Run-Native $Python $differential `
    --ir $ir `
    --oracle-exe $oracleExe `
    --witness-source $witnessSource `
    --interpreter $interpreter

Write-Host ""
Write-Host "[6/6] Differential coverage audit"
Run-Native $Python $coverageAudit `
    --ir $ir `
    --witness-source $witnessSource `
    --output $coverageReport

Write-Host ""
Write-Host "=================================================="
Write-Host " KTRF / SCHOOL DAYS DIFFERENTIAL: PASS"
Write-Host "=================================================="
Write-Host "Coverage report: $coverageReport"
