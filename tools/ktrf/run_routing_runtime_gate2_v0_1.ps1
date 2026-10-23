param()

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$NativeBuild = Join-Path $Root "build\ktrf\routing-runtime-v0.1"
$Artifact = Join-Path $Root "build\ktrf\school-days-hq.ktnroute"

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

Write-Host "=== KTRF Routing Runtime v0.1 / Gate 2 ==="
Write-Host ""

Push-Location $Root
try {
    Write-Host "[1/5] Fresh frozen School Days oracle -> canonical IR"
    Invoke-Checked powershell -ExecutionPolicy Bypass -File .\tools\ktrf\run_sdhq_export.ps1

    Write-Host ""
    Write-Host "[2/5] Compile production KTRF fixture"
    Invoke-Checked python .\tools\ktrf\compile_full_v0_1.py `
        --ir .\build\ktrf\school-days-hq.routing.json `
        --output .\build\ktrf\school-days-hq.ktnroute

    Write-Host ""
    Write-Host "[3/5] Configure standalone routing-runtime harness"
    Invoke-Checked cmake -S .\tests\ktrf_native -B $NativeBuild

    Write-Host ""
    Write-Host "[4/5] Build choice + transition routing harness"
    Invoke-Checked cmake --build $NativeBuild --config Release --target KtrfRoutingRuntimeGate2Test

    $Candidates = @(
        (Join-Path $NativeBuild "KtrfRoutingRuntimeGate2Test.exe"),
        (Join-Path $NativeBuild "Release\KtrfRoutingRuntimeGate2Test.exe"),
        (Join-Path $NativeBuild "KtrfRoutingRuntimeGate2Test"),
        (Join-Path $NativeBuild "Release\KtrfRoutingRuntimeGate2Test")
    )
    $TestExe = $Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $TestExe) {
        throw "KtrfRoutingRuntimeGate2Test executable not found under $NativeBuild"
    }

    Write-Host ""
    Write-Host "[5/5] Deferred CHOI + TRAN trigger/predicate/priority/effect execution"
    Invoke-Checked $TestExe $Artifact

    Write-Host ""
    Write-Host "========================================================="
    Write-Host " KTRF ROUTING RUNTIME v0.1 / GATE 2: PASS"
    Write-Host "========================================================="
    Write-Host "Artifact: $Artifact"
}
finally {
    Pop-Location
}
