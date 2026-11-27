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
$env:PATH = "$llvmRoot;$ucrtBin;$env:PATH"

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
