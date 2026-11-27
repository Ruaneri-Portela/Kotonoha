# Build WebAssembly do Kotonoha via CMake preset (PowerShell).
# Ambiente-neutral: usa cmake/VCPKG_ROOT/EMSDK do ambiente. O unico fallback
# e' um emsdk oficial instalado em E:\emsdk (Windows). Nao depende de MSYS2.
param(
    [string]$Preset = 'wasm-vcpkg',
    [string]$Target = '',
    [switch]$ConfigureOnly
)

$ErrorActionPreference = 'Stop'
$presetName = $Preset

$cmakeExe = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
if (-not $cmakeExe) {
    $cmakeExe = Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (-not (Test-Path -LiteralPath $cmakeExe)) { throw "cmake.exe nao encontrado" }

if (-not $env:VCPKG_ROOT) {
    $env:VCPKG_ROOT = Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\18\Community\VC\vcpkg'
}
if (-not (Test-Path -LiteralPath (Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'))) {
    throw "vcpkg.cmake nao encontrado em VCPKG_ROOT=$env:VCPKG_ROOT"
}

# Emscripten SDK oficial: EMSDK do ambiente, senao E:\emsdk.
$emsdk = $env:EMSDK
if (-not $emsdk) {
    foreach ($candidate in @('E:\emsdk', 'C:\emsdk')) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'upstream\emscripten\emcc.py')) {
            $emsdk = $candidate
            break
        }
    }
}
if (-not $emsdk -or -not (Test-Path -LiteralPath (Join-Path $emsdk 'upstream\emscripten\emcc.py'))) {
    throw "emsdk oficial nao encontrado (defina EMSDK). Instale: git clone https://github.com/emscripten-core/emsdk.git E:\emsdk; emsdk install latest; emsdk activate latest"
}
$emsdk = (Resolve-Path -LiteralPath $emsdk).Path
$env:EMSDK = $emsdk

$emRoot = Join-Path $emsdk 'upstream\emscripten'
if ($env:EMSCRIPTEN_ROOT -and (Test-Path -LiteralPath (Join-Path $env:EMSCRIPTEN_ROOT 'emcc.py'))) {
    $emRoot = $env:EMSCRIPTEN_ROOT
}
$env:EMSCRIPTEN_ROOT = $emRoot
$env:EM_CONFIG = Join-Path $emsdk '.emscripten'
$env:BINARYEN_ROOT = Join-Path $emsdk 'upstream'
$env:EMSDK_NODE = (Get-ChildItem -LiteralPath (Join-Path $emsdk 'node') -Directory -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName)
$env:EMSDK_PYTHON = (Get-ChildItem -LiteralPath (Join-Path $emsdk 'python') -Recurse -Filter 'python.exe' -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName)
if ($env:EMSDK_NODE) { $env:PATH = "$env:EMSDK_NODE;$env:PATH" }
if ($env:EMSDK_PYTHON) { $env:PATH = "$env:EMSDK_PYTHON;$env:PATH" }
$env:PATH = "$env:BINARYEN_ROOT\bin;$emRoot;$emsdk;$env:PATH"
# Ninja e' o gerador dos presets; aceita qualquer instalacao disponivel.
$ninja = (Get-Command ninja.exe -ErrorAction SilentlyContinue).Source
if (-not $ninja -and (Test-Path -LiteralPath 'C:\msys64\ucrt64\bin\ninja.exe')) {
    $env:PATH = "$env:PATH;C:\msys64\ucrt64\bin"
}

Write-Host "[Kotonoha] preset=$presetName EMSDK=$emsdk EMSCRIPTEN_ROOT=$emRoot VCPKG_ROOT=$env:VCPKG_ROOT"

& $cmakeExe --preset $presetName
if ($LASTEXITCODE -ne 0) { throw "configure WASM falhou (exit $LASTEXITCODE)" }
if ($ConfigureOnly -or $env:KOTONOHA_WASM_CONFIGURE_ONLY -eq '1') {
    Write-Host '[Kotonoha] configuracao WASM concluida'
    exit 0
}

$buildArgs = @('--build', '--preset', $presetName)
if ($Target) { $buildArgs += @('--target', $Target) }
& $cmakeExe @buildArgs
if ($LASTEXITCODE -ne 0) { throw "build WASM falhou (exit $LASTEXITCODE)" }
Write-Host '[Kotonoha] build WASM concluido'
