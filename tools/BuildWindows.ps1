#requires -Version 5.1
<#
.SYNOPSIS
    Standard PowerShell driver for Kotonoha's native Windows builds.

.DESCRIPTION
    Mirrors tools/BuildApple.py: prepares dependencies (optionally), builds the
    native MSBuild solution for every Win32/x64/ARM64 x Debug/Release variant,
    and builds the MSVC CMake presets with the same shared vcpkg install roots
    under out/build/windows-<arch>-msvc-vcpkg/vcpkg_installed.

    Known toolchain workarounds applied here are documented in
    docs/BUILD_WINDOWS.md (EN) and docs/BUILD_WINDOWS.pt-BR.md.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\BuildWindows.ps1
    powershell -ExecutionPolicy Bypass -File tools\BuildWindows.ps1 -Arch x64 -Configuration Release
    powershell -ExecutionPolicy Bypass -File tools\BuildWindows.ps1 -Target cmake -Arch arm64 -Configuration Release
    powershell -ExecutionPolicy Bypass -File tools\BuildWindows.ps1 -Install -CleanObjs
#>
[CmdletBinding()]
param(
    [ValidateSet('all', 'msbuild', 'cmake')] [string] $Target = 'all',
    [ValidateSet('all', 'x86', 'x64', 'arm64')] [string] $Arch = 'all',
    [ValidateSet('all', 'Debug', 'Release')] [string] $Configuration = 'all',
    [switch] $Install,
    [switch] $CleanObjs,
    [ValidateSet('auto', 'on', 'off')] [string] $Optimization = 'auto',
    [int] $Jobs = 0
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$VisualStudio = Join-Path $Root 'buildsystems/visualstudio'
$Prepare = Join-Path $Root 'tools/PrepareVisualStudio.ps1'

$Architectures = if ($Arch -eq 'all') { @('x86', 'x64', 'arm64') } else { @($Arch) }
$Configurations = if ($Configuration -eq 'all') { @('Debug', 'Release') } else { @($Configuration) }

$PlatformMap = @{ x86 = 'Win32'; x64 = 'x64'; arm64 = 'ARM64' }
$VcvarsArchMap = @{ x86 = 'x86'; x64 = 'amd64'; arm64 = 'amd64_arm64' }

$script:Results = @()

function Say([string] $Message) {
    Write-Host "[Kotonoha/BuildWindows] $Message"
}

function Invoke-BuildStep([string] $Name, [scriptblock] $Body) {
    Say "==> $Name"
    try {
        & $Body
        $script:Results += [pscustomobject]@{ Step = $Name; Ok = $true }
        Say "OK: $Name"
    } catch {
        $script:Results += [pscustomobject]@{ Step = $Name; Ok = $false }
        Write-Error "FAILED: $Name - $($_.Exception.Message)"
        throw
    }
}

function Get-VcvarsAll {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $install = $null
    if (Test-Path -LiteralPath $vswhere) {
        $install = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild `
            -property installationPath 2>$null | Select-Object -First 1
    }
    if (-not $install) { $install = 'C:\Program Files\Microsoft Visual Studio\18\Community' }
    $bat = Join-Path $install 'VC\Auxiliary\Build\vcvarsall.bat'
    if (-not (Test-Path -LiteralPath $bat)) {
        throw "vcvarsall.bat not found: $bat (set up the Visual Studio C++ toolset)"
    }
    return $bat
}

function Get-HostPython {
    # The arm64-windows vcpkg interpreter cannot run on an x64 host. CMake must
    # use the x64 host interpreter for find_package(Python3) while still linking
    # the arm64 Development component from the vcpkg install root.
    $hostPython = Join-Path $Root 'out/build/windows-x64-msvc-vcpkg/vcpkg_installed/x64-windows/tools/python3/python.exe'
    if (Test-Path -LiteralPath $hostPython) { return $hostPython }
    $cmd = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Invoke-PrepareInstall([string] $TargetArch) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $Prepare `
        -Mode Install -Architectures $TargetArch
    if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed for $TargetArch" }
}

function Invoke-MsBuild([string] $TargetArch, [string] $Config) {
    $platform = $PlatformMap[$TargetArch]
    if ($CleanObjs) {
        $objDir = Join-Path $VisualStudio ("obj\" + $platform)
        if (Test-Path -LiteralPath $objDir) {
            Say "Removing stale intermediates: $objDir"
            Remove-Item -Recurse -Force -LiteralPath $objDir
        }
    }
    & powershell -NoProfile -ExecutionPolicy Bypass -File $Prepare `
        -Mode Build -Architectures $TargetArch -Configurations $Config
    if ($LASTEXITCODE -ne 0) { throw "MSBuild $platform/$Config failed" }
}

function Invoke-CMakeBuild([string] $TargetArch, [string] $Config, [string] $Vcvars) {
    $preset = "windows-$TargetArch-msvc-vcpkg"
    $buildDir = Join-Path $Root "out/build/$preset"
    $extra = @()

    if ($TargetArch -eq 'arm64') {
        $hostPython = Get-HostPython
        if ($hostPython) { $extra += ('"-DPython3_EXECUTABLE={0}"' -f $hostPython) }
    }

    $optimization = $Optimization
    if ($optimization -eq 'auto') { $optimization = if ($TargetArch -eq 'arm64') { 'off' } else { 'on' } }
    if ($optimization -eq 'off') {
        # MSVC 14.52 arm64 cross compiler hits C1001 on SchoolDaysBackLogPresenter.cpp
        # with /O2. Optimization off is only a build-time verification workaround.
        $extra += '"-DCMAKE_CXX_FLAGS_RELEASE=/Od"'
    }
    $jobsArg = if ($Jobs -gt 0) { " -j $Jobs" } else { '' }

    $cmdLine = '"{0}" {1} && cmake --preset {2} {3} && cmake --build "{4}" --config {5}{6}' -f `
        $Vcvars, $VcvarsArchMap[$TargetArch], $preset, ($extra -join ' '), $buildDir, $Config, $jobsArg
    Say $cmdLine
    & cmd.exe /d /s /c $cmdLine
    if ($LASTEXITCODE -ne 0) { throw "CMake $preset/$Config failed" }
}

Set-Location $Root
$vcvars = Get-VcvarsAll

if ($Install) {
    foreach ($item in $Architectures) {
        $targetArch = $item
        Invoke-BuildStep "vcpkg install $targetArch" { Invoke-PrepareInstall $targetArch }
    }
}

if ($Target -in @('all', 'msbuild')) {
    foreach ($item in $Architectures) {
        foreach ($cfg in $Configurations) {
            $targetArch = $item
            $config = $cfg
            Invoke-BuildStep "msbuild $targetArch/$config" { Invoke-MsBuild $targetArch $config }
        }
    }
}

if ($Target -in @('all', 'cmake')) {
    foreach ($item in $Architectures) {
        foreach ($cfg in $Configurations) {
            $targetArch = $item
            $config = $cfg
            Invoke-BuildStep "cmake $targetArch/$config" { Invoke-CMakeBuild $targetArch $config $vcvars }
        }
    }
}

Write-Host ''
Write-Host '[Kotonoha/BuildWindows] Summary:'
$script:Results | Format-Table -AutoSize Step, Ok
$failed = @($script:Results | Where-Object { -not $_.Ok })
if ($failed.Count -gt 0) {
    Write-Error "[Kotonoha/BuildWindows] $($failed.Count) step(s) failed"
    exit 1
}
Say 'All requested builds succeeded.'
exit 0
