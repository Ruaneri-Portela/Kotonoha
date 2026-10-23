param()

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$NativeBuild = Join-Path $Root "build\ktrf\native-reader-v0.1"
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

Write-Host "=== KTRF Native Reader v0.1 / Gate 2 - Typed Core ==="
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
    Write-Host "[3/5] Configure native typed-reader harness"
    Invoke-Checked cmake -S .\tests\ktrf_native -B $NativeBuild

    Write-Host ""
    Write-Host "[4/5] Build native typed-reader harness"
    Invoke-Checked cmake --build $NativeBuild --config Release

    $Candidates = @(
        (Join-Path $NativeBuild "KtrfNativeTypedReaderTest.exe"),
        (Join-Path $NativeBuild "Release\KtrfNativeTypedReaderTest.exe"),
        (Join-Path $NativeBuild "KtrfNativeTypedReaderTest"),
        (Join-Path $NativeBuild "Release\KtrfNativeTypedReaderTest")
    )
    $TestExe = $Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $TestExe) {
        throw "KtrfNativeTypedReaderTest executable not found under $NativeBuild"
    }

    Write-Host ""
    Write-Host "[5/5] Decode + validate every typed core table and cross-reference"
    Invoke-Checked $TestExe $Artifact

    Write-Host ""
    Write-Host "========================================================="
    Write-Host " KTRF NATIVE READER v0.1 / GATE 2 TYPED CORE: PASS"
    Write-Host "========================================================="
    Write-Host "Artifact: $Artifact"
}
finally {
    Pop-Location
}
