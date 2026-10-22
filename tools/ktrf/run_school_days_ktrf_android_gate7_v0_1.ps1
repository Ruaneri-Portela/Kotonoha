param(
    [string]$AssetsRoot = "",
    [string]$DeviceSerial = "",
    [int]$BootWaitSeconds = 12,
    [switch]$SkipRouteBuild,
    [switch]$SkipGradleBuild,
    [switch]$SkipInstall,
    [switch]$SkipLaunch
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$AndroidProject = Join-Path $Root "android-project"
$Stage = Join-Path $Root "build\ktrf\school-days-android-gate7-assets"
$InitScript = Join-Path $Root "build\ktrf\school-days-android-gate7.init.gradle"
$Route = Join-Path $Root "build\ktrf\school-days-hq.ktnroute"
$Ir = Join-Path $Root "build\ktrf\school-days-hq.routing.json"
$Manifest = Join-Path $Root "build\ktrf\school-days-hq.ktnroute.manifest.json"
$Apk = Join-Path $AndroidProject "app\build\outputs\apk\debug\app-debug.apk"
$LogFile = Join-Path $Root "build\ktrf\school-days-android-gate7.log"
$Package = "me.hirameki.kotonoha"
$GateActivity = "$Package/.KtrfGate7Activity"

if ([string]::IsNullOrWhiteSpace($AssetsRoot)) {
    $AssetsRoot = Join-Path $Root "assets"
}
$AssetsRoot = (Resolve-Path $AssetsRoot).Path

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory=$true)][string]$Label,
        [Parameter(Mandatory=$true)][string]$Exe,
        [string[]]$Arguments = @()
    )

    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        & $Exe @Arguments
        $code = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previous
    }

    if ($code -ne 0) {
        throw "$Label failed with exit code $code"
    }
}

function Copy-RequiredFile {
    param([string]$RelativePath)
    $source = Join-Path $AssetsRoot $RelativePath
    if (-not (Test-Path $source -PathType Leaf)) {
        throw "Required Gate 7 asset is missing: $source"
    }
    $destination = Join-Path $Stage $RelativePath
    New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent) | Out-Null
    Copy-Item -Force $source $destination
}

function Copy-RequiredTree {
    param([string]$RelativePath)
    $source = Join-Path $AssetsRoot $RelativePath
    if (-not (Test-Path $source -PathType Container)) {
        throw "Required Gate 7 asset directory is missing: $source"
    }
    $destination = Join-Path $Stage $RelativePath
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Copy-Item -Force -Recurse (Join-Path $source "*") $destination
}

function Resolve-Adb {
    $candidates = @()
    if ($env:ANDROID_SDK_ROOT) {
        $candidates += (Join-Path $env:ANDROID_SDK_ROOT "platform-tools\adb.exe")
    }
    if ($env:ANDROID_HOME) {
        $candidates += (Join-Path $env:ANDROID_HOME "platform-tools\adb.exe")
    }
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate -PathType Leaf)) {
            return $candidate
        }
    }
    $command = Get-Command adb.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $command = Get-Command adb -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw "adb was not found. Set ANDROID_SDK_ROOT/ANDROID_HOME or put adb on PATH."
}

function Get-AdbPrefix {
    param([string]$Serial)
    if ([string]::IsNullOrWhiteSpace($Serial)) { return @() }
    return @("-s", $Serial)
}

function Resolve-GradleLauncher {
    $wrapper = Join-Path $AndroidProject "gradlew.bat"
    $wrapperJar = Join-Path $AndroidProject "gradle\wrapper\gradle-wrapper.jar"
    if ((Test-Path $wrapper -PathType Leaf) -and (Test-Path $wrapperJar -PathType Leaf)) {
        Write-Host "gradle_launcher=wrapper"
        return $wrapper
    }

    $properties = Join-Path $AndroidProject "gradle\wrapper\gradle-wrapper.properties"
    if (-not (Test-Path $properties -PathType Leaf)) {
        throw "Gradle wrapper JAR is missing and gradle-wrapper.properties was not found: $properties"
    }

    $text = Get-Content -Raw $properties
    $match = [regex]::Match($text, 'gradle-([0-9]+(?:\.[0-9]+)+)-bin\.zip')
    if (-not $match.Success) {
        throw "Could not determine Gradle version from $properties"
    }

    $version = $match.Groups[1].Value
    $bootstrapRoot = Join-Path $Root "build\gradle-bootstrap"
    $gradleHome = Join-Path $bootstrapRoot "gradle-$version"
    $gradleBat = Join-Path $gradleHome "bin\gradle.bat"
    if (Test-Path $gradleBat -PathType Leaf) {
        Write-Host "gradle_launcher=bootstrap-cache"
        Write-Host "gradle_version=$version"
        return $gradleBat
    }

    New-Item -ItemType Directory -Force -Path $bootstrapRoot | Out-Null
    $zip = Join-Path $bootstrapRoot "gradle-$version-bin.zip"
    $url = "https://services.gradle.org/distributions/gradle-$version-bin.zip"

    Write-Host "Gradle wrapper JAR is missing; bootstrapping Gradle $version"
    Write-Host "gradle_download=$url"
    if (-not (Test-Path $zip -PathType Leaf)) {
        Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $zip
    }

    if (Test-Path $gradleHome) {
        Remove-Item -Recurse -Force $gradleHome
    }
    Expand-Archive -Path $zip -DestinationPath $bootstrapRoot -Force
    if (-not (Test-Path $gradleBat -PathType Leaf)) {
        throw "Gradle bootstrap completed but launcher was not found: $gradleBat"
    }

    Write-Host "gradle_launcher=bootstrap"
    Write-Host "gradle_version=$version"
    return $gradleBat
}

Write-Host "=== School Days KTRF Android v0.1 / Gate 7 ==="
Write-Host "assets=$AssetsRoot"
Write-Host "stage=$Stage"
Write-Host "abi=arm64-v8a"
Write-Host ""

Push-Location $Root
try {
    Write-Host "[1/6] Production KTRF artifact"
    if ($SkipRouteBuild) {
        if (-not (Test-Path $Route -PathType Leaf)) {
            throw "-SkipRouteBuild requested but route artifact does not exist: $Route"
        }
        Write-Host "route build skipped by request"
    }
    else {
        Invoke-NativeChecked "School Days KTRF IR export" "powershell" @(
            "-ExecutionPolicy", "Bypass", "-File", ".\tools\ktrf\run_sdhq_export.ps1"
        )
        Invoke-NativeChecked "School Days KTRF compile" "python" @(
            ".\tools\ktrf\compile_full_v0_1.py",
            "--ir", $Ir,
            "--output", $Route,
            "--manifest", $Manifest
        )
    }

    Write-Host ""
    Write-Host "[2/6] Build minimal Android boot asset staging"
    if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
    New-Item -ItemType Directory -Force -Path $Stage | Out-Null

    Copy-Item -Force $Route (Join-Path $Stage "school-days-hq.ktnroute")
    Copy-RequiredFile "styles.skot"
    Copy-RequiredTree "fonts"
    Copy-RequiredFile "00\00-00-A00.ENG.ORS"
    Copy-RequiredTree "Movie00\00-00\00-00-A00"
    Copy-RequiredTree "Se00\00-00\00-00-A00"
    Copy-RequiredTree "Voice00\00-00\00-00-A00"
    Copy-RequiredFile "BGM\SD_BGM\SDBGM07_LOOP.OGG"

    $stageFiles = @(Get-ChildItem $Stage -Recurse -File)
    $stageBytes = ($stageFiles | Measure-Object -Property Length -Sum).Sum
    Write-Host "staged_files=$($stageFiles.Count)"
    Write-Host ("staged_bytes={0}" -f $stageBytes)
    Write-Host "first_ors=00/00-00-A00.ENG.ORS"

    New-Item -ItemType Directory -Force -Path (Split-Path $InitScript -Parent) | Out-Null
    $initText = @'
gradle.afterProject { project, state ->
    if (project.path == ':app') {
        def gateAssets = System.getenv('KOTONOHA_GATE7_ASSETS')
        if (gateAssets == null || gateAssets.trim().isEmpty()) {
            throw new GradleException('KOTONOHA_GATE7_ASSETS is not set')
        }
        project.android.sourceSets.main.assets.setSrcDirs([new File(gateAssets)])
        println('KTRF_GATE7_ASSETS=' + gateAssets)
    }
}
'@
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($InitScript, $initText, $utf8NoBom)
    Write-Host "init_script_encoding=utf8-nobom"

    Write-Host ""
    Write-Host "[3/6] Build arm64 Android debug APK"
    if ($SkipGradleBuild) {
        if (-not (Test-Path $Apk -PathType Leaf)) {
            throw "-SkipGradleBuild requested but APK does not exist: $Apk"
        }
        Write-Host "Gradle build skipped by request"
    }
    else {
        if (-not $env:VCPKG_ROOT) {
            throw "VCPKG_ROOT is not set"
        }
        if (-not $env:ANDROID_NDK_HOME) {
            throw "ANDROID_NDK_HOME is not set"
        }
        $env:KOTONOHA_GATE7_ASSETS = $Stage
        $gradleLauncher = Resolve-GradleLauncher
        Push-Location $AndroidProject
        try {
            Invoke-NativeChecked "Android Gradle build" $gradleLauncher @(
                "--no-daemon",
                "-I", $InitScript,
                ":app:assembleDebug"
            )
        }
        finally {
            Pop-Location
        }
        if (-not (Test-Path $Apk -PathType Leaf)) {
            throw "Gradle returned success but APK was not found: $Apk"
        }
    }
    Write-Host "apk=$Apk"

    Write-Host ""
    Write-Host "[4/6] Install APK on Android device"
    if ($SkipInstall) {
        Write-Host "install skipped by request"
    }
    else {
        $adb = Resolve-Adb
        $devices = & $adb devices
        $online = @($devices | Where-Object { $_ -match "\tdevice$" })
        if ([string]::IsNullOrWhiteSpace($DeviceSerial)) {
            if ($online.Count -ne 1) {
                throw "Expected exactly one adb device; found $($online.Count). Use -DeviceSerial when multiple devices are connected."
            }
            $DeviceSerial = ($online[0] -split "\t")[0]
        }
        $adbPrefix = Get-AdbPrefix $DeviceSerial
        Write-Host "device=$DeviceSerial"
        Invoke-NativeChecked "adb install" $adb ($adbPrefix + @("install", "-r", "-t", $Apk))

        # Never use `pm clear` here: it also erases the reusable full School
        # Days development installation under the app's external files area.
        # Refresh only the internal APK-smoke extraction tree.
        Invoke-NativeChecked "refresh internal Gate 7 staging" $adb ($adbPrefix + @(
            "shell", "run-as", $Package, "rm", "-rf", "files/assets"
        ))
    }

    Write-Host ""
    Write-Host "[5/6] Launch KTRF smoke activity"
    if ($SkipLaunch) {
        Write-Host "launch skipped by request"
        Write-Host "Gate 7 cannot be declared PASS without the device boot check."
        return
    }

    if (-not $adb) { $adb = Resolve-Adb }
    if (-not $adbPrefix) {
        $adbPrefix = Get-AdbPrefix $DeviceSerial
    }
    Invoke-NativeChecked "adb logcat clear" $adb ($adbPrefix + @("logcat", "-c"))
    Invoke-NativeChecked "adb activity launch" $adb ($adbPrefix + @(
        "shell", "am", "start", "-n", $GateActivity
    ))

    Write-Host "waiting ${BootWaitSeconds}s for native KTRF boot..."
    Start-Sleep -Seconds $BootWaitSeconds

    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $rawLog = & $adb @adbPrefix logcat -d -v brief 2>&1
        $logCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previous
    }
    if ($logCode -ne 0) {
        throw "adb logcat failed with exit code $logCode"
    }

    $logText = ($rawLog -join "`n")
    $filtered = @($rawLog | Where-Object {
        $_ -match "KTRF-GATE7|KTRF-APP|SDL|Kotonoha"
    })
    $filtered | Set-Content -Encoding UTF8 $LogFile
    $filtered | ForEach-Object { Write-Host $_ }

    Write-Host ""
    Write-Host "[6/6] Verify ARM64 KTRF New Game boot"
    if ($logText -notmatch "KTRF-GATE7.*launch source=.*route=") {
        throw "Gate 7 activity launch marker was not found in logcat. See $LogFile"
    }
    if ($logText -notmatch "\[KTRF-APP\] enabled scene=00/00-00-A00") {
        throw "Native KTRF New Game marker was not found in logcat. See $LogFile"
    }

    $assetSource = if ($logText -match "KTRF-GATE7.*launch source=external-full") {
        "external-full"
    } else {
        "internal-smoke"
    }

    Write-Host "android_activity=PASS"
    Write-Host "asset_source=$assetSource"
    Write-Host "ktrf_document=PASS"
    Write-Host "new_game_scene=00/00-00-A00"
    Write-Host "first_ors=PASS"
    Write-Host "log=$LogFile"
    Write-Host ""
    Write-Host "========================================================="
    Write-Host " SCHOOL DAYS KTRF ANDROID v0.1 / GATE 7: PASS"
    Write-Host "========================================================="
    Write-Host ""
    Write-Host "Manual check before closing the app: confirm that the first School Days"
    Write-Host "scene reaches the screen. Media/UI fidelity is intentionally outside Gate 7."
}
finally {
    Pop-Location
}
