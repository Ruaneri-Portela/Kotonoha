param(
    [string]$AssetsRoot = "",
    [string]$VcpkgRoot = "",
    [string]$BuildDir = "",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$SkipGate5,
    [switch]$SkipConfigure,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $root

function Invoke-Checked {
    param([scriptblock]$Command, [string]$Label)
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function Test-VcpkgBaseline {
    param([string]$Root, [string]$Baseline)
    & git -C $Root cat-file -e "${Baseline}:versions/baseline.json" 2>$null
    return ($LASTEXITCODE -eq 0)
}

if ([string]::IsNullOrWhiteSpace($AssetsRoot)) {
    $AssetsRoot = Join-Path $root "assets"
}
$AssetsRoot = [System.IO.Path]::GetFullPath($AssetsRoot)
if (-not (Test-Path $AssetsRoot -PathType Container)) {
    throw "Assets root does not exist: $AssetsRoot"
}

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $root "build\ktrf\school-days-desktop-gate6-v0.1"
}
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

$route = Join-Path $root "build\ktrf\school-days-hq.ktnroute"
$style = Join-Path $root "assets\styles.skot"
$log = Join-Path $root "build\ktrf\school-days-desktop-gate6.log"
$manifestPath = Join-Path $root "vcpkg.json"

Write-Host "=== School Days KTRF Desktop v0.1 / Gate 6 ==="
Write-Host "assets=$AssetsRoot"
Write-Host "build=$BuildDir"
Write-Host "configuration=$Configuration"
Write-Host ""

if (-not $SkipGate5) {
    Write-Host "[1/6] Re-run certified engine cutover gate"
    Invoke-Checked {
        powershell -ExecutionPolicy Bypass -File `
            .\tools\ktrf\run_school_days_ktrf_adapter_gate5_v0_1.ps1 `
            -AssetsRoot $AssetsRoot
    } "Gate 5"
    Write-Host ""
}
else {
    Write-Host "[1/6] Gate 5 skipped by request"
    Write-Host ""
}

if (-not (Test-Path $route -PathType Leaf)) {
    throw "Production KTRF fixture is missing: $route"
}

if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    $VcpkgRoot = $env:VCPKG_ROOT
}
if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    $vcpkgCommand = Get-Command vcpkg -ErrorAction SilentlyContinue
    if ($null -ne $vcpkgCommand) {
        $VcpkgRoot = Split-Path -Parent $vcpkgCommand.Source
    }
}

$toolchain = $null
if (-not [string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    $VcpkgRoot = [System.IO.Path]::GetFullPath($VcpkgRoot)
    $candidate = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
    if (Test-Path $candidate -PathType Leaf) {
        $toolchain = $candidate
    }
}

Write-Host "[2/6] Validate pinned vcpkg baseline"
if ($null -ne $toolchain) {
    if (-not (Test-Path $manifestPath -PathType Leaf)) {
        throw "vcpkg manifest is missing: $manifestPath"
    }

    $manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
    $baseline = [string]$manifest.'builtin-baseline'
    if ([string]::IsNullOrWhiteSpace($baseline)) {
        throw "vcpkg.json has no builtin-baseline"
    }

    Write-Host "vcpkg_root=$VcpkgRoot"
    Write-Host "vcpkg_baseline=$baseline"

    if (-not (Test-Path (Join-Path $VcpkgRoot ".git"))) {
        throw "VcpkgRoot is not a Git checkout; cannot validate/fetch pinned baseline: $VcpkgRoot"
    }

    if (-not (Test-VcpkgBaseline -Root $VcpkgRoot -Baseline $baseline)) {
        Write-Host "Pinned baseline is missing locally; fetching exact commit..."
        & git -C $VcpkgRoot fetch origin $baseline --depth=1
        $exactFetchCode = $LASTEXITCODE

        if ($exactFetchCode -ne 0 -or -not (Test-VcpkgBaseline -Root $VcpkgRoot -Baseline $baseline)) {
            Write-Host "Exact SHA fetch did not expose the baseline; fetching origin/master as fallback..."
            & git -C $VcpkgRoot fetch origin master --depth=64
            if ($LASTEXITCODE -ne 0) {
                throw "Unable to fetch vcpkg origin/master"
            }
        }
    }

    if (-not (Test-VcpkgBaseline -Root $VcpkgRoot -Baseline $baseline)) {
        throw "Pinned vcpkg baseline $baseline does not contain versions/baseline.json after fetch"
    }

    Write-Host "vcpkg_baseline_preflight=PASS"
}
else {
    Write-Host "vcpkg_toolchain=<not set; baseline preflight skipped>"
}
Write-Host ""

Write-Host "[3/6] Configure full desktop Kotonoha"
if (-not $SkipConfigure) {
    if (Test-Path $BuildDir) {
        Remove-Item $BuildDir -Recurse -Force
    }

    $configureArgs = @("-S", $root, "-B", $BuildDir)
    if ($null -ne $toolchain) {
        $configureArgs += "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
        Write-Host "vcpkg_toolchain=$toolchain"
    }
    else {
        Write-Host "vcpkg_toolchain=<not set; using CMake environment>"
    }

    & cmake @configureArgs
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE"
    }
}
else {
    Write-Host "configure skipped by request"
}
Write-Host ""

Write-Host "[4/6] Build full desktop Kotonoha"
if (-not $SkipBuild) {
    Invoke-Checked {
        cmake --build $BuildDir --config $Configuration --target Kotonoha
    } "Kotonoha desktop build"
}
else {
    Write-Host "build skipped by request"
}
Write-Host ""

$exeCandidates = @(
    (Join-Path $BuildDir "$Configuration\Kotonoha.exe"),
    (Join-Path $BuildDir "Kotonoha.exe")
)
$exe = $exeCandidates | Where-Object { Test-Path $_ -PathType Leaf } | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($exe)) {
    throw "Kotonoha.exe was not produced under $BuildDir"
}

$mediaRoot = $AssetsRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
$launchArgs = @("-K", $route, $AssetsRoot, "-p", $mediaRoot)
if (Test-Path $style -PathType Leaf) {
    $launchArgs += @("-s", $style)
}

Write-Host "[5/6] Launch real engine in KTRF mode"
Write-Host "exe=$exe"
Write-Host "route=$route"
Write-Host "ors_root=$AssetsRoot"
Write-Host "media_prefix=$mediaRoot"
Write-Host ""
Write-Host "Manual smoke checklist before closing the window:"
Write-Host "  1. The game window opens without an immediate crash."
Write-Host "  2. New Game begins from 00/00-00-A00."
Write-Host "  3. Image/audio/video loading is not obviously using broken paths."
Write-Host "  4. Let the runtime reach at least one scene transition if practical."
Write-Host "  5. Close the window normally when you are satisfied."
Write-Host ""

$logDir = Split-Path -Parent $log
if (-not (Test-Path $logDir)) {
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
}
if (Test-Path $log) {
    Remove-Item $log -Force
}

& $exe @launchArgs 2>&1 | Tee-Object -FilePath $log
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    throw "Kotonoha KTRF desktop run exited with code $exitCode. Log: $log"
}

Write-Host ""
Write-Host "[6/6] Desktop runtime exit check"
Write-Host "exit_code=$exitCode"
Write-Host "log=$log"
Write-Host ""
Write-Host "========================================================="
Write-Host " SCHOOL DAYS KTRF DESKTOP v0.1 / GATE 6: PASS"
Write-Host "========================================================="
Write-Host ""
Write-Host "Gate 6 is a real desktop smoke gate: its PASS means the full executable"
Write-Host "built, entered -K mode, and exited normally. Visual/audio correctness"
Write-Host "still requires the manual checklist above."
