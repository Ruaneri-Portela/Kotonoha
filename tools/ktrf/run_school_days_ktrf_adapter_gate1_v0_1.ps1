param()

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$NativeBuild = Join-Path $Root "build\ktrf\school-days-adapter-gate1-v0.1"
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

Write-Host "=== School Days KTRF Adapter v0.1 / Gate 1 ==="
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
    Write-Host "[3/5] Configure standalone School Days adapter harness"
    Invoke-Checked cmake -S .\tests\ktrf_native -B $NativeBuild

    Write-Host ""
    Write-Host "[4/5] Build School Days adapter harness"
    Invoke-Checked cmake --build $NativeBuild --config Release --target KtrfSchoolDaysAdapterTest

    $TestExe = Resolve-BuiltExe "KtrfSchoolDaysAdapterTest"

    Write-Host ""
    Write-Host "[5/5] Validate profile/resource/callback adapter against production KTRF"
    Invoke-Checked $TestExe $Artifact

    Write-Host ""
    Write-Host "========================================================="
    Write-Host " SCHOOL DAYS KTRF ADAPTER v0.1 / GATE 1: PASS"
    Write-Host "========================================================="
    Write-Host "Artifact: $Artifact"
}
finally {
    Pop-Location
}
