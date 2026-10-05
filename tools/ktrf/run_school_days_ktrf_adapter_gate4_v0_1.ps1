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

$route = Join-Path $root "build\ktrf\school-days-hq.ktnroute"
$ir = Join-Path $root "build\ktrf\school-days-hq.routing.json"
$manifest = Join-Path $root "build\ktrf\school-days-hq.ktnroute.manifest.json"
$buildDir = Join-Path $root "build\ktrf\school-days-gameplay-bridge-gate4-v0.1"

function Invoke-Checked {
    param([scriptblock]$Command, [string]$Label)
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

Write-Host "=== School Days KTRF Adapter v0.1 / Gate 4 Gameplay Bridge ==="
Write-Host "assets=$AssetsRoot"
Write-Host ""

Write-Host "[1/5] Fresh frozen School Days oracle -> canonical IR"
Invoke-Checked { powershell -ExecutionPolicy Bypass -File .\tools\ktrf\run_sdhq_export.ps1 } "IR export"
Write-Host ""

Write-Host "[2/5] Compile production KTRF fixture"
Invoke-Checked {
    python .\tools\ktrf\compile_full_v0_1.py `
        --ir $ir `
        --output $route `
        --manifest $manifest
} "KTRF compile"
Write-Host ""

Write-Host "[3/5] Configure Gameplay ownership bridge harness"
if (Test-Path $buildDir) { Remove-Item $buildDir -Recurse -Force }
Invoke-Checked {
    cmake -S .\tests\ktrf_gameplay_bridge -B $buildDir
} "CMake configure"
Write-Host ""

Write-Host "[4/5] Build Gameplay ownership bridge harness"
Invoke-Checked {
    cmake --build $buildDir --config Release --target KtrfSchoolDaysGameplayBridgeTest
} "CMake build"
Write-Host ""

$exe = Join-Path $buildDir "Release\KtrfSchoolDaysGameplayBridgeTest.exe"
if (-not (Test-Path $exe)) {
    $exe = Join-Path $buildDir "KtrfSchoolDaysGameplayBridgeTest.exe"
}
if (-not (Test-Path $exe)) {
    throw "KtrfSchoolDaysGameplayBridgeTest executable was not produced"
}

Write-Host "[5/5] Route + swap exactly one active Gameplay per physical ORS"
Invoke-Checked { & $exe $route $AssetsRoot } $exe
Write-Host ""
Write-Host "========================================================="
Write-Host " SCHOOL DAYS KTRF ADAPTER v0.1 / GATE 4: PASS"
Write-Host "========================================================="
