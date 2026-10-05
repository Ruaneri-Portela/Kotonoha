param(
    [string]$Python = "python",
    [string]$BuildDir = "build-win"
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$genericRunner = Join-Path $repo "tools\ktrf\run_validation.ps1"
$exportRunner = Join-Path $repo "tools\ktrf\run_sdhq_export.ps1"
$witnessRunner = Join-Path $repo "tools\ktrf\run_sdhq_differential.ps1"
$targetedRunner = Join-Path $repo "tools\ktrf\run_sdhq_targeted_transition_diff.ps1"
$choiceRunner = Join-Path $repo "tools\ktrf\run_sdhq_choice_diff.ps1"
$effectRunner = Join-Path $repo "tools\ktrf\run_sdhq_effect_stress.ps1"
$matrixRunner = Join-Path $repo "tools\ktrf\run_sdhq_state_matrix.ps1"
$freezeTool = Join-Path $repo "tools\ktrf\freeze_sdhq_semantic_v0_1.py"

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

Write-Host "=== KTRF / School Days semantic freeze gate v0.1 ==="
Write-Host ""
Write-Host "This is intentionally a full rerun of the semantic conformance stack."
Write-Host "Generated freeze evidence is written under build\ktrf and is not committed automatically."
Write-Host ""

Write-Host "[1/8] Generic KTRF validation"
Run-Native powershell -ExecutionPolicy Bypass -File $genericRunner -Python $Python

Write-Host ""
Write-Host "[2/8] Fresh School Days oracle -> canonical IR + profile validation"
Run-Native powershell -ExecutionPolicy Bypass -File $exportRunner -Python $Python

Write-Host ""
Write-Host "[3/8] Causal 22-ending differential + witness coverage"
Run-Native powershell -ExecutionPolicy Bypass -File $witnessRunner `
    -Python $Python `
    -BuildDir $BuildDir

Write-Host ""
Write-Host "[4/8] Complete executable Transition differential closure"
Run-Native powershell -ExecutionPolicy Bypass -File $targetedRunner `
    -Python $Python `
    -BuildDir $BuildDir

Write-Host ""
Write-Host "[5/8] Exhaustive Choice / Feeling differential"
Run-Native powershell -ExecutionPolicy Bypass -File $choiceRunner `
    -Python $Python `
    -BuildDir $BuildDir

Write-Host ""
Write-Host "[6/8] Source Effect stress differential"
Run-Native powershell -ExecutionPolicy Bypass -File $effectRunner `
    -Python $Python `
    -BuildDir $BuildDir

Write-Host ""
Write-Host "[7/8] Broader finite boundary state matrix"
Run-Native powershell -ExecutionPolicy Bypass -File $matrixRunner `
    -Python $Python `
    -BuildDir $BuildDir `
    -MaxPairwisePerNode 32

Write-Host ""
Write-Host "[8/8] Deterministic semantic-freeze manifest"
Run-Native $Python $freezeTool `
    --repo $repo `
    --ir (Join-Path $repo "build\ktrf\school-days-hq.routing.json") `
    --output (Join-Path $repo "build\ktrf\school-days-hq.semantic-freeze-v0.1.json") `
    --sha256-output (Join-Path $repo "build\ktrf\school-days-hq.semantic-freeze-v0.1.sha256")

Write-Host ""
Write-Host "========================================================="
Write-Host " KTRF / SCHOOL DAYS SEMANTIC FREEZE GATE v0.1: PASS"
Write-Host "========================================================="
Write-Host "Manifest: $(Join-Path $repo 'build\ktrf\school-days-hq.semantic-freeze-v0.1.json')"
Write-Host "SHA-256:  $(Join-Path $repo 'build\ktrf\school-days-hq.semantic-freeze-v0.1.sha256')"
Write-Host ""
Write-Host "Do not tag/freeze the branch until the generated manifest and hash are reviewed."
