param()

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$NativeBuild = Join-Path $Root "build\ktrf\routing-runtime-gate3-v0.1"
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

function Resolve-BuiltExe {
    param([Parameter(Mandatory=$true)][string]$Name)
    $Candidates = @(
        (Join-Path $NativeBuild "$Name.exe"),
        (Join-Path $NativeBuild "Release\$Name.exe"),
        (Join-Path $NativeBuild $Name),
        (Join-Path $NativeBuild "Release\$Name")
    )
    $Found = $Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $Found) {
        throw "$Name executable not found under $NativeBuild"
    }
    return $Found
}

Write-Host "=== KTRF Routing Runtime v0.1 / Gate 3 Native Differential ==="
Write-Host ""

Push-Location $Root
try {
    Write-Host "[1/6] Python syntax"
    Invoke-Checked python -m py_compile .\tools\ktrf\diff_sdhq_native_witnesses.py

    Write-Host ""
    Write-Host "[2/6] Fresh frozen School Days oracle -> canonical IR"
    Invoke-Checked powershell -ExecutionPolicy Bypass -File .\tools\ktrf\run_sdhq_export.ps1

    Write-Host ""
    Write-Host "[3/6] Compile production KTRF fixture"
    Invoke-Checked python .\tools\ktrf\compile_full_v0_1.py `
        --ir .\build\ktrf\school-days-hq.routing.json `
        --output .\build\ktrf\school-days-hq.ktnroute

    Write-Host ""
    Write-Host "[4/6] Configure standalone oracle/native trace harnesses"
    Invoke-Checked cmake -S .\tests\ktrf_native -B $NativeBuild

    Write-Host ""
    Write-Host "[5/6] Build frozen C++ oracle + native KTRF router traces"
    Invoke-Checked cmake --build $NativeBuild --config Release --target SchoolDaysOracleTrace KtrfNativeRouterTrace

    $OracleExe = Resolve-BuiltExe "SchoolDaysOracleTrace"
    $NativeExe = Resolve-BuiltExe "KtrfNativeRouterTrace"

    Write-Host ""
    Write-Host "[6/6] 22 ending witnesses: frozen oracle vs native .ktnroute router"
    Invoke-Checked python .\tools\ktrf\diff_sdhq_native_witnesses.py `
        --ir .\build\ktrf\school-days-hq.routing.json `
        --oracle-exe $OracleExe `
        --native-exe $NativeExe `
        --artifact $Artifact `
        --witness-source .\tests\SchoolDaysFullRouterTest.cpp

    Write-Host ""
    Write-Host "========================================================="
    Write-Host " KTRF ROUTING RUNTIME v0.1 / GATE 3 NATIVE PARITY: PASS"
    Write-Host "========================================================="
    Write-Host "Artifact: $Artifact"
}
finally {
    Pop-Location
}
