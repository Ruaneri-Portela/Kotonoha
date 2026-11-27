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

## Triplets vcpkg dinâmicos (macOS 15.0)

Dois triplets de overlay dedicados em `vcpkg-overlays/triplets/` compilam todas
as dependências vcpkg com linkage dinâmico de bibliotecas e deployment target
macOS 15.0:

- `macos-arm64-vcpkg.cmake` — Apple Silicon (`arm64`)
- `macos-amd64-vcpkg.cmake` — Intel (`x86_64`)

A grafia `amd64` é apenas um nome: o triplet e o diretório de build a usam,
enquanto o compilador e o vcpkg continuam visando `x86_64` internamente.

A camada de arquitetura `amd64` fixa `Python3_EXECUTABLE=/usr/bin/python3`,
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
cmake -S . -B out/build/macos-arm64-native-vcpkg-release -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=macos-arm64-vcpkg \
  -DVCPKG_OVERLAY_TRIPLETS=$PWD/vcpkg-overlays/triplets \
  -DVCPKG_OVERLAY_PORTS=$PWD/vcpkg-overlays/ports \
  '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg;imgui-vcpkg' \
  -DKOTONOHA_SDL_PROVIDER=VCPKG \
  -DKOTONOHA_IMGUI_PROVIDER=VCPKG \
  -DKOTONOHA_BUILD_ALL=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build out/build/macos-arm64-native-vcpkg-release --parallel

# Intel (x86_64), all game ports, Release, dynamic vcpkg libraries.
# As opções Python3_* e CMAKE_EXE_LINKER_FLAGS abaixo são necessárias para que
# o bundle x86_64 incorpore o Python3.framework universal do Xcode.
# O Python do Homebrew e o libunibreak são arm64-only e não servem para x86_64.
cmake -S . -B out/build/macos-amd64-native-vcpkg-release -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=macos-amd64-vcpkg \
  -DVCPKG_OVERLAY_TRIPLETS=$PWD/vcpkg-overlays/triplets \
  -DVCPKG_OVERLAY_PORTS=$PWD/vcpkg-overlays/ports \
  '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg;imgui-vcpkg' \
  -DKOTONOHA_SDL_PROVIDER=VCPKG \
  -DKOTONOHA_IMGUI_PROVIDER=VCPKG \
  -DKOTONOHA_BUILD_ALL=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DPython3_ROOT_DIR=/Applications/Xcode.app/Contents/Developer/Library/Frameworks \
  -DPython3_FIND_FRAMEWORK=FIRST \
  '-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath,/Applications/Xcode.app/Contents/Developer/Library/Frameworks'
cmake --build out/build/macos-amd64-native-vcpkg-release --parallel
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
  --overlay-triplets=$PWD/vcpkg-overlays/triplets \
  --overlay-ports=$PWD/vcpkg-overlays/ports \
  --x-feature=sdl-vcpkg \
  --x-install-root=$PWD/out/build/macos-arm64-native-vcpkg-release/vcpkg_installed

# Em host arm64, mantenha o libunibreak arm64-only do Homebrew fora do
# pkg-config de x86_64 para que o libass não link a arquitetura errada
# (ld: ignoring file .../libunibreak.dylib: found architecture 'arm64').
# Defina PKG_CONFIG_LIBDIR explicitamente no configure e em um `vcpkg install`
# direto; o triplet repassa a variável aos ports baseados em meson:
VCPKG_LIBDIR=$PWD/out/build/macos-amd64-native-vcpkg-release/vcpkg_installed/macos-amd64-vcpkg/lib/pkgconfig
PKG_CONFIG_LIBDIR="$VCPKG_LIBDIR" \
vcpkg install --triplet macos-amd64-vcpkg \
  --overlay-triplets=$PWD/vcpkg-overlays/triplets \
  --overlay-ports=$PWD/vcpkg-overlays/ports \
  --x-feature=sdl-vcpkg \
  --x-install-root=$PWD/out/build/macos-amd64-native-vcpkg-release/vcpkg_installed
```

Defina `PKG_CONFIG_LIBDIR` apontando para o diretório `lib/pkgconfig` do triplet
antes do configure e de qualquer `vcpkg install` direto. O triplet declara
`VCPKG_ENV_PASSTHROUGH_UNTRACKED` para que o vcpkg repasse o valor aos ports
baseados em meson sem incluir o caminho absoluto no hash de ABI. Assim, o
`libunibreak.pc` arm64-only do Homebrew fica fora do build x86_64 do libass
mesmo quando o vcpkg é invocado automaticamente pelo toolchain do CMake.

O `BundleApp.cmake` prepara `Python.framework` e `Python3.framework` como
framework bundles reais, remove o helper `Python.app` aninhado e reescreve a
dependência do runtime embutido para `@executable_path/../Frameworks/...`. Se um
build antigo ainda falhar com `verify_app failed` ou `Could not ad-hoc sign`,
apague apenas `out/build/macos-amd64-native-vcpkg-release/outputs/<App>.app` e recompile.

## Xcode

```sh
open buildsystems/xcode/Kotonoha.xcodeproj
xcodebuild -list -project buildsystems/xcode/Kotonoha.xcodeproj
```

Selecione um scheme macOS, como `KotonohaExample (macOS)`. Consulte o [guia Xcode](../buildsystems/xcode/README.pt-BR.md). Bundles precisam das bibliotecas e dos recursos corretos; não envie assets comerciais ao Git.
