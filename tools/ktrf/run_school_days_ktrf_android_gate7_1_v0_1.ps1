param(
    [string]$AssetsRoot = "",
    [string]$DeviceSerial = ""
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$AndroidProject = Join-Path $Root "android-project"
$Stage = Join-Path $Root "build\ktrf\school-days-android-gate7-assets"
$InitScript = Join-Path $Root "build\ktrf\school-days-android-gate7.init.gradle"
$BaseRunner = Join-Path $PSScriptRoot "run_school_days_ktrf_android_gate7_v0_1.ps1"
$Package = "me.hirameki.kotonoha"
$GateActivity = "$Package/.KtrfGate7Activity"
$Apk = Join-Path $AndroidProject "app\build\outputs\apk\debug\app-debug.apk"

if ([string]::IsNullOrWhiteSpace($AssetsRoot)) {
    $AssetsRoot = Join-Path $Root "assets"
}
$AssetsRoot = (Resolve-Path $AssetsRoot).Path

function Copy-RequiredFile {
    param([string]$RelativePath)
    $source = Join-Path $AssetsRoot $RelativePath
    if (-not (Test-Path $source -PathType Leaf)) {
        throw "Required Gate 7.1 asset is missing: $source"
    }
    $destination = Join-Path $Stage $RelativePath
    New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent) | Out-Null
    Copy-Item -Force $source $destination
}

function Copy-OptionalTree {
    param([string]$RelativePath)
    $source = Join-Path $AssetsRoot $RelativePath
    if (-not (Test-Path $source -PathType Container)) {
        Write-Host "optional_missing=$RelativePath"
        return
    }
    $destination = Join-Path $Stage $RelativePath
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Copy-Item -Force -Recurse (Join-Path $source "*") $destination
    Write-Host "staged_tree=$RelativePath"
}

function Resolve-Adb {
    if ($env:ANDROID_SDK_ROOT) {
        $candidate = Join-Path $env:ANDROID_SDK_ROOT "platform-tools\adb.exe"
        if (Test-Path $candidate -PathType Leaf) { return $candidate }
    }
    $command = Get-Command adb.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $command = Get-Command adb -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw "adb was not found"
}

function Resolve-GradleLauncher {
    $wrapper = Join-Path $AndroidProject "gradlew.bat"
    $wrapperJar = Join-Path $AndroidProject "gradle\wrapper\gradle-wrapper.jar"
    if ((Test-Path $wrapper -PathType Leaf) -and (Test-Path $wrapperJar -PathType Leaf)) {
        return $wrapper
    }
    $bootstrap = Join-Path $Root "build\gradle-bootstrap\gradle-8.14.5\bin\gradle.bat"
    if (Test-Path $bootstrap -PathType Leaf) { return $bootstrap }
    throw "Gradle launcher is unavailable. Run Gate 7 once first to bootstrap Gradle 8.14.5."
}

function Invoke-Checked {
    param([string]$Exe, [string[]]$Arguments)
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        & $Exe @Arguments
        $code = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previous
    }
    if ($code -ne 0) { throw "$Exe failed with exit code $code" }
}

Write-Host "=== School Days KTRF Android v0.1 / Gate 7.1 multi-scene ==="
Write-Host ""

# Reuse Gate 7's already-proven deterministic route/staging/init-script setup,
# but stop before build/install/launch. Gate 7.1 then extends the staging tree.
& powershell -ExecutionPolicy Bypass -File $BaseRunner `
    -AssetsRoot $AssetsRoot `
    -SkipRouteBuild `
    -SkipGradleBuild `
    -SkipInstall `
    -SkipLaunch
if ($LASTEXITCODE -ne 0) { throw "Base Gate 7 staging failed" }

Write-Host ""
Write-Host "[1/4] Extend staging through first real choice"
$scenes = @("00-00-A00", "00-00-A01", "00-00-A02", "00-00-A03")
foreach ($scene in $scenes) {
    Copy-RequiredFile ("00\{0}.ENG.ORS" -f $scene)
    Copy-OptionalTree ("Movie00\00-00\{0}" -f $scene)
    Copy-OptionalTree ("Se00\00-00\{0}" -f $scene)
    Copy-OptionalTree ("Voice00\00-00\{0}" -f $scene)
}

# Keep the smoke compact but make BGM dependencies for these early scenes
# deterministic instead of guessing individual track names.
Copy-OptionalTree "BGM\SD_BGM"

$files = @(Get-ChildItem $Stage -Recurse -File)
$bytes = ($files | Measure-Object -Property Length -Sum).Sum
Write-Host "smoke_scenes=$($scenes -join ',')"
Write-Host "staged_files=$($files.Count)"
Write-Host "staged_bytes=$bytes"

Write-Host ""
Write-Host "[2/4] Rebuild arm64 APK with extended staging"
$env:KOTONOHA_GATE7_ASSETS = $Stage
$gradle = Resolve-GradleLauncher
Push-Location $AndroidProject
try {
    Invoke-Checked $gradle @("--no-daemon", "-I", $InitScript, ":app:assembleDebug")
}
finally {
    Pop-Location
}
if (-not (Test-Path $Apk -PathType Leaf)) { throw "APK was not produced: $Apk" }
Write-Host "apk=$Apk"

Write-Host ""
Write-Host "[3/4] Install clean smoke APK"
$adb = Resolve-Adb
$devices = & $adb devices
$online = @($devices | Where-Object { $_ -match "\tdevice$" })
if ([string]::IsNullOrWhiteSpace($DeviceSerial)) {
    if ($online.Count -ne 1) {
        throw "Expected exactly one adb device; found $($online.Count). Use -DeviceSerial when needed."
    }
    $DeviceSerial = ($online[0] -split "\t")[0]
}
$prefix = @("-s", $DeviceSerial)
Invoke-Checked $adb ($prefix + @("install", "-r", "-t", $Apk))
Invoke-Checked $adb ($prefix + @("shell", "pm", "clear", $Package))

Write-Host ""
Write-Host "[4/4] Launch multi-scene smoke"
Invoke-Checked $adb ($prefix + @("logcat", "-c"))
Invoke-Checked $adb ($prefix + @("shell", "am", "start", "-n", $GateActivity))

Write-Host ""
Write-Host "========================================================="
Write-Host " GATE 7.1 APK LAUNCHED"
Write-Host "========================================================="
Write-Host "Manual PASS criteria:"
Write-Host "  1. A00 reaches the screen."
Write-Host "  2. Runtime advances to A01, then A02, then A03 without the missing-ORS black stop."
Write-Host "  3. The real choice in A03 appears/commits normally."
Write-Host "  4. KTRF DEV scene catalog remains visible; jumps outside the staged subset may still fail by design."
Write-Host ""
Write-Host "This is not Gate 8 and does not attempt to package the full game."
