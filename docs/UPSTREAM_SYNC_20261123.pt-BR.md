# Sincronização com o upstream de Ruaneri — 23/11/2026

## Objetivo e referências
Esta integração incorpora o snapshot fixo `Ruaneri-Portela/Kotonoha:dev` **009e5cfa299008ef3db37a4fb35ba4883422df81** à branch de desenvolvimento do fork `tsubaki-0x/Kotonoha`, preservando as políticas e funcionalidades específicas do School Days HQ para Windows e Android. O upstream original tinha base local registrada como `afcfc64bcbe629765386189bb4f57d37a444160d`.

O candidato foi preparado isoladamente na branch `sync/ruaneri-dev-20261123-009e`, originalmente baseada no `dev` do fork (`d5b13a6429fa4064c7214a8149b40d3c2a992ba8`). Nenhuma mudança foi feita diretamente no repositório do Ruaneri.

## Principais incorporações
- Separação de codecs neutros em `modules/formats`, containers em `modules/providers` e interpretadores em `modules/vms`.
- Novos testes, prefetch, trabalhadores de legendas, atualizações de apresentação, mídia, VFS, SDL, upscaling e melhorias em diversos ports.
- Identidade visual e recursos comuns (com licenças próprias), configurações CMake, Gradle, Windows, Linux, Web e Apple.
- `AGENTS.md` e `AGENTS.pt-BR.md` do upstream com a seção de proteção do fork preservada, incluindo DEV/DIST, saves, GPKs e restrições de publicação. A branch principal de distribuição do fork é `hybrid` (não o nome histórico).
- O asset `assets/SPLASH.PNG` e ícone específicos do fork permanecem reservados para o School Days; a nova marca genérica atende aos outros jogos.
- Android continua com SAF, arquitetura de launcher/atividade apropriada, a política de 29 GPKs somente externos, ARMv7 NEON e ARM64; nenhuma instalação no dispositivo foi realizada.
- Corrigidos contratos de testes duplicados (RealLive, School Days, ImGui/Encoding), bug de caminho no verificador de CMake em Windows, preset Windows portátil via `VCPKG_ROOT` e baseline vcpkg verificável.

## Riscos e verificações
O histórico divergente exigiu **65 resoluções de conflito no candidato DEV**. O Windows diferencia pouco o casing, mas o upstream renomeou `lib/src/Filesystem` para `lib/src/filesystem`; a árvore física deve continuar sem duplicatas e ser validada por build limpo.

Os testes devem usar fixtures sintéticos e ambientes isolados; **nunca executar o EXE retail de School Days, nem modificar GPKs/saves pessoais**.

As verificações estáticas de metadados CMake, matriz de jogos, NEON/Android, persistência, isolamento de plataformas e detecção de alvos duplicados passaram. A validação nativa Windows é detalhada abaixo. **Não confundir compilação e CTest sintético com montagem de APK, QA visual/GPU ou QA físico Android.**

### Resultados nativos Windows — 23/11/2026
- **DEV:** CMake/MSVC x64 Release `ALL_BUILD` concluído; `ctest -C Release -LE real --output-on-failure` aprovou **98/98 testes**, sem acessar saves ou GPKs originais. Log: `D:/Dev/_research/Kotonoha-Upstream-Sync-Audit-20261123/ctest-dev-all-synthetic-green-final.log`.
- **HYBRID/DIST:** configuração `KOTONOHA_DISTRIBUTION_BUILD=ON`, `KOTONOHA_ENABLE_MCP=OFF`, `KOTONOHA_DEV_CONTROL=OFF`, assets comerciais fora do bundle. `ALL_BUILD` x64 Release aprovado; CTest sintético **93/93 testes**, incluindo `SchoolDays.DistDebugAbsence` e isolamento DEV/DIST. Log: `D:/Dev/_research/Kotonoha-Upstream-Sync-Audit-20261123/ctest-hybrid-dist-all-synthetic-final-green.log`.
- **Testes dependentes de dados reais:** 14 alvos DEV e 12 HYBRID/DIST excluídos pela etiqueta `real` ou por dependências reais. Não executamos o EXE/instalador retail nem testes destrutivos em saves pessoais.
- **Regressões tratadas:** contratos de BackLog/menus/NextChoice; include legado, ícone WinRes; rejeição de monitor inválido, diretórios sem identidade, ISO duplicado, fechamento real de saída FFmpeg, fallback MetalFX e clique duplicado no slider; atualização dos testes para proteção de CLI DIST e sidecars longos sob TEMP variável.
- **Android:** `Gradle :app:tasks --offline` na HYBRID aprovou a configuração; não equivale a compilar APK. SDK plataforma `android-37` ausente no ambiente de QA, e não houve instalação nem teste físico desta integração.

Os builds são binários brutos de QA, não os pacotes finais com assets privados. Os recursos privados de launcher/Display Options não estavam presentes nas worktrees de integração, e a execução visual real ainda exige inspeção separada.

## Backups e recuperação
O bundle de 91 refs foi verificado em `D:/Dev/_backups/Kotonoha-pre-ruaneri-sync-20261123-0606/kotonoha-all-refs.bundle`; SHA-256 `EC7A583B15D6A39C3E8BF49C96D2C1493A5A2325882FCD8F8EF7EB71282BC1E6`.

Os 13 arquivos não rastreados da L1 Cross Days foram preservados em `cross-days-l1-untracked.zip` na mesma pasta, SHA-256 `3607A7F98BF527BECB6745F93817FCC056E883E1F8C0BA7B3C453D31AF84D78F`. A worktree da L1 não foi modificada; o laboratório continua congelado.

A branch `hybrid` deve ser integrada e testada separadamente, preservando suas especificidades e histórico. Não usar `git reset --hard`, `git clean`, force-push, nem excluir worktrees/branches sem classificação. PRs upstream, inclusive #95, não devem ser atualizados automaticamente.

## Estado e limpeza segura
As compilações e os testes sintéticos Windows dos candidatos estão aprovados. A distribuição ao jogador e a paridade Android permanecem condicionadas a QA visual/físico, assets autorizados e empacotamento privado separado.

O inventário local em `D:/Dev/_research/Kotonoha-Upstream-Sync-Audit-20261123/REPOSITORY_INVENTORY_20261123.pt-BR.md` registrou **30 worktrees e 37 branches locais**. Manter obrigatoriamente a worktree Cross Days L1 (diretório `games/cross-days/` não rastreado) e a worktree de PR de tradução (`SchoolDaysTranslationGate` ainda não commitado). Branches com commits exclusivos, worktrees de PRs e backups não podem ser removidos apenas por aparentarem estar obsoletos; a situação remota dos PRs deve ser verificada antes de limpeza destrutiva.
