# Cross-build Windows (MinGW)

**English (US)** | [Português (Brasil)](BUILD_WINDOWS.pt-BR.md)

O Kotonoha consegue compilar executáveis Windows a partir de hosts **macOS
(Darwin) e Linux** com mingw-w64 e vcpkg. Os presets CMake `mingw-*` são
habilitados em ambos os hosts; não é necessário um preset específico por host.

## Pré-requisitos

- CMake 3.21 ou superior (schema de presets versão 3) e Ninja.
- vcpkg instalado, com `VCPKG_ROOT` apontando para o diretório raiz dele.
- Uma toolchain mingw-w64 no `PATH`. O toolchain de chainload do vcpkg
  (`$VCPKG_ROOT/scripts/toolchains/mingw.cmake`) localiza os compiladores pelos
  nomes padrão em ambos os hosts:
  - `x64-mingw-dynamic`: `x86_64-w64-mingw32-g++`
  - `x86-mingw-dynamic`: `i686-w64-mingw32-g++`
  - `arm64-mingw-dynamic`: `aarch64-w64-mingw32-g++`

  No Linux, esses pacotes normalmente são `mingw-w64` / `gcc-mingw-w64-*`
  (mais `binutils-mingw-w64-*`); no macOS, costumam ser instalados via
  Homebrew (`brew install mingw-w64`). Os nomes acima são idênticos nos dois
  hosts, portanto nenhuma alteração nos presets é necessária.

## Compilação

```sh
cmake --preset mingw-x64
cmake --build --preset mingw-x64
```

Substitua `mingw-x64` por `mingw-x86` ou `mingw-arm64` para as outras
arquiteturas. Os presets usam os triplets vcpkg `*-mingw-dynamic`, os overlays
`vcpkg-overlays/ports` e `vcpkg-overlays/triplets` e a feature de manifesto
`sdl-vcpkg`. Os diretórios de build são `out/build/<preset-name>`; os
executáveis Windows são gerados em `out/build/<preset-name>/outputs/<target>/`,
junto com as DLLs de runtime do MinGW e do vcpkg.

O `PKG_CONFIG_LIBDIR` é derivado de `${sourceDir}` em cada preset, portanto
funciona em qualquer host sem edição. Nenhum caminho exclusivo de macOS, como
`/opt/homebrew`, está embutido nesses presets.

## Gate por host

O `mingw-cross-base` usa esta condition (schema de presets CMake versão 3):

```json
"condition": { "type": "inList", "string": "${hostSystemName}", "list": ["Darwin", "Linux"] }
```

Os presets de configuração `mingw-x64`, `mingw-x86` e `mingw-arm64` a herdam.
Assim, `cmake --list-presets` os lista no macOS e no Linux e os omite nos
demais hosts. O tipo de condition do CMake é `inList` (com `string` / `list`);
a forma `equals` para um único host continua válida para os outros presets.

## Implantação das DLLs de runtime

O auxiliar CMake `kotonoha_is_mingw_toolchain()` em
`buildsystems/cmake/packaging/KotonohaAppPackaging.cmake` detecta esses builds
por `WIN32` mais verificações de `MINGW` / compilador / triplet `-mingw`. Não
existe gate `APPLE AND MINGW`: os caminhos exclusivos de macOS exigem
adicionalmente `CMAKE_SYSTEM_NAME STREQUAL "Darwin"`, que é falso em um
cross-build MinGW (`CMAKE_SYSTEM_NAME` é `Windows`). A busca de DLLs de runtime
do MinGW trata tanto compiladores symlinkados do Homebrew quanto instalações
mingw-w64 do Linux.
