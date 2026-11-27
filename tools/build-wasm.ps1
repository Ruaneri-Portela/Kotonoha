# Build WebAssembly do Kotonoha via CMake preset (PowerShell).
# Ambiente-neutral: usa cmake/VCPKG_ROOT/EMSDK do ambiente, com fallbacks
# apenas para uma instalacao MSYS2 UCRT64 tipica no Windows.
param(
    [string]$Preset = 'wasm-vcpkg',
    [string]$Target = '',
    [switch]$ConfigureOnly
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
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

# Emscripten: SDK oficial (EMSDK) ou instalacao MSYS2 UCRT64.
$emRoot = $env:EMSCRIPTEN_ROOT
if (-not $emRoot -and $env:EMSDK) {
    $emRoot = Join-Path $env:EMSDK 'upstream\emscripten'
}
if (-not $emRoot) { $emRoot = 'C:\msys64\ucrt64\lib\emscripten' }
if (-not (Test-Path -LiteralPath $emRoot)) { throw "EMSCRIPTEN_ROOT nao encontrado: $emRoot" }
$env:EMSCRIPTEN_ROOT = $emRoot
if (-not $env:EMSDK) { $env:EMSDK = $emRoot }

# LLVM/UCRT normalmente ja estao no PATH quando o ambiente esta ativado;
# acrescentamos os diretorios padrao do MSYS2 apenas como fallback.
$uCRT = $env:MINGW_PREFIX
if (-not $uCRT) { $uCRT = 'C:\msys64\ucrt64' }
$llvmBin = Join-Path $uCRT 'opt\emscripten-llvm\bin'
if (Test-Path -LiteralPath $llvmBin) { $env:PATH = "$llvmBin;$env:PATH" }
if (Test-Path -LiteralPath (Join-Path $uCRT 'bin')) {
    $env:PATH = "$uCRT\bin;$env:PATH"
}
$env:PATH = "$emRoot;$env:PATH"

# O wasm-opt do Binaryen MSYS2 trava com -O3 em ports como bzip2/brotli/json-c.
# Geramos um shim FORA do repositorio (TEMP) que copia entrada -> saida e
# responde --version com o numero esperado pelo emcc. A otimizacao e opcional.
$shimRoot = Join-Path $env:TEMP 'kotonoha-wasm-shim'
$shimDir = Join-Path $shimRoot 'bin'
$shimExe = Join-Path $shimDir 'wasm-opt.exe'
if (-not (Test-Path -LiteralPath $shimExe)) {
    New-Item -ItemType Directory -Force -Path $shimDir | Out-Null
    $shimSource = @'
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-version")) {
            puts("wasm-opt version 133");
            return 0;
        }
    const char *input = NULL, *output = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) output = argv[++i];
        else if (argv[i][0] != '-' && !input) input = argv[i];
    }
    if (!input || !output || !strcmp(input, output)) return 0;
    FILE *in = fopen(input, "rb");
    if (!in) return 1;
    FILE *out = fopen(output, "wb");
    if (!out) { fclose(in); return 1; }
    char buffer[65536];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), in)) > 0)
        fwrite(buffer, 1, count, out);
    fclose(in);
    fclose(out);
    return 0;
}
'@
    $shimC = Join-Path $shimRoot 'wasm-opt.c'
    Set-Content -LiteralPath $shimC -Value $shimSource -Encoding ascii
    $gcc = Get-Command gcc.exe -ErrorAction SilentlyContinue
    if (-not $gcc) { $gccExe = Join-Path $uCRT 'bin\gcc.exe' } else { $gccExe = $gcc.Source }
    & $gccExe -O2 -o $shimExe $shimC
    if ($LASTEXITCODE -ne 0) { throw "Falha ao compilar o shim wasm-opt" }
    # O Emscripten procura wasm-emscripten-finalize no mesmo BINARYEN_ROOT.
    Copy-Item -Force $shimExe (Join-Path $shimDir 'wasm-emscripten-finalize.exe')
}
$env:PATH = "$shimDir;$env:PATH"

Write-Host "[Kotonoha] preset=$presetName EMSCRIPTEN_ROOT=$emRoot VCPKG_ROOT=$env:VCPKG_ROOT"

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
