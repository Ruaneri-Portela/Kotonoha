param(
    [string]$Python = "python",
    [string]$BuildDir = "build-win",
    [int]$MaxCombinationsPerNode = 250000
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$baselineRunner = Join-Path $repo "tools\ktrf\run_sdhq_differential.ps1"
$targeted = Join-Path $repo "tools\ktrf\diff_sdhq_targeted_transitions.py"
$interpreter = Join-Path $repo "tools\ktrf\sdhq_injected_state_interpreter.py"
$diffModule = Join-Path $repo "tools\ktrf\diff_sdhq_witnesses.py"
$ir = Join-Path $repo "build\ktrf\school-days-hq.routing.json"
$coverage = Join-Path $repo "build\ktrf\school-days-hq.differential-coverage.json"
$output = Join-Path $repo "build\ktrf\school-days-hq.targeted-transition-diff.json"
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

Write-Host "=== KTRF / School Days targeted transition closure ==="
Write-Host ""

Write-Host "[1/3] Targeted differential syntax"
Run-Native $Python -m py_compile $targeted $interpreter

Write-Host ""
Write-Host "[2/3] Reconfirm witness differential + coverage gap"
Run-Native powershell -ExecutionPolicy Bypass -File $baselineRunner `
    -Python $Python `
    -BuildDir $BuildDir

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
    throw "SchoolDaysOracleTrace executable was not found after baseline runner."
}

Write-Host ""
Write-Host "[3/3] Target all witness-uncovered executable transitions"
Run-Native $Python $targeted `
    --ir $ir `
    --coverage $coverage `
    --oracle-exe $oracleExe `
    --interpreter $interpreter `
    --diff-module $diffModule `
    --output $output `
    --max-combinations-per-node $MaxCombinationsPerNode

Write-Host ""
Write-Host "=========================================================="
Write-Host " KTRF / SCHOOL DAYS TARGETED TRANSITION CLOSURE: PASS"
Write-Host "=========================================================="
Write-Host "Report: $output"
