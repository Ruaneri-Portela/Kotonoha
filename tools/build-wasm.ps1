# Build WebAssembly do Kotonoha (PowerShell puro, sem bash/MSYS2 shell).
# Equivale a tools/build-wasm.sh, mas usando apenas cmdlets e o cmake.exe do
# Visual Studio. O Emscripten do MSYS2 UCRT64 fornece o toolchain e o LLVM.
#
# Uso: & .\tools\build-wasm.ps1
#      & .\tools\build-wasm.ps1 -DKOTONOHA_BUILD_ALL=ON
#      $env:KOTONOHA_WASM_TARGET='RealLive'; & .\tools\build-wasm.ps1
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$buildDir = if ($env:KOTONOHA_WASM_BUILD_DIR) { $env:KOTONOHA_WASM_BUILD_DIR } else { Join-Path $root 'build-wasm' }
$target = if ($env:KOTONOHA_WASM_TARGET) { $env:KOTONOHA_WASM_TARGET } else { 'Kotonoha' }

$emRoot = if ($env:EMSCRIPTEN_ROOT) { $env:EMSCRIPTEN_ROOT } else { 'C:\msys64\ucrt64\lib\emscripten' }
$llvmRoot = if ($env:LLVM_ROOT) { $env:LLVM_ROOT } else { 'C:\msys64\ucrt64\opt\emscripten-llvm\bin' }
$ucrtBin = if ($env:UCRT_BIN) { $env:UCRT_BIN } else { 'C:\msys64\ucrt64\bin' }
$vcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg' }
$cmakeExe = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
if (-not $cmakeExe) {
  $cmakeExe = Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (-not (Test-Path -LiteralPath $cmakeExe)) { throw "cmake.exe nao encontrado" }

$vcpkgToolchain = Join-Path $vcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
$emToolchain = Join-Path $emRoot 'cmake\Modules\Platform\Emscripten.cmake'
if (-not (Test-Path -LiteralPath $vcpkgToolchain)) { throw "vcpkg.cmake nao encontrado: $vcpkgToolchain" }
if (-not (Test-Path -LiteralPath $emToolchain)) { throw "Emscripten.cmake nao encontrado: $emToolchain" }

# Emscripten precisa do LLVM e do Python/Ninja do UCRT no PATH.
$env:EMSCRIPTEN_ROOT = $emRoot
$env:LLVM_ROOT = $llvmRoot
# O triplet wasm32-emscripten do vcpkg exige emcc visivel no PATH.
# No Windows o wasm-opt do Binaryen do MSYS2 trava com -O3 em executaveis
# WASM de ports como bzip2/brotli/json-c. Como a otimizacao e opcional,
# geramos um shim FORA do repositorio (TEMP) que so copia entrada -> saida
# e responde --version com o numero esperado pelo emcc.
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
    $gcc = Join-Path $ucrtBin 'gcc.exe'
    & $gcc -O2 -o $shimExe $shimC
    if ($LASTEXITCODE -ne 0) { throw "Falha ao compilar o shim wasm-opt" }
    # O Emscripten procura wasm-emscripten-finalize no mesmo BINARYEN_ROOT.
    Copy-Item -Force $shimExe (Join-Path $shimDir 'wasm-emscripten-finalize.exe')
}
$env:PATH = "$shimDir;$emRoot;$llvmRoot;$ucrtBin;$env:PATH"

Write-Host "[Kotonoha] EMSCRIPTEN_ROOT=$emRoot"
Write-Host "[Kotonoha] VCPKG_ROOT=$vcpkgRoot"
Write-Host "[Kotonoha] build dir: $buildDir"

$cmakeArgs = @(
  '-S', $root,
  '-B', $buildDir,
  '-G', 'Ninja',
  "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain",
  "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$emToolchain",
  '-DVCPKG_TARGET_TRIPLET=wasm32-emscripten',
  "-DVCPKG_OVERLAY_PORTS=$(Join-Path $root 'vcpkg-overlays\ports')",
  '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg;imgui-vcpkg',
  '-DKOTONOHA_SDL_PROVIDER=VCPKG',
  '-DKOTONOHA_IMGUI_PROVIDER=VCPKG',
  '-DKOTONOHA_BUILD_EXAMPLE=ON',
  '-DBUILD_TESTING=OFF',
  '-DCMAKE_BUILD_TYPE=Release'
) + $args
& $cmakeExe @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "configuracao WASM falhou (exit $LASTEXITCODE)" }

if ($env:KOTONOHA_WASM_CONFIGURE_ONLY -eq '1') {
  Write-Host '[Kotonoha] configuracao concluida (KOTONOHA_WASM_CONFIGURE_ONLY=1)'
  exit 0
}

$buildArgs = @('--build', $buildDir, '--target', $target)
if ($env:KOTONOHA_WASM_JOBS) { $buildArgs += @('--parallel', $env:KOTONOHA_WASM_JOBS) }
& $cmakeExe @buildArgs
if ($LASTEXITCODE -ne 0) { throw "build WASM falhou (exit $LASTEXITCODE)" }

Write-Host "[Kotonoha] build-wasm concluido: $(Join-Path (Join-Path $buildDir 'outputs') $target)"
