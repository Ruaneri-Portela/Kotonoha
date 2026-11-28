# Compilações Windows (MSVC, MinGW e cross MinGW)

[English (US)](BUILD_WINDOWS.md) | **Português (Brasil)**

Para a solução Visual Studio nativa (sem CMake; Win32/x64/ARM64, Debug e
Release, toolset v145 / VS 18), veja
[BUILD_VISUAL_STUDIO.pt-BR.md](BUILD_VISUAL_STUDIO.pt-BR.md).

Configure o CMake usando um preset de [`CMakePresets.json`](../CMakePresets.json)
(por exemplo `windows-x64-msvc-vcpkg` ou `windows-x64-mingw-system`) ou
diretamente com `cmake -S . -B <build-dir> -G Ninja`. Esses builds são
single-config, portanto `CMAKE_BUILD_TYPE` seleciona Debug ou Release.

Os aplicativos desktop ficam em `<build-dir>/outputs/<target>/`. O CMake sempre
emite `KotonohaGameHost` na **mesma** pasta de `KotonohaLauncher`
(`outputs/KotonohaLauncher/`) via `kotonoha_configure_helper_output()`; o host
destacado nunca deve ficar em `bin/`.

## MSVC (host Windows)

Use o prompt de desenvolvedor do Visual Studio correspondente à arquitetura
(`vcvarsall.bat x64`, `x86` ou `arm64`) para que `cl.exe` e o SDK fiquem
visíveis. Com vcpkg, aponte a toolchain para `$VCPKG_ROOT` e escolha
`x64-windows`, `x86-windows` ou `arm64-windows` como `VCPKG_TARGET_TRIPLET`.
MSVC + dependências `SYSTEM` exigem uma instalação ABI-compatible: nunca misture
`*.dll.a` do MSYS2 com `*.lib` do MSVC. O CMake avisa sobre essa combinação.

```sh
cmake -S . -B build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-windows \
  -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build-msvc --target Kotonoha
```

Para um build multi-config com o Visual Studio, escolha um gerador do Visual
Studio em vez do Ninja e compile com `cmake --build <dir> --config Release`;
nesse caso `CMAKE_BUILD_TYPE` é ignorado.

## Driver de build Windows

Os drivers de build seguem a convenção da plataforma: **Windows usa
PowerShell (`.ps1`)**, enquanto macOS/Linux usam Python (`tools/BuildApple.py`,
`tools/BuildIOS.py`, `tools/BuildMatrix.py`). Não adicione um driver Python para
o caminho Windows/MSVC.

`tools/BuildWindows.ps1` é o equivalente no Windows de `tools/BuildApple.py`.
Ele executa a matriz nativa do MSBuild e os presets CMake do MSVC usando as
raízes vcpkg compartilhadas em
`out/build/windows-<arch>-msvc-vcpkg/vcpkg_installed`:

```powershell
powershell -ExecutionPolicy Bypass -File tools\BuildWindows.ps1
powershell -ExecutionPolicy Bypass -File tools\BuildWindows.ps1 -Arch x64 -Configuration Release
powershell -ExecutionPolicy Bypass -File tools\BuildWindows.ps1 -Target cmake -Arch arm64
powershell -ExecutionPolicy Bypass -File tools\BuildWindows.ps1 -Install -CleanObjs
```

`-Target` seleciona `all` (padrão), `msbuild` ou `cmake`; `-Arch` e
`-Configuration` aceitam `all` ou um valor único. `-Install` executa
`PrepareVisualStudio.ps1 -Mode Install` antes; `-CleanObjs` remove os
intermediários obsoletos em `buildsystems/visualstudio/obj/<Platform>`.

O driver aplica estes workarounds de toolchain:

- O Ninja chama `cl.exe` diretamente e precisa do ambiente do `vcvarsall.bat`
  (`x86`, `amd64`, `amd64_arm64`). Sem ele o compilador não encontra headers
  padrão (`cstddef: No such file or directory`).
- Um host x64 não executa o interpretador CPython `arm64-windows`. O configure
  CMake ARM64 recebe o interpretador host por `-DPython3_EXECUTABLE` enquanto
  continua linkando o componente Development ARM64 do vcpkg.
- O compilador cruzado ARM64 do MSVC 14.52 apresenta `C1001` em
  `SchoolDaysBackLogPresenter.cpp` com `/O2`. O driver passa
  `-DCMAKE_CXX_FLAGS_RELEASE=/Od` para ARM64 por padrão; use `-Optimization on`
  para sobrescrever quando o compilador estiver corrigido.
- Objetos Win32 obsoletos compilados contra os headers antigos do ImGui falham
  no link com `ImDrawList::AddLine` não resolvido. Use `-CleanObjs` após trocar
  o provider do ImGui.

## MinGW nativo (host Windows)

Execute em um shell MSYS2. `SYSTEM` usa os pacotes do pacman/UCRT:
`KotonohaDepsProvider.cmake` adiciona `$MSYSTEM_PREFIX` (por exemplo
`C:/msys64/ucrt64`) a `CMAKE_PREFIX_PATH` e `PKG_CONFIG_PATH`. `VCPKG` usa os
triplets `*-mingw-dynamic` e define `VCPKG_HOST_TRIPLET` para o mesmo triplet.

```sh
cmake -S . -B build-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DKOTONOHA_DEPS_PROVIDER=SYSTEM \
  -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build-mingw --target Kotonoha
```

## Cross MinGW (host macOS / Linux)

**vcpkg é obrigatório aqui.** Os pacotes apt/Homebrew descrevem o host, não o
alvo Windows, então `KOTONOHA_DEPS_PROVIDER=SYSTEM` é rejeitado com:

```
[Kotonoha] MinGW cross-compilation requires VCPKG: system packages (apt/brew)
cannot provide Windows target libraries.
```

Pré-requisitos: CMake 3.21+, Ninja, `VCPKG_ROOT` e uma toolchain mingw-w64 no
`PATH`. A toolchain de chainload
`$VCPKG_ROOT/scripts/toolchains/mingw.cmake` localiza os compiladores pelos
nomes padrão em ambos os hosts:

- `x64-mingw-dynamic`: `x86_64-w64-mingw32-g++`
- `x86-mingw-dynamic`: `i686-w64-mingw32-g++`
- `arm64-mingw-dynamic`: `aarch64-w64-mingw32-g++`

No Linux, instale `mingw-w64` / `gcc-mingw-w64-*` (mais
`binutils-mingw-w64-*`); no macOS, use `brew install mingw-w64`. Não fixe nomes
de compiladores no cache do CMake: deixe a toolchain de chainload resolvê-los.

```sh
cmake -S . -B build-mingw-cross -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/toolchains/mingw.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic \
  -DVCPKG_OVERLAY_PORTS=vcpkg-overlays/ports \
  -DVCPKG_OVERLAY_TRIPLETS=vcpkg-overlays/triplets \
  '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg;imgui-vcpkg' \
  -DKOTONOHA_DEPS_PROVIDER=VCPKG \
  -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build-mingw-cross --target Kotonoha
```

Substitua `x64` por `x86` ou `arm64` nos outros alvos.

## Implantação das DLLs de runtime

O auxiliar CMake `kotonoha_is_mingw_toolchain()` em
`buildsystems/cmake/packaging/KotonohaAppPackaging.cmake` detecta esses builds
por `WIN32` mais verificações de `MINGW` / compilador / triplet `-mingw`. Não
existe gate `APPLE AND MINGW`: os caminhos exclusivos de macOS exigem
adicionalmente `CMAKE_SYSTEM_NAME STREQUAL "Darwin"`, que é falso em um
cross-build MinGW (`CMAKE_SYSTEM_NAME` é `Windows`). A busca de DLLs de runtime
do MinGW trata tanto compiladores symlinkados do Homebrew quanto instalações
mingw-w64 do Linux.
