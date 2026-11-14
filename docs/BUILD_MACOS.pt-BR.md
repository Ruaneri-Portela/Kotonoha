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

## Xcode

```sh
open buildsystems/xcode/Kotonoha.xcodeproj
xcodebuild -list -project buildsystems/xcode/Kotonoha.xcodeproj
```

Selecione um scheme macOS, como `KotonohaExample (macOS)`. Consulte o [guia Xcode](../buildsystems/xcode/README.pt-BR.md). Bundles precisam das bibliotecas e dos recursos corretos; não envie assets comerciais ao Git.
