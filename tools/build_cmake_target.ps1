param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Kotonoha", "SchoolDays", "KotonohaEngine")]
    [string]$Target,

    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateSet("Win32", "x64", "ARM64")]
    [string]$Platform = "x64",

    [switch]$Rebuild,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildDir = Join-Path $repo ("build\vs\{0}-{1}" -f $Configuration, $Platform)

if ($Clean) {
    if (Test-Path $buildDir -PathType Container) {
        & cmake --build $buildDir --config $Configuration --target clean
        if ($LASTEXITCODE -ne 0) {
            throw "CMake clean failed with exit code $LASTEXITCODE"
        }
    }
    exit 0
}

$configure = @(
    "-S", $repo,
    "-B", $buildDir,
    "-A", $Platform
)

$vcpkgRoot = $env:KOTONOHA_VCPKG_ROOT
if ([string]::IsNullOrWhiteSpace($vcpkgRoot)) {
    $vcpkgRoot = $env:VCPKG_ROOT
}

if (-not [string]::IsNullOrWhiteSpace($vcpkgRoot) -and
    -not (Test-Path (Join-Path $buildDir "CMakeCache.txt") -PathType Leaf)) {
    $toolchain = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"
    if (-not (Test-Path $toolchain -PathType Leaf)) {
        throw "The configured VCPKG root has no CMake toolchain: $toolchain"
    }
    $configure += "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
}

& cmake @configure
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE"
}

if ($Rebuild) {
    & cmake --build $buildDir --config $Configuration --target clean
    if ($LASTEXITCODE -ne 0) {
        throw "CMake clean failed with exit code $LASTEXITCODE"
    }
}

& cmake --build $buildDir --config $Configuration --target $Target
if ($LASTEXITCODE -ne 0) {
    throw "CMake build for $Target failed with exit code $LASTEXITCODE"
}
