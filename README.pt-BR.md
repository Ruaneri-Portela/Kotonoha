# Kotonoha

[English (US)](README.md) | **Português (Brasil)**

Kotonoha é um **motor modular de visual novels em C++17** baseado em SDL3. Inclui cenas, diálogo, saves, entrada, provedores de assets, reprodução de mídia (FFmpeg), legendas (libass) e ferramentas de depuração (Dear ImGui). Alguns ports usam runtimes adicionais, como CPython.

> **Estado de desenvolvimento:** Cada port tem um nível diferente de compatibilidade. Executar uma cena ou passar em testes básicos **não** significa reproduzir integralmente o jogo original.

## Início rápido

Requisitos: CMake, compilador C++17, Git/submódulos e as dependências em [`vcpkg.json`](vcpkg.json). Configure o vcpkg antes do CMake, caso ainda não esteja integrado ao ambiente.

```sh
git clone --recurse-submodules https://github.com/Ruaneri-Portela/Kotonoha.git
cd Kotonoha
cmake -S . -B build -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build --target Kotonoha
```

Para usar o vcpkg explicitamente, acrescente `-DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"` na configuração. Para compilar um jogo, habilite sua opção (por exemplo, `-DKOTONOHA_BUILD_REALLIVE=ON`) e compile o alvo correspondente. Execute `ctest --test-dir build --output-on-failure` para testes nativos configurados.

## Documentação

- **[Índice da documentação](docs/README.pt-BR.md)** — guias e plataformas.
- **[Guias de compilação](docs/BUILD_GUIDES.pt-BR.md)** — desktop, Android, iOS, macOS e WebAssembly.
- **[Ports de jogos](games/README.pt-BR.md)** — módulos, limitações e instruções específicas.
- **[Máquinas virtuais](modules/vms/README.pt-BR.md)** e **[provedores de assets](modules/providers/README.pt-BR.md)**.
- **[Overlays](docs/ASSET_OVERLAYS.pt-BR.md)**, **[armazenamento](docs/DIRECTORY_POLICY.pt-BR.md)** e **[configurações](docs/ENGINE_SETTINGS.pt-BR.md)**.

## Estrutura

| Pasta | Função |
| --- | --- |
| `lib/` | Motor compartilhado, headers públicos e testes |
| `modules/vms/` | Máquinas virtuais de scripts |
| `modules/providers/` | Leitura de assets e arquivos compactados |
| `games/` | Ports individuais e demonstração |
| `assets/` | Recursos compartilhados e avisos de terceiros |
| `buildsystems/` | CMake, projeto Gradle Android e projeto Xcode nativo |
| `docs/` | Documentação multiplataforma |
| `tools/` | Utilitários de build e assets |

## Dados de jogos e licenças

**Dados comerciais não são distribuídos.** Use somente assets obtidos legalmente e não os envie ao Git. O local padrão para dados de jogos é `games/<jogo>/game-assets/`. Consulte o [README do port](games/README.pt-BR.md) para arquivos exigidos. Os direitos dos jogos originais pertencem aos respectivos titulares.

Ao contribuir, mantenha o motor independente de plataforma, documente novas opções nos dois idiomas e preserve relatórios históricos ao reorganizar guias ativos.
