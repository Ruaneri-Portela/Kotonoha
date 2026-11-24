# Guias de compilação

[English (US)](BUILD_GUIDES.md) | **Português (Brasil)**

Use compilador C++17 e CMake. As dependências nativas podem vir de pacotes do sistema (Homebrew, apt, MSYS2 UCRT), do vcpkg ou dos submódulos fixados do SDL — veja [provedores de dependências nativas](DEPENDENCIES.pt-BR.md). Execute os comandos a partir da raiz, salvo indicação contrária.

| Plataforma | Guia |
| --- | --- |
| Linux / Windows | [Desktop abaixo](#desktop) |
| Windows (cross MinGW a partir de macOS/Linux) | [Cross-build MinGW](BUILD_WINDOWS.pt-BR.md) |
| macOS | [Bundles de aplicativo](BUILD_MACOS.pt-BR.md) |
| iOS | [Projeto Xcode nativo](BUILD_IOS.pt-BR.md) |
| Android | [Variants do Gradle](BUILD_ANDROID.pt-BR.md) |
| WebAssembly | [Emscripten](BUILD_WEB.pt-BR.md) |
| Testes | [Validação](BUILD_VALIDATION.pt-BR.md) |

## Presets CMake

O `CMakePresets.json` cobre 52 pares de presets visíveis de configure/build com
o nome canônico `<platform>-<arch>-<toolchain>-<provider>-<config>`:

| Família | Padrão de nome | Providers |
| --- | --- | --- |
| macOS nativo | `macos-{arm64,amd64}-native-{vcpkg,system}-{debug,release}` | Homebrew ou vcpkg |
| Linux nativo | `linux-{x64,arm64}-native-{vcpkg,system}-{debug,release}` | apt ou vcpkg |
| Windows MSVC | `windows-{x64,x86,arm64}-msvc-{vcpkg,system}-{debug,release}` | MSVC/pacotes nativos ou vcpkg |
| Windows MinGW nativo | `windows-{x64,x86,arm64}-mingw-{vcpkg,system}-{debug,release}` | MSYS2 UCRT/pacman ou vcpkg |
| Windows MinGW cross | `windows-{x64,x86,arm64}-mingw-cross-vcpkg-{debug,release}` | **somente vcpkg** |
| WebAssembly | `wasm-vcpkg-{debug,release}` | **somente vcpkg** |

Todos os presets usam Ninja (single-config: `CMAKE_BUILD_TYPE` é a fonte da
verdade e o campo `configuration` do build preset é informativo) e
`out/build/<preset>` como diretório de build. Os aliases de compatibilidade
`mingw-{x64,x86,arm64}` (cross vcpkg Release) e `windows-ucrt-system` (UCRT
nativo Release) mantêm os nomes históricos funcionando. Os provedores de
dependências nativas são selecionados por `KOTONOHA_DEPS_PROVIDER`,
`KOTONOHA_SDL_PROVIDER` e `KOTONOHA_IMGUI_PROVIDER`; veja
[provedores de dependências](DEPENDENCIES.pt-BR.md).

## Desktop

```sh
cmake -S . -B build -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build --target Kotonoha
ctest --test-dir build --output-on-failure
```

Configure `CMAKE_TOOLCHAIN_FILE` para o vcpkg quando necessário. Para jogos, use `-DKOTONOHA_BUILD_<OPCAO>=ON`; as opções incluem `AMAGAMI`, `ZERO`, `SNOW`, `SUMMER`, `MEO`, `DDLC`, `KATAWA`, `SCHOOL_DAYS`, `REALLIVE`, `KIRIKIRI`, `LUCA_SYSTEM`, `PMAIL`, `PONSCRIPTER`, `N2SYSTEM` e `ARTEMIS`. `-DKOTONOHA_BUILD_ALL=ON` solicita todos os ports desktop e pode exigir dependências adicionais. Veja o [índice de jogos](../games/README.pt-BR.md). Os 16 aplicativos (incluindo Example) constam no manifesto Xcode com 32 schemes; builds reais por plataforma/dispositivo continuam exigindo validação.

No desktop, o alvo `KotonohaLauncher` só existe com `-DKOTONOHA_BUILD_ALL=ON`. Ele linka as bibliotecas dos 15 ports de jogos, excluindo o Example independente, e executa os jogos no mesmo processo; veja o [guia do launcher](../launcher/README.pt-BR.md).

Na CI do GitHub Actions, Linux e WASM usam um único runner, sem cache das builds incrementais; veja os [detalhes de CI/vcpkg](CI_VCPKG.pt-BR.md).

O CMake não baixa assets comerciais. Compilar com sucesso não significa que o jogo inteiro funciona.
