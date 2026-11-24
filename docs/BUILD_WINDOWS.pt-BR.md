# Compilações Windows (MSVC, MinGW e MinGW cross)

**English (US)** | [Português (Brasil)](BUILD_WINDOWS.pt-BR.md)

Todos os presets CMake usam Ninja e `out/build/<preset-name>`. Os nomes
canônicos seguem `<platform>-<arch>-<toolchain>-<provider>-<config>`, por
exemplo `windows-x64-msvc-vcpkg-release`. O campo `configuration` do build
preset é informativo no Ninja (single-config): o `CMAKE_BUILD_TYPE` do configure
preset é a fonte da verdade.

## Matriz

| Host | Toolchain | Arquitetura | Provider | Presets |
| --- | --- | --- | --- | --- |
| Windows | MSVC | x64, x86, arm64 | vcpkg | `windows-<arch>-msvc-vcpkg-{debug,release}` |
| Windows | MSVC | x64, x86, arm64 | system | `windows-<arch>-msvc-system-{debug,release}` |
| Windows | MinGW nativo | x64, x86, arm64 | vcpkg | `windows-<arch>-mingw-vcpkg-{debug,release}` |
| Windows | MinGW nativo | x64, x86, arm64 | system (MSYS2 UCRT/pacman) | `windows-<arch>-mingw-system-{debug,release}` |
| macOS / Linux | MinGW cross | x64, x86, arm64 | **somente vcpkg** | `windows-<arch>-mingw-cross-vcpkg-{debug,release}` |

Aliases de compatibilidade (mesmas configurações, diretórios de build
históricos):

| Alias | Equivalente |
| --- | --- |
| `mingw-x64` | `windows-x64-mingw-cross-vcpkg-release` |
| `mingw-x86` | `windows-x86-mingw-cross-vcpkg-release` |
| `mingw-arm64` | `windows-arm64-mingw-cross-vcpkg-release` |
| `windows-ucrt-system` | `windows-x64-mingw-system-release` (seleção de build apenas Example) |

## MSVC (host Windows)

Use o Visual Studio developer prompt correspondente à arquitetura
(`vcvarsall.bat x64`, `x86` ou `arm64`) para que `cl.exe` e o SDK fiquem
visíveis. Os presets definem `VCPKG_TARGET_TRIPLET` (`x64-windows`,
`x86-windows` ou `arm64-windows`) e, nos builds vcpkg, `VCPKG_ROOT` deve apontar
para o checkout do vcpkg. Dependências MSVC + `SYSTEM` exigem uma instalação
ABI-compatible (nunca misture `*.dll.a` do MSYS2 com `*.lib` do MSVC); o CMake
emite um aviso para essa combinação.

```sh
cmake --preset windows-x64-msvc-vcpkg-release
cmake --build --preset windows-x64-msvc-vcpkg-release
cmake --preset windows-arm64-msvc-system-debug
cmake --build --preset windows-arm64-msvc-system-debug
```

Com o **gerador Visual Studio** em vez do Ninja dos presets, configure uma vez
com `-DCMAKE_CONFIGURATION_TYPES=Debug;Release` e compile com
`cmake --build <dir> --config Debug` (ou `--config Release`): nesse modo o
`CMAKE_BUILD_TYPE` é ignorado e o campo `configuration` do build preset é o que
importa. Os presets versionados usam Ninja em todos os lugares para manter uma
matriz uniforme de single-config.

## MinGW nativo (host Windows)

Execute em um shell MSYS2. `SYSTEM` é o caminho pacman/UCRT: `$MSYSTEM_PREFIX`
(por exemplo `C:/msys64/ucrt64`) é adicionado a `CMAKE_PREFIX_PATH` e
`PKG_CONFIG_PATH` por `KotonohaDepsProvider.cmake`. `VCPKG` usa os triplets
`*-mingw-dynamic` e define `VCPKG_HOST_TRIPLET` para o mesmo triplet.

```sh
# Shell MSYS2 UCRT64, apenas pacotes pacman
cmake --preset windows-x64-mingw-system-release
cmake --build --preset windows-x64-mingw-system-release

# triplet vcpkg x64-mingw-dynamic
cmake --preset windows-x64-mingw-vcpkg-debug
cmake --build --preset windows-x64-mingw-vcpkg-debug
```

## MinGW cross (host macOS / Linux)

**vcpkg é obrigatório.** Pacotes apt/Homebrew descrevem o host, não o alvo
Windows, portanto `KOTONOHA_DEPS_PROVIDER=SYSTEM` é rejeitado com:

```
[Kotonoha] MinGW cross-compilation requires VCPKG: system packages (apt/brew)
cannot provide Windows target libraries.
```

Pré-requisitos: CMake 3.21+ (schema de presets versão 3), Ninja, `VCPKG_ROOT` e
uma toolchain mingw-w64 no `PATH`. O toolchain de chainload do vcpkg
`$VCPKG_ROOT/scripts/toolchains/mingw.cmake` localiza os compiladores pelos
nomes padrão em ambos os hosts:

- `x64-mingw-dynamic`: `x86_64-w64-mingw32-g++`
- `x86-mingw-dynamic`: `i686-w64-mingw32-g++`
- `arm64-mingw-dynamic`: `aarch64-w64-mingw32-g++`

No Linux, instale `mingw-w64` / `gcc-mingw-w64-*` (mais
`binutils-mingw-w64-*`); no macOS, use `brew install mingw-w64`. **Não** fixe
nomes de compilador nus no cache do preset: o CMake os resolveria em relação ao
diretório de origem. O toolchain de chainload cuida deles.

```sh
cmake --preset windows-x64-mingw-cross-vcpkg-release
cmake --build --preset windows-x64-mingw-cross-vcpkg-release

# alias histórico, mesma configuração
cmake --preset mingw-x64
cmake --build --preset mingw-x64
```

Substitua `x64` por `x86` ou `arm64` nos outros alvos. Os presets exportam
`PKG_CONFIG_LIBDIR` a partir de seu próprio diretório de build e usam os
overlays `vcpkg-overlays/ports` e `vcpkg-overlays/triplets` além das features de
manifesto `sdl-vcpkg;imgui-vcpkg`.

## Gate por host

`windows-base` usa
`{"type":"equals","lhs":"${hostSystemName}","rhs":"Windows"}`;
`mingw-cross-base` usa
`{"type":"inList","string":"${hostSystemName}","list":["Darwin","Linux"]}`.
Assim, `cmake --list-presets` mostra apenas a família aplicável em cada host.

## Implantação das DLLs de runtime

O auxiliar CMake `kotonoha_is_mingw_toolchain()` em
`buildsystems/cmake/packaging/KotonohaAppPackaging.cmake` detecta esses builds
por `WIN32` mais verificações de `MINGW` / compilador / triplet `-mingw`. Não
existe gate `APPLE AND MINGW`: os caminhos exclusivos de macOS exigem
adicionalmente `CMAKE_SYSTEM_NAME STREQUAL "Darwin"`, que é falso em um
cross-build MinGW (`CMAKE_SYSTEM_NAME` é `Windows`). A busca de DLLs de runtime
do MinGW trata tanto compiladores symlinkados do Homebrew quanto instalações
mingw-w64 do Linux.
