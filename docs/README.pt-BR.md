# Índice de documentação — Kotonoha v3

[English (US)](README.md) | **Português (Brasil)**

Os READMEs da raiz descrevem o projeto, os jogos e os fluxos básicos de build.
Estes guias abrangem o motor compartilhado e ferramentas; permanecem nos
caminhos já publicados para evitar a quebra de links existentes.

| Área | Documento | Objetivo |
| --- | --- | --- |
| Introdução | [README (English)](../README.md) / [Português](../README.pt-BR.md) | Visão geral e organização |
| Compilação e validação | [BUILD_VALIDATION.md](BUILD_VALIDATION.md) | Requisitos, build e testes |
| Configuração | [SETTINGS.md](SETTINGS.md) | Configurações compartilhadas do motor |
| Assets e overlays | [ASSET_OVERLAYS.md](ASSET_OVERLAYS.md) | Proprietários, overlays e resolução |
| Caminhos por plataforma | [Directories.pt-BR.md](Directories.pt-BR.md) | Diretórios por plataforma |
| Android | [Android README](../Android/README.md) / [PT-BR](../Android/README.pt-BR.md) | Plataforma móvel adiada |
| School Days HQ | [Índice School Days](../games/school-days/docs/README.pt-BR.md) | Windows L4, paridade, idiomas e QA |
| Sincronização upstream histórica | [RUANERI_SYNC_FOLLOWUP_2026-11-04.md](RUANERI_SYNC_FOLLOWUP_2026-11-04.md) | Registro datado, não relatório atual |
| Higiene do repositório | [REPOSITORY_HYGIENE_2026-11-09.md](REPOSITORY_HYGIENE_2026-11-09.md) | Limpeza segura, dados protegidos e reversão |
| Próximas tarefas de arquitetura | [Pendências de manutenção](MAINTENANCE_BACKLOG.pt-BR.md) | CMake, SDL callbacks, RAII e StoragePaths pendentes |

## Convenções de pastas

- `lib/`, `cmake/`, `games/` e o conteúdo versionado de `assets/` são fonte
  ou insumos de build; **não** devem ser eliminados como arquivos gerados.
- Tanto `cmake/` quanto `buildsystems/cmake/` são usados atualmente; não foram
  consolidados. O projeto Windows L4 ainda inclui módulos de `cmake/`.
- `build/`, `vcpkg_installed/` e saídas Android são gerados, mas o `build/`
  **local contém GPKs comerciais, saves preservados, relatórios, junctions
  para o retail e um ZIP de handoff**. Nunca o apague integralmente.
- Notas datadas de pesquisa de School Days ficam separadas por tema na pasta
  de documentação do jogo. Documentos publicados na raiz mantêm seus caminhos.
- Capturas antes soltas na raiz foram arquivadas localmente em
  `build/repo-cleanup-backup-20261109/root-qa-captures/`, com backup ZIP
  separado também em `D:/Dev/Kotonoha-v3-reorg-backups/`. Não são assets Git.

## Segurança

Nenhum script de limpeza deve seguir junctions ou reparse points nem apagar
o executável retail, GPKs, saves, traduções privadas ou a Release Windows L4.
Notas históricas e capturas de QA são registros, não garantia de que os bugs
ou etapas correspondentes permaneçam atuais.
