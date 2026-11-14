# Guias de compilação

[English (US)](BUILD_GUIDES.md) | **Português (Brasil)**

Use compilador C++17, CMake, submódulos do repositório e as dependências de `vcpkg.json`. Execute os comandos a partir da raiz, salvo indicação contrária.

| Plataforma | Guia |
| --- | --- |
| Linux / Windows | [Desktop abaixo](#desktop) |
| macOS | [Bundles de aplicativo](BUILD_MACOS.pt-BR.md) |
| iOS | [Projeto Xcode nativo](BUILD_IOS.pt-BR.md) |
| Android | [Variants do Gradle](BUILD_ANDROID.pt-BR.md) |
| WebAssembly | [Emscripten](BUILD_WEB.pt-BR.md) |
| Testes | [Validação](BUILD_VALIDATION.pt-BR.md) |

## Desktop

```sh
cmake -S . -B build -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build --target Kotonoha
ctest --test-dir build --output-on-failure
```

Configure `CMAKE_TOOLCHAIN_FILE` para o vcpkg quando necessário. Para jogos, use `-DKOTONOHA_BUILD_<OPCAO>=ON`; as opções incluem `AMAGAMI`, `ZERO`, `SNOW`, `SUMMER`, `DDLC`, `SCHOOL_DAYS`, `REALLIVE`, `KIRIKIRI`, `LUCA_SYSTEM` e `PMAIL`. `-DKOTONOHA_BUILD_ALL=ON` solicita todos os ports desktop e pode exigir dependências adicionais. Veja o [índice de jogos](../games/README.pt-BR.md).

O CMake não baixa assets comerciais. Compilar com sucesso não significa que o jogo inteiro funciona.
