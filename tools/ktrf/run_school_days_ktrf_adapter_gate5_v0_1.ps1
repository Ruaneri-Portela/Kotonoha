param(
    [string]$AssetsRoot = ""
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $root

if ([string]::IsNullOrWhiteSpace($AssetsRoot)) {
    $AssetsRoot = Join-Path $root "assets"
}
$AssetsRoot = [System.IO.Path]::GetFullPath($AssetsRoot)

function Invoke-Checked {
    param([scriptblock]$Command, [string]$Label)
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

Write-Host "=== School Days KTRF Adapter v0.1 / Gate 5 Engine Cutover ==="
Write-Host "assets=$AssetsRoot"
Write-Host ""

Write-Host "[1/3] Gameplay bridge regression (Gate 4)"
Invoke-Checked {
    powershell -ExecutionPolicy Bypass -File `
        .\tools\ktrf\run_school_days_ktrf_adapter_gate4_v0_1.ps1 `
        -AssetsRoot $AssetsRoot
} "Gate 4 regression"
Write-Host ""

Write-Host "[2/3] Gate 5 audit syntax"
Invoke-Checked {
    python -m py_compile .\tools\ktrf\audit_school_days_ktrf_engine_gate5.py
} "Gate 5 audit syntax"
Write-Host ""

Write-Host "[3/3] Verify exclusive KTRF/legacy SDL runtime cutover"
Invoke-Checked {
    python .\tools\ktrf\audit_school_days_ktrf_engine_gate5.py --root $root
} "Gate 5 engine cutover audit"
Write-Host ""

Write-Host "KTRF launch command:"
Write-Host "  Kotonoha.exe -K build\ktrf\school-days-hq.ktnroute `"$AssetsRoot`""
Write-Host ""
Write-Host "========================================================="
Write-Host " SCHOOL DAYS KTRF ADAPTER v0.1 / GATE 5: PASS"
Write-Host "========================================================="
