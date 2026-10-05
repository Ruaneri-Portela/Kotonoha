param(
    [string]$Python = "python",
    [string]$BuildDir = "build-win",
    [int]$MaxCombinationsPerNode = 250000,
    [int]$MaxPairwisePerNode = 32,
    [int]$BatchSize = 500
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$exportRunner = Join-Path $repo "tools\ktrf\run_sdhq_export.ps1"
$matrixDiff = Join-Path $repo "tools\ktrf\diff_sdhq_state_matrix.py"
$targetedModule = Join-Path $repo "tools\ktrf\diff_sdhq_targeted_transitions.py"
$diffModule = Join-Path $repo "tools\ktrf\diff_sdhq_witnesses.py"
$interpreter = Join-Path $repo "tools\ktrf\sdhq_state_matrix_interpreter.py"
$ir = Join-Path $repo "build\ktrf\school-days-hq.routing.json"
$report = Join-Path $repo "build\ktrf\school-days-hq.state-matrix-diff.json"
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

Write-Host "=== KTRF / School Days broader state-matrix differential ==="
Write-Host ""

Write-Host "[1/4] Python syntax"
Run-Native $Python -m py_compile `
    $matrixDiff `
    $targetedModule `
    $diffModule `
    $interpreter

Write-Host ""
Write-Host "[2/4] Fresh School Days oracle -> validated KTRF IR"
Run-Native powershell -ExecutionPolicy Bypass -File $exportRunner -Python $Python

Write-Host ""
Write-Host "[3/4] Build C++ oracle trace adapter with test-only state access"
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
Write-Host "[4/4] Differential finite boundary state matrix"
Run-Native $Python $matrixDiff `
    --ir $ir `
    --oracle-exe $oracleExe `
    --interpreter $interpreter `
    --targeted-module $targetedModule `
    --diff-module $diffModule `
    --output $report `
    --max-combinations-per-node $MaxCombinationsPerNode `
    --max-pairwise-per-node $MaxPairwisePerNode `
    --batch-size $BatchSize

Write-Host ""
Write-Host "========================================================="
Write-Host " KTRF / SCHOOL DAYS BROADER STATE MATRIX: PASS"
Write-Host "========================================================="
Write-Host "Report: $report"
