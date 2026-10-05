param(
    [string]$AssetsRoot = "",
    [string]$DeviceSerial = "",
    [switch]$SkipRoute
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Package = "me.hirameki.kotonoha"
$RemoteRoot = "/sdcard/Android/data/$Package/files/SchoolDays"
$Route = Join-Path $Root "build\ktrf\school-days-hq.ktnroute"

if ([string]::IsNullOrWhiteSpace($AssetsRoot)) {
    $AssetsRoot = Join-Path $Root "assets"
}
$AssetsRoot = (Resolve-Path $AssetsRoot).Path

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

function Invoke-AdbChecked {
    param([string[]]$Arguments)
    & $script:Adb @script:AdbPrefix @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "adb failed with exit code $LASTEXITCODE: $($Arguments -join ' ')"
    }
}

$Adb = Resolve-Adb
$devices = & $Adb devices
$online = @($devices | Where-Object { $_ -match "\tdevice$" })
if ([string]::IsNullOrWhiteSpace($DeviceSerial)) {
    if ($online.Count -ne 1) {
        throw "Expected exactly one adb device; found $($online.Count). Use -DeviceSerial when multiple devices are connected."
    }
    $DeviceSerial = ($online[0] -split "\t")[0]
}
$AdbPrefix = @("-s", $DeviceSerial)

Write-Host "=== School Days Android full asset deploy v0.1 ==="
Write-Host "device=$DeviceSerial"
Write-Host "source=$AssetsRoot"
Write-Host "destination=$RemoteRoot"
Write-Host ""

Invoke-AdbChecked @("shell", "mkdir", "-p", $RemoteRoot)

Write-Host "[1/3] Sync full School Days asset tree"
Write-Host "This can take a while on the first deployment (~6 GB)."
$localDot = Join-Path $AssetsRoot "."
Invoke-AdbChecked @("push", "--sync", $localDot, "$RemoteRoot/")

Write-Host ""
Write-Host "[2/3] Deploy current KTRF route"
if ($SkipRoute) {
    Write-Host "route deploy skipped by request"
} else {
    if (-not (Test-Path $Route -PathType Leaf)) {
        throw "KTRF route not found: $Route. Run the Gate 7 route build first or use -SkipRoute."
    }
    Invoke-AdbChecked @("push", $Route, "$RemoteRoot/school-days-hq.ktnroute")
}

Write-Host ""
Write-Host "[3/3] Verify development installation"
Invoke-AdbChecked @("shell", "du", "-sh", $RemoteRoot)
Invoke-AdbChecked @("shell", "ls", "$RemoteRoot/00/00-00-A00.ENG.ORS")
Invoke-AdbChecked @("shell", "ls", "$RemoteRoot/styles.skot")
if (-not $SkipRoute) {
    Invoke-AdbChecked @("shell", "ls", "$RemoteRoot/school-days-hq.ktnroute")
}

Write-Host ""
Write-Host "========================================================="
Write-Host " SCHOOL DAYS ANDROID FULL ASSETS: DEPLOYED"
Write-Host "========================================================="
Write-Host ""
Write-Host "Future runs use adb push --sync, so unchanged files are not recopied."
Write-Host "Do not run 'pm clear $Package' unless you intentionally want to erase this installation."
