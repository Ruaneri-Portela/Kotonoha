[CmdletBinding()]
param(
    [string]$BuildDir = "",
    [string]$OutputDir = "",
    [Parameter(Mandatory=$true)]
    [string]$RoutePath,
    [string]$RetailRoot = "",
    [switch]$IncludeLocalPacks,
    [switch]$UsePacksJunction,
    [switch]$IncludeRetailConfig,
    [switch]$Seed100Percent,
    [switch]$SeedEndingCheckpoints,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repo 'build-run\Release'
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repo 'dist\SchoolDaysHQ-Commercial-Test'
}

$build = (Resolve-Path $BuildDir).Path
$route = (Resolve-Path $RoutePath).Path
$output = [System.IO.Path]::GetFullPath($OutputDir)

if ($Clean -and (Test-Path $output)) {
    $repoFull = [System.IO.Path]::GetFullPath($repo)
    if (-not $output.StartsWith($repoFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean package outside repository: $output"
    }
    Remove-Item -LiteralPath $output -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $output | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $output 'Save') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $output 'route') | Out-Null

$exe = Join-Path $build 'SchoolDays.exe'
if (-not (Test-Path $exe)) { throw "SchoolDays.exe not found: $exe" }
Copy-Item -LiteralPath $exe -Destination (Join-Path $output 'SchoolDays.exe') -Force

# Runtime dependencies live beside the executable in desktop builds. Copy DLLs
# only; build libraries, tests and unrelated game executables stay out.
Get-ChildItem -LiteralPath $build -File -Filter '*.dll' |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $output -Force }

$assets = Join-Path $build 'assets'
if (-not (Test-Path $assets)) { throw "Runtime assets not found: $assets" }
$packageAssets = Join-Path $output 'assets'
if (Test-Path $packageAssets) { Remove-Item -LiteralPath $packageAssets -Recurse -Force }
Copy-Item -LiteralPath $assets -Destination $packageAssets -Recurse -Force

Copy-Item -LiteralPath $route -Destination (Join-Path $output 'route\school-days-hq.ktnroute') -Force

# Production-facing generic settings. School Days' authenticated Config.DAT is
# separate and remains game-owned.
$ini = @'
[KotonohaConfig][Settings]
WindowSize=1280,720
ScalerFactor=1.500
FullScreen=0
DisplayMode=0
DisplayIndex=0
FullscreenSize=0,0
FullscreenRefreshMilliHz=0
ContentAspectPreference=0
EnableVsync=1
MediaCacheSeconds=2.00
DebugUiScale=1.00
ShowFps=0
ShowTimestamp=0
EnableDebugUi=0
SkipFiles=0
UpscalingEnabled=1
UpscalerPreset=1
SplashSeconds=3.00
SplashFadeSeconds=0.80
PrefetchHorizonMs=10000
SeekBigMs=5000
SeekSmallMs=50
LoadingDebounceMs=300
MinLoadingPauseMs=1000
CursorSize=32
AssetsPath=assets
'@
[System.IO.File]::WriteAllText((Join-Path $output 'Kotonoha.ini'), $ini,
    [System.Text.UTF8Encoding]::new($false))

$retail = $null
if (-not [string]::IsNullOrWhiteSpace($RetailRoot)) {
    $retail = (Resolve-Path $RetailRoot).Path
}

if ($IncludeRetailConfig) {
    if ($null -eq $retail) { throw '-IncludeRetailConfig requires -RetailRoot' }
    $retailConfig = Join-Path $retail 'Config.DAT'
    if (-not (Test-Path $retailConfig)) { throw "Retail Config.DAT not found: $retailConfig" }
    Copy-Item -LiteralPath $retailConfig -Destination (Join-Path $output 'Config.DAT') -Force
}

$packsOut = Join-Path $output 'Packs'
if ($IncludeLocalPacks -or $UsePacksJunction) {
    if ($null -eq $retail) { throw 'Packs options require -RetailRoot' }
    $packsIn = Join-Path $retail 'Packs'
    if (-not (Test-Path $packsIn)) { throw "Retail Packs not found: $packsIn" }
    if (Test-Path $packsOut) {
        $item = Get-Item -LiteralPath $packsOut -Force
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            Remove-Item -LiteralPath $packsOut -Force
        } else {
            Remove-Item -LiteralPath $packsOut -Recurse -Force
        }
    }
    if ($UsePacksJunction) {
        New-Item -ItemType Junction -Path $packsOut -Target $packsIn | Out-Null
    } else {
        New-Item -ItemType Directory -Force -Path $packsOut | Out-Null
        Get-ChildItem -LiteralPath $packsIn -File -Filter '*.gpk' |
            ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $packsOut -Force }
    }
} else {
    New-Item -ItemType Directory -Force -Path $packsOut | Out-Null
    Set-Content -LiteralPath (Join-Path $packsOut 'PUT_RETAIL_GPKS_HERE.txt') -Value @(
        'Place the legally obtained School Days HQ retail .GPK files in this folder.'
        'For a private QA package use -UsePacksJunction or -IncludeLocalPacks.'
    ) -Encoding UTF8
}

if ($Seed100Percent -or $SeedEndingCheckpoints) {
    if ($null -eq $retail) { throw 'Save seeding requires -RetailRoot' }
    $saveIn = Join-Path $retail 'Save'
    $saveOut = Join-Path $output 'Save'
    if ($Seed100Percent) {
        $globalFlag = Join-Path $saveIn 'GlobalFlag.DAT'
        if (-not (Test-Path $globalFlag)) { throw "GlobalFlag.DAT not found: $globalFlag" }
        Copy-Item -LiteralPath $globalFlag -Destination $saveOut -Force
    }
    if ($SeedEndingCheckpoints) {
        Get-ChildItem -LiteralPath $saveIn -File -Filter 'SaveFile*.DAT' |
            Where-Object {
                $_.BaseName -match '^SaveFile(\d+)$' -and [int]$Matches[1] -le 20
            } |
            ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $saveOut -Force }
    }
}

$manifest = [ordered]@{
    GeneratedUtc = [DateTime]::UtcNow.ToString('o')
    Executable = 'SchoolDays.exe'
    Route = 'route/school-days-hq.ktnroute'
    Packs = if ($UsePacksJunction) { 'junction-private-qa' } elseif ($IncludeLocalPacks) { 'copied-private-qa' } else { 'empty-user-supplied' }
    Seed100Percent = [bool]$Seed100Percent
    SeedEndingCheckpoints = [bool]$SeedEndingCheckpoints
    UpscalerDefault = 'Anime Light - ArtCNN C4F16'
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'BUILD-MANIFEST.json') -Encoding UTF8

Write-Host "School Days commercial-test package: $output"
Get-ChildItem -LiteralPath $output -Force | Sort-Object Name |
    Select-Object Name,Mode,Length | Format-Table -AutoSize
