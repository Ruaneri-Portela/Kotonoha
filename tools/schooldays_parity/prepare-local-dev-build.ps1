$ErrorActionPreference = 'Stop'

$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$release = Join-Path $repository 'build\school-days-dev-control4\games\school-days\Release'
$originalPacks = 'D:\SCHOOL DAYS HQ\Overflow\SCHOOLDAYS HQ\Packs'
$link = Join-Path $release 'Packs'

if (-not (Test-Path -LiteralPath (Join-Path $release 'SchoolDays.exe') -PathType Leaf)) {
    throw "DEV SchoolDays.exe is missing from $release"
}
if (-not (Test-Path -LiteralPath (Join-Path $release 'game-assets\school-days-hq.ktnroute') -PathType Leaf)) {
    throw 'DEV routing asset is missing'
}
$source = (Resolve-Path -LiteralPath $originalPacks).Path
$archives = @(Get-ChildItem -LiteralPath $source -File -Filter '*.GPK')
if ($archives.Count -ne 29 -or -not (Test-Path -LiteralPath (Join-Path $source 'BGM.GPK') -PathType Leaf)) {
    throw "Expected the complete 29-pack retail set in $source"
}

if (Test-Path -LiteralPath $link) {
    $existing = Get-Item -LiteralPath $link -Force
    if ($existing.LinkType -ne 'Junction' -or
        [string]$existing.Target[0] -ne $source) {
        throw "Packs already exists and does not point to $source; no change made"
    }
} else {
    New-Item -ItemType Junction -Path $link -Target $source | Out-Null
}
Write-Output "DEV Packs junction ready: $link -> $source"
