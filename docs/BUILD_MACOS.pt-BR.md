# Compilação macOS

[English (US)](BUILD_MACOS.md) | **Português (Brasil)**

Use **CMake** para builds desktop ou o **projeto Xcode** versionado para schemes nativos. Instale as ferramentas de linha de comando do Xcode, CMake e as dependências do projeto (preferencialmente pelo manifesto vcpkg).

## CMake

```sh
cmake -S . -B build/macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DKOTONOHA_BUILD_EXAMPLE=ON \
  -DKOTONOHA_BUILD_MACOS_BUNDLES=ON
cmake --build build/macos --target Kotonoha
```

Para outros ports, use a opção/alvo do jogo. Quando o vcpkg não for detectado automaticamente, defina `-DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"`.

## Presets CMake (macOS 15.0+)

O `CMakePresets.json` versionado oferece dois presets Release/Ninja com
`KOTONOHA_BUILD_EXAMPLE=ON` e `KOTONOHA_BUILD_MACOS_BUNDLES=ON`. Eles usam
`out/build/<preset>` como diretório de build e não alteram a árvore `build`
existente. Os presets `-vcpkg` usam os triplets dinâmicos para macOS 15.0;
os presets `-native` compilam diretamente contra as dependências do host, sem
toolchain vcpkg.

```sh
# Native Apple Silicon (arm64)
# Build without vcpkg (host dependencies)
cmake --preset macos-arm64-native
cmake --build out/build/macos-arm64-native --target Kotonoha

cmake --preset macos-arm64-vcpkg
cmake --build out/build/macos-arm64-vcpkg --target Kotonoha

# Native Intel, or cross-compiled on Apple Silicon with the x86_64 SDK
cmake --preset macos-amd64-vcpkg
cmake --build out/build/macos-amd64-vcpkg --target Kotonoha
```

## Triplets vcpkg dinâmicos (macOS 15.0)

Dois triplets de overlay dedicados em `vcpkg-overlays/triplets/` compilam todas
as dependências vcpkg com linkage dinâmico de bibliotecas e deployment target
macOS 15.0:

- `macos-arm64-vcpkg.cmake` — Apple Silicon (`arm64`)
- `macos-amd64-vcpkg.cmake` — Intel (`x86_64`)

A grafia `amd64` é apenas um nome: triplet, preset e diretório de build a
usam, enquanto o compilador e o vcpkg continuam visando `x86_64`
internamente.

O preset `macos-amd64-vcpkg` também fixa `Python3_EXECUTABLE=/usr/bin/python3`,
`Python3_ROOT_DIR` e `Python3_FIND_FRAMEWORK=FIRST` para que o FindPython3
selecione o `Python3.framework` universal do Xcode (3.9, x86_64 + arm64). O
rpath correspondente em `CMAKE_EXE_LINKER_FLAGS` permite que o `fixup_bundle`
resolva o runtime embutido dentro do bundle x86_64 em vez de escolher o
`Python.framework` arm64-only do Homebrew, o que fazia o `verify_app` falhar.

Ambos definem `VCPKG_LIBRARY_LINKAGE dynamic`, `VCPKG_CRT_LINKAGE dynamic`,
`VCPKG_CMAKE_SYSTEM_NAME Darwin`, `CMAKE_OSX_DEPLOYMENT_TARGET 15.0` e
`-mmacosx-version-min=15.0` nas flags C/CXX/linker. Eles usam nomes de triplet
únicos para que suas entradas no cache binário nunca colidam com os triplets
estáticos `arm64-osx` / `x64-osx`. Sempre passe os diretórios de triplets e
ports de overlay no **primeiro** configure.

```sh
# Apple Silicon (arm64), all game ports, Release, dynamic vcpkg libraries
cmake -S . -B out/build/macos-arm64-vcpkg -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=macos-arm64-vcpkg \
  -DVCPKG_OVERLAY_TRIPLETS=/Users/nero/Codes/Kotonoha/vcpkg-overlays/triplets \
  -DVCPKG_OVERLAY_PORTS=/Users/nero/Codes/Kotonoha/vcpkg-overlays/ports \
  '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg' \
  -DKOTONOHA_SDL_PROVIDER=VCPKG \
  -DKOTONOHA_BUILD_ALL=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build out/build/macos-arm64-vcpkg --parallel

# Intel (x86_64), all game ports, Release, dynamic vcpkg libraries.
# As opções Python3_* e CMAKE_EXE_LINKER_FLAGS abaixo já são definidas por
# `cmake --preset macos-amd64-vcpkg`; mantenha-as apenas em configures manuais.
# The Xcode Python3.framework is universal (x86_64 + arm64); Homebrew's
# Python and libunibreak are arm64-only and cannot be used for x86_64.
cmake -S . -B out/build/macos-amd64-vcpkg -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=macos-amd64-vcpkg \
  -DVCPKG_OVERLAY_TRIPLETS=/Users/nero/Codes/Kotonoha/vcpkg-overlays/triplets \
  -DVCPKG_OVERLAY_PORTS=/Users/nero/Codes/Kotonoha/vcpkg-overlays/ports \
  '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg' \
  -DKOTONOHA_SDL_PROVIDER=VCPKG \
  -DKOTONOHA_BUILD_ALL=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DPython3_ROOT_DIR=/Applications/Xcode.app/Contents/Developer/Library/Frameworks \
  -DPython3_FIND_FRAMEWORK=FIRST \
  '-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath,/Applications/Xcode.app/Contents/Developer/Library/Frameworks'
cmake --build out/build/macos-amd64-vcpkg --parallel
```

Execute a suíte de testes headless de qualquer árvore com
`SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy ctest --test-dir <build-dir> --output-on-failure`.
As falhas conhecidas e pré-existentes são `Kotonoha.Infrastructure`,
`Kotonoha.Directories` e `Kotonoha.MediaFormats`. Em Apple Silicon, os binários
x86_64 rodam via Rosetta: adicione `--timeout 120` (e opcionalmente `-j 8`) para
evitar o timeout padrão de 20 segundos dos testes de ajuda dos ports.

Se o primeiro configure falhar dentro do port overlay `ffmpeg` com
`x_vcpkg_pkgconfig_get_modules ... no such file or directory` (uma peculiaridade
de bootstrap de host tools do vcpkg), pré-popule a árvore de instalação com
`vcpkg install` e execute novamente o mesmo comando de configure:

```sh
vcpkg install --triplet macos-arm64-vcpkg \
  --overlay-triplets=/Users/nero/Codes/Kotonoha/vcpkg-overlays/triplets \
  --overlay-ports=/Users/nero/Codes/Kotonoha/vcpkg-overlays/ports \
  --x-feature=sdl-vcpkg \
  --x-install-root=/Users/nero/Codes/Kotonoha/out/build/macos-arm64-vcpkg/vcpkg_installed

# On an arm64 host, keep Homebrew's arm64-only libunibreak out of the x86_64
# pkg-config search path so libass does not link the wrong architecture
# (ld: ignoring file .../libunibreak.dylib: found architecture 'arm64').
# The macos-amd64-vcpkg preset exports PKG_CONFIG_LIBDIR automatically and the
# triplet passes it through to meson-based ports, so `cmake --preset
# macos-amd64-vcpkg` needs no manual pre-install for this. Set it explicitly
# only when invoking vcpkg directly:
VCPKG_LIBDIR=/Users/nero/Codes/Kotonoha/out/build/macos-amd64-vcpkg/vcpkg_installed/macos-amd64-vcpkg/lib/pkgconfig
PKG_CONFIG_LIBDIR="$VCPKG_LIBDIR" \
vcpkg install --triplet macos-amd64-vcpkg \
  --overlay-triplets=/Users/nero/Codes/Kotonoha/vcpkg-overlays/triplets \
  --overlay-ports=/Users/nero/Codes/Kotonoha/vcpkg-overlays/ports \
  --x-feature=sdl-vcpkg \
  --x-install-root=/Users/nero/Codes/Kotonoha/out/build/macos-amd64-vcpkg/vcpkg_installed
```

O preset `macos-amd64-vcpkg` define `PKG_CONFIG_LIBDIR` apontando para o
diretório `lib/pkgconfig` do triplet em seu bloco `environment`, e o triplet
declara `VCPKG_ENV_PASSTHROUGH_UNTRACKED` para que o vcpkg repasse o valor aos
ports baseados em meson sem incluir o caminho absoluto no hash de ABI. Assim, o
`libunibreak.pc` arm64-only do Homebrew fica fora do build x86_64 do libass
mesmo quando o vcpkg é invocado automaticamente pelo toolchain do CMake durante
`cmake --preset macos-amd64-vcpkg`.

## Xcode

```sh
open buildsystems/xcode/Kotonoha.xcodeproj
xcodebuild -list -project buildsystems/xcode/Kotonoha.xcodeproj
```

Selecione um scheme macOS, como `KotonohaExample (macOS)`. Consulte o [guia Xcode](../buildsystems/xcode/README.pt-BR.md). Bundles precisam das bibliotecas e dos recursos corretos; não envie assets comerciais ao Git.
