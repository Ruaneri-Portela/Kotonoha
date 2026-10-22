param()

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

function Invoke-Checked {
    param(
        [Parameter(Mandatory=$true)][string]$Exe,
        [Parameter(ValueFromRemainingArguments=$true)][string[]]$Args
    )
    & $Exe @Args
    if ($LASTEXITCODE -ne 0) {
        throw "$Exe failed with exit code $LASTEXITCODE"
    }
}

Write-Host "=== KTRF binary semantic tables v0.1 / phase 1 ==="
Write-Host ""

Push-Location $Root
try {
    Write-Host "[1/4] Python syntax"
    Invoke-Checked python -m py_compile `
        .\tools\ktrf\binary_container_v0_1.py `
        .\tools\ktrf\binary_semantic_phase1_v0_1.py `
        .\tools\ktrf\compile_phase1_v0_1.py `
        .\tests\ktrf\test_binary_container_v0_1.py `
        .\tests\ktrf\test_binary_semantic_phase1_v0_1.py

    Write-Host ""
    Write-Host "[2/4] Container + phase-1 regression tests"
    Invoke-Checked python .\tests\ktrf\test_binary_container_v0_1.py
    Invoke-Checked python .\tests\ktrf\test_binary_semantic_phase1_v0_1.py

    Write-Host ""
    Write-Host "[3/4] Fresh frozen School Days oracle -> canonical IR"
    Invoke-Checked powershell -ExecutionPolicy Bypass -File .\tools\ktrf\run_sdhq_export.ps1

    Write-Host ""
    Write-Host "[4/4] Compile + decode-check School Days phase-1 artifact"
    Invoke-Checked python .\tools\ktrf\compile_phase1_v0_1.py `
        --ir .\build\ktrf\school-days-hq.routing.json `
        --output .\build\ktrf\school-days-hq.phase1.ktnroute

    Write-Host ""
    Write-Host "========================================================="
    Write-Host " KTRF BINARY SEMANTIC PHASE 1 v0.1: PASS"
    Write-Host "========================================================="
    Write-Host "Artifact: $Root\build\ktrf\school-days-hq.phase1.ktnroute"
}
finally {
    Pop-Location
}
