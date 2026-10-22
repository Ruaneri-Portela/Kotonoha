param(
    [string]$AssetsRoot = ""
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$NativeBuild = Join-Path $Root "build\ktrf\school-days-adapter-gate2-v0.1"
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

if ([string]::IsNullOrWhiteSpace($AssetsRoot)) {
    $AssetsRoot = Join-Path $Root "assets"
}
if (!(Test-Path -LiteralPath $AssetsRoot -PathType Container)) {
    throw "School Days assets root not found: $AssetsRoot"
}
$AssetsRoot = (Resolve-Path -LiteralPath $AssetsRoot).Path

Write-Host "=== School Days KTRF Adapter v0.1 / Gate 2 ORS Asset Resolver ==="
Write-Host "Assets root: $AssetsRoot"
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
    Write-Host "[3/5] Configure standalone School Days asset resolver harness"
    Invoke-Checked cmake -S .\tests\ktrf_native -B $NativeBuild

    Write-Host ""
    Write-Host "[4/5] Build School Days asset resolver harness"
    Invoke-Checked cmake --build $NativeBuild --config Release --target KtrfSchoolDaysAssetResolverTest

    $TestExe = Resolve-BuiltExe "KtrfSchoolDaysAssetResolverTest"

    Write-Host ""
    Write-Host "[5/5] Resolve and open all physical School Days ORS assets"
    Invoke-Checked $TestExe $Artifact $AssetsRoot

    Write-Host ""
    Write-Host "========================================================="
    Write-Host " SCHOOL DAYS KTRF ADAPTER v0.1 / GATE 2: PASS"
    Write-Host "========================================================="
    Write-Host "Artifact: $Artifact"
    Write-Host "Assets:   $AssetsRoot"
}
finally {
    Pop-Location
}
