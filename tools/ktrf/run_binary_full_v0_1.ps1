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

Write-Host "=== KTRF binary v0.1 / FINAL FULL CORE GATE ==="
Write-Host ""

Push-Location $Root
try {
    Write-Host "[1/5] Python syntax"
    Invoke-Checked python -m py_compile `
        .\tools\ktrf\binary_container_v0_1.py `
        .\tools\ktrf\binary_semantic_phase1_v0_1.py `
        .\tools\ktrf\binary_semantic_phase2_v0_1.py `
        .\tools\ktrf\binary_semantic_phase3_v0_1.py `
        .\tools\ktrf\binary_semantic_phase4_v0_1.py `
        .\tools\ktrf\binary_semantic_phase5_v0_1.py `
        .\tools\ktrf\binary_semantic_phase6_v0_1.py `
        .\tools\ktrf\binary_semantic_phase7_v0_1.py `
        .\tools\ktrf\binary_full_v0_1.py `
        .\tools\ktrf\compile_full_v0_1.py `
        .\tools\ktrf\validate_full_v0_1.py `
        .\tests\ktrf\test_binary_full_v0_1.py

    Write-Host ""
    Write-Host "[2/5] Container + phase-1..7 + final-core regression tests"
    Invoke-Checked python .\tests\ktrf\test_binary_container_v0_1.py
    Invoke-Checked python .\tests\ktrf\test_binary_semantic_phase1_v0_1.py
    Invoke-Checked python .\tests\ktrf\test_binary_semantic_phase2_v0_1.py
    Invoke-Checked python .\tests\ktrf\test_binary_semantic_phase3_v0_1.py
    Invoke-Checked python .\tests\ktrf\test_binary_semantic_phase4_v0_1.py
    Invoke-Checked python .\tests\ktrf\test_binary_semantic_phase5_v0_1.py
    Invoke-Checked python .\tests\ktrf\test_binary_semantic_phase6_v0_1.py
    Invoke-Checked python .\tests\ktrf\test_binary_semantic_phase7_v0_1.py
    Invoke-Checked python .\tests\ktrf\test_binary_full_v0_1.py

    Write-Host ""
    Write-Host "[3/5] Fresh frozen School Days oracle -> canonical IR"
    Invoke-Checked powershell -ExecutionPolicy Bypass -File .\tools\ktrf\run_sdhq_export.ps1

    Write-Host ""
    Write-Host "[4/5] Compile production-candidate School Days .ktnroute + manifest"
    Invoke-Checked python .\tools\ktrf\compile_full_v0_1.py `
        --ir .\build\ktrf\school-days-hq.routing.json `
        --output .\build\ktrf\school-days-hq.ktnroute `
        --manifest .\build\ktrf\school-days-hq.ktnroute.manifest.json

    Write-Host ""
    Write-Host "[5/5] Independent artifact validation + frozen oracle counts"
    Invoke-Checked python .\tools\ktrf\validate_full_v0_1.py `
        --input .\build\ktrf\school-days-hq.ktnroute `
        --manifest .\build\ktrf\school-days-hq.ktnroute.manifest.json `
        --expect META=1 `
        --expect NSPC=1 `
        --expect FEAT=8 `
        --expect ENTR=1 `
        --expect VARS=497 `
        --expect RSRC=1855 `
        --expect HOOK=3 `
        --expect ENDG=22 `
        --expect EXPR=1892 `
        --expect EFFT=11356 `
        --expect CHOI=287 `
        --expect NODE=1857 `
        --expect TRAN=2458

    Write-Host ""
    Write-Host "========================================================="
    Write-Host " KTRF BINARY v0.1 FULL CORE: PASS"
    Write-Host "========================================================="
    Write-Host "Artifact: $Root\build\ktrf\school-days-hq.ktnroute"
    Write-Host "Manifest: $Root\build\ktrf\school-days-hq.ktnroute.manifest.json"
}
finally {
    Pop-Location
}
