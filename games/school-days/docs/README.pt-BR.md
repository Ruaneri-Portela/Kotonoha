# School Days HQ v1.02 — índice de documentação (Kotonoha v3)

[English (US)](README.md) | **Português (Brasil)**

**Alvos:** Windows x64 e Android (`arm64-v8a`), compartilhando uma única
implementação de runtime do School Days HQ v1.02 em `games/school-days/`,
enquanto o motor genérico permanece em `lib/` e os adaptadores Android ficam em
`buildsystems/android/`. Este índice orienta a navegação; não afirma paridade
completa com o retail nem conclusão de QA físico. Consulte
[docs/FORK_WORKFLOW.pt-BR.md](../../../docs/FORK_WORKFLOW.pt-BR.md) e
[Configuração Android portrait/SAF](android/PORTRAIT_SETUP.pt-BR.md) para os
fluxos atuais de compilação e distribuição.

Os documentos publicados anteriormente mantêm seus caminhos para não quebrar
links externos. Registros de investigação datados são fotografias históricas,
não instruções de build atuais. Um link neste índice não autoriza publicar
arte privada, GPKs comerciais, saves ou traduções.

## Documentos estáveis do projeto (nomes preservados)

| Tema | Documentos |
| --- | --- |
| Release e integração upstream | [Preparação de release](RELEASE_READINESS_AUDIT.md), [Handoff Windows L4](RUANERI_KOTONOHA_V3_WINDOWS_L4_HANDOFF.md), [Sync Ruaneri](RUANERI_SYNC_2026-11-04.md) |
| Pesquisa retail | [Tracing do runtime](ORIGINAL_RUNTIME_TRACING.md), [Assets retail ausentes](MISSING_RETAIL_ASSETS.md), [Trace de transição dos menus](MENU_TRANSITION_TRACE.md), [Auditoria do Route Map](MENU_ROUTE_MAP_AUDIT.md) |
| Interface e paridade | [Transição de saída](EXIT_TRANSITION_PORT.md), [SFX e hover dos menus](MENU_SFX_AND_ROUTE_MAP_HOVER_2026-11-05.md), [AC/CHIP do título](TITLE_AC_CHIP.md) |
| Idioma da interface da launcher (Windows/Android) | [Cinco idiomas, persistência e testes](LAUNCHER_INTERFACE_LANGUAGE.pt-BR.md) |
| QA | [Piloto de perfil limpo](CLEAN_PROFILE_PILOT.md) |

## Notas de pesquisa por tema (registros históricos)

### Configuração e launcher

- [Metadados atuais de Display Options e staging da arte privada](configuration/display-options/README.pt-BR.md)
- [Arte de Display Options](configuration/DISPLAY_OPTIONS_USER_ART_2026-11-05.md)
- [Contrato revisado de General/Display](configuration/GENERAL_DISPLAY_REVISED_USER_CONTRACT_2026-11-05.md)
- [Investigação anterior de General/Sound/Display](configuration/GENERAL_SOUND_DISPLAY_SYNC_2026-11-05.md) — histórica; requisitos posteriores podem prevalecer
- [Arte externa de General/Sound](configuration/GENERAL_SOUND_EXTERNAL_ART_2026-11-05.md)
- [Integração do JSON no launcher](configuration/LAUNCHER_JSON_L4_INTEGRATION_2026-11-06.md)

### Paridade e comparação retail

- [SFX e medidor de resultado de escolha](parity/CHOICE_RESULT_METER_SFX_RETAIL_2026-11-05.md)
- [Progresso de Load e GlobalFlag](parity/LOAD_AND_GLOBAL_PROGRESS_2026-11-05.md)
- [Fades L×H](parity/LXH_FADE_TRANSITIONS_L4_2026-11-06.md)
- [Dez itens de paridade de gameplay/UI](parity/PARITY_TEN_ITEMS_2026-11-05.md)
- [Velocidade/UI/áudio retail](parity/RETAIL_SPEED_GUI_INTERNAL_AUDIO_2026-11-05.md)

### Localização e L×H

- [Auditoria rigorosa do JSON espanhol](localization/ES_JSON_STRICT_AUDIT_L4_2026-11-06.md)
- [QA de integração de idioma L×H](localization/LXH_LOCALE_L4_STAGE2_STAGE3_QA_2026-11-07.md)
- [Assets universais ingleses L×H](localization/LXH_UNIVERSAL_ENGLISH_ASSETS_L4_2026-11-07.md)

### QA e planejamento

- [Proveniência dos saves L4](qa/L4_SAVE_PROVENANCE_STAGE4.md)
- [Progresso de QA da etapa 5 L4](qa/L4_STAGE5_QA_PROGRESS_2026-11-07.md) — registro datado, não necessariamente atual
- [Plano L4/L×H/idiomas/launcher](planning/PLANO_DEFINITIVO_L4_LXH_IDIOMAS_LAUNCHER_2026-11-07.md) — planejamento datado

### Registros de entrega

- [Staging local limpo L4](delivery/L4_CLEAN_LOCAL_STAGING_2026-11-07.md)
- [Handoff comunitário com duas DLLs](delivery/L4_TWO_DLL_COMMUNITY_HANDOFF_2026-11-07.md)

## Não mover caminhos sem atualizar dependências

As pastas `src/`, `include/`, `game-assets/`, `windows/` e `tests/fixtures/`
possuem caminhos usados em CMake, runtime e testes. A arte privada fica em
`game-assets/overlay/` (ou na fonte antiga `game-assets/display-options/`) e
`game-assets/launcher-art/`. A UI de opções aprovada pode ser incorporada
aos binários nativos de Windows e Android; overlays externos são opcionais.
A arte do launcher Windows é instalada em `assets/SchoolDaysHQ-Launcher.png`
ao lado do executável. Os metadados públicos
de Display Options ficam em `docs/configuration/display-options/`.
Mover essas pastas exige atualizar consumidores, compilar e rodar CTest.

GPKs comerciais, mídia L×H local, saves reais e JSONs privados devem ficar
fora do Git. A pasta `build/` contém builds L4 ativas, backups e junctions
para a instalação retail; nunca a limpe recursivamente de forma geral.

## Ícone privado e opcional do executável Windows

O ícone monocromático de School Days HQ é **arte fornecida e aprovada
localmente pelo proprietário**, não é um ativo do repositório. Converta a
imagem autorizada para o formato ICO do Windows e coloque-a em
`games/school-days/windows/SchoolDays.ico` no checkout local. Não versionar
nem incluir o arquivo em pacotes públicos de código-fonte. O arquivo
`games/school-days/windows/SchoolDays.rc` é versionado e referencia esse
nome, mas o CMake registra o recurso **somente quando o ICO existe**. Uma
worktree Git limpa, sem ícone, continua configurando e compilando o
executável Windows, com o ícone padrão da plataforma.
