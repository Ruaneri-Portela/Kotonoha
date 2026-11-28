# Kotonoha

[English (US)](README.md) | **Português (Brasil)**

Kotonoha é um **motor modular de visual novels em C++17** baseado em SDL3. Inclui cenas, diálogo, saves, entrada, provedores de assets, reprodução de mídia (FFmpeg), legendas (libass) e ferramentas de depuração (Dear ImGui). Alguns ports usam runtimes adicionais, como CPython.

> **Estado de desenvolvimento:** A branch de integração ativa é `dev`; a `main` pode estar defasada. Cada port tem um nível diferente de compatibilidade. Executar uma cena ou passar em testes básicos **não** significa reproduzir integralmente o jogo original.

## Início rápido

Requisitos: CMake, compilador C++17, Git/submódulos e as dependências em [`vcpkg.json`](vcpkg.json). Configure o vcpkg antes do CMake, caso ainda não esteja integrado ao ambiente.

```sh
git clone --recurse-submodules https://github.com/Ruaneri-Portela/Kotonoha.git
cd Kotonoha
git switch dev  # Últimas correções integradas
cmake -S . -B build -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build --target Kotonoha
```

Para usar o vcpkg explicitamente, acrescente `-DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"` na configuração. Para compilar um jogo, habilite sua opção (por exemplo, `-DKOTONOHA_BUILD_REALLIVE=ON`) e compile o alvo correspondente. Execute `ctest --test-dir build --output-on-failure` para testes nativos configurados.

## Documentação

- **[Índice da documentação](docs/README.pt-BR.md)** — guias e plataformas.
- **[Carta de estilo e identidade visual](design/README.pt-BR.md)** — K oficial, splash, paleta, temas e geração de ícones por plataforma.
- **[Guias de compilação](docs/BUILD_GUIDES.pt-BR.md)** — desktop, Android, iOS, macOS, WebAssembly e a solução Visual Studio nativa.
- **[CI e vcpkg](docs/CI_VCPKG.pt-BR.md)** — Linux/WASM no mesmo runner, artefatos e política de cache.
- **[Ports de jogos](games/README.pt-BR.md)** — módulos, limitações e instruções específicas.
- **[Máquinas virtuais](modules/vms/README.pt-BR.md)**, **[provedores de assets](modules/providers/README.pt-BR.md)** e **[formatos de arquivo](modules/formats/README.pt-BR.md)**.
- **[Overlays](docs/ASSET_OVERLAYS.pt-BR.md)**, **[armazenamento](docs/DIRECTORY_POLICY.pt-BR.md)** e **[configurações](docs/ENGINE_SETTINGS.pt-BR.md)**.

## Estado atual da integração

O repositório contém **16 aplicativos de jogos no CMake/Android (15 ports + Example)** (incluindo Example, N2System e PONscripter). O launcher compartilhado é o padrão para inicialização interativa; `--no-launcher`, `--frames` e MCP/modo sem interface seguem a política central. Os 16 aplicativos constam na matriz de alvos Xcode; a compilação real em dispositivos e simuladores ainda precisa ser validada. Consulte [validação de build](docs/BUILD_VALIDATION.pt-BR.md).

Os artefatos Linux e WebAssembly são compilados sequencialmente em **um único job do GitHub Actions**. O cache de builds incrementais do motor/jogos foi desabilitado, mantendo somente o cache binário de dependências do vcpkg. Veja [instruções de CI](docs/CI_VCPKG.pt-BR.md). Testes sintéticos não comprovam paridade com os jogos comerciais.

O [launcher unificado independente](launcher/README.pt-BR.md) funciona no desktop e linka diretamente as bibliotecas dos jogos (exceto Example), executando cada jogo no mesmo processo, sem iniciar binários secundários. É gerado apenas com `KOTONOHA_BUILD_ALL=ON` no desktop nativo.

## Estrutura

| Pasta | Função |
| --- | --- |
| `launcher/` | Launcher unificado desktop para vários motores, host destacado (`host/`) e gerenciador Avalonia (`desktop/KotonohaManager/`) |
| `lib/` | Motor compartilhado, headers públicos e testes |
| `modules/vms/` | Máquinas virtuais de scripts |
| `modules/providers/` | Leitura de assets e arquivos compactados |
| `modules/formats/` | Codecs neutros de imagem, áudio e contêineres |
| `games/` | Ports individuais e demonstração |
| `design/` | Tokens canônicos de identidade visual (`tokens.json`) e ícones/splash gerados (`generated/`) |
| `assets/` | Recursos compartilhados e avisos de terceiros |
| `buildsystems/` | CMake, projeto Gradle Android, projeto Xcode nativo e solução Visual Studio independente (`visualstudio/Kotonoha.sln`) |
| `docs/` | Documentação multiplataforma |
| `tools/` | Utilitários de build e assets |

Antes de distribuir binários, consulte a [licença](LICENSE), os [avisos de terceiros](THIRD_PARTY_NOTICES.pt-BR.md) e a [auditoria de licenciamento](docs/LICENSING.pt-BR.md).

## Dados de jogos e licenças

**Dados comerciais não são distribuídos.** Use somente assets obtidos legalmente e não os envie ao Git. O local padrão para dados de jogos é `games/<jogo>/game-assets/`. Consulte o [README do port](games/README.pt-BR.md) para arquivos exigidos. Os direitos dos jogos originais pertencem aos respectivos titulares.

Ao contribuir, mantenha o motor independente de plataforma, documente novas opções nos dois idiomas e preserve relatórios históricos ao reorganizar guias ativos.

## Integração do School Days HQ e isolamento de QA

Os testes integrados de paridade do School Days HQ (Windows x64 e Android SAF), os perfis DEV/DIST e os relatórios históricos de QA estão documentados em [`docs/KNOWN_STATE.pt-BR.md`](docs/KNOWN_STATE.pt-BR.md), [`docs/FORK_WORKFLOW.pt-BR.md`](docs/FORK_WORKFLOW.pt-BR.md) e [`games/school-days/docs/`](games/school-days/docs/). Em testes de Replay, gameplay retail ou migração de saves, utilize sempre perfis isolados via `--documents` e `--config`; nunca altere pacotes GPK comerciais nem saves pessoais durante os testes.
