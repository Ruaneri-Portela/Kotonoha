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

## Presets CMake (macOS Sequoia+)

O `CMakePresets.json` versionado oferece dois presets Release/Ninja com
`KOTONOHA_BUILD_EXAMPLE=ON` e `KOTONOHA_BUILD_MACOS_BUNDLES=ON`. Eles usam
`out/build/<preset>` como diretório de build e não alteram a árvore `build`
existente. Nenhum dos dois força o vcpkg; adicione o argumento de toolchain
acima no primeiro configure se as dependências do host não forem detectadas.

```sh
# Apple Silicon nativo (arm64)
# Compilar sem vcpkg (dependências do host)
cmake --preset macos-arm64-native
cmake --build out/build/macos-arm64-native --target Kotonoha

cmake --preset macos-arm64-vcpkg
cmake --build out/build/macos-arm64-vcpkg --target Kotonoha

# Intel nativo, ou cross-compilado em Apple Silicon com o SDK x86_64
cmake --preset macos-x86_64-vcpkg
cmake --build out/build/macos-x86_64-vcpkg --target Kotonoha
```

## Triplets dinâmicos Sequoia (vcpkg)

Dois triplets de overlay dedicados em `vcpkg-overlays/triplets/` compilam todas
as dependências vcpkg com linkage dinâmico de bibliotecas e deployment target
macOS 15.0 (Sequoia):

- `arm64-osx-sequoia-dynamic.cmake` — Apple Silicon (`arm64`)
- `x64-osx-sequoia-dynamic.cmake` — Intel (`x86_64`)

Ambos definem `VCPKG_LIBRARY_LINKAGE dynamic`, `VCPKG_CRT_LINKAGE dynamic`,
`VCPKG_CMAKE_SYSTEM_NAME Darwin`, `CMAKE_OSX_DEPLOYMENT_TARGET 15.0` e
`-mmacosx-version-min=15.0` nas flags C/CXX/linker. Eles usam nomes de triplet
únicos para que suas entradas no cache binário nunca colidam com os triplets
estáticos `arm64-osx` / `x64-osx`. Sempre passe os diretórios de triplets e
ports de overlay no **primeiro** configure.

```sh
# Apple Silicon (arm64), todos os ports, Release, bibliotecas vcpkg dinâmicas
cmake -S . -B out/build/macos-sequoia-arm64-dynamic -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=arm64-osx-sequoia-dynamic \
  -DVCPKG_OVERLAY_TRIPLETS=/Users/nero/Codes/Kotonoha/vcpkg-overlays/triplets \
  -DVCPKG_OVERLAY_PORTS=/Users/nero/Codes/Kotonoha/vcpkg-overlays/ports \
  '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg' \
  -DKOTONOHA_SDL_PROVIDER=VCPKG \
  -DKOTONOHA_BUILD_ALL=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build out/build/macos-sequoia-arm64-dynamic --parallel

# Intel (x86_64), todos os ports, Release, bibliotecas vcpkg dinâmicas.
# O Python3.framework do Xcode é universal (x86_64 + arm64); o Python e o
# libunibreak do Homebrew são apenas arm64 e não servem para x86_64.
XCODE_PY=/Applications/Xcode.app/Contents/Developer/Library/Frameworks/Python3.framework/Versions/3.9
cmake -S . -B out/build/macos-sequoia-x64-dynamic -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-osx-sequoia-dynamic \
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
cmake --build out/build/macos-sequoia-x64-dynamic --parallel
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
vcpkg install --triplet arm64-osx-sequoia-dynamic \
  --overlay-triplets=/Users/nero/Codes/Kotonoha/vcpkg-overlays/triplets \
  --overlay-ports=/Users/nero/Codes/Kotonoha/vcpkg-overlays/ports \
  --x-feature=sdl-vcpkg \
  --x-install-root=/Users/nero/Codes/Kotonoha/out/build/macos-sequoia-arm64-dynamic/vcpkg_installed

# Em um host arm64, mantenha os pacotes arm64-only do Homebrew fora do
# caminho de busca pkg-config do x86_64 para que o port libass não os linque.
VCPKG_LIBDIR=/Users/nero/Codes/Kotonoha/out/build/macos-sequoia-x64-dynamic/vcpkg_installed/x64-osx-sequoia-dynamic/lib/pkgconfig
PKG_CONFIG_LIBDIR="$VCPKG_LIBDIR" \
vcpkg install --triplet x64-osx-sequoia-dynamic \
  --overlay-triplets=/Users/nero/Codes/Kotonoha/vcpkg-overlays/triplets \
  --overlay-ports=/Users/nero/Codes/Kotonoha/vcpkg-overlays/ports \
  --x-feature=sdl-vcpkg \
  --x-install-root=/Users/nero/Codes/Kotonoha/out/build/macos-sequoia-x64-dynamic/vcpkg_installed
```

## Xcode

```sh
open buildsystems/xcode/Kotonoha.xcodeproj
xcodebuild -list -project buildsystems/xcode/Kotonoha.xcodeproj
```

Selecione um scheme macOS, como `KotonohaExample (macOS)`. Consulte o [guia Xcode](../buildsystems/xcode/README.pt-BR.md). Bundles precisam das bibliotecas e dos recursos corretos; não envie assets comerciais ao Git.
