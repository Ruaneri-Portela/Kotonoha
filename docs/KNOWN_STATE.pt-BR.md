# Estado conhecido após o sync upstream/dev — 2026-11-15

[English (US)](KNOWN_STATE.md) | **Português (Brasil)**

Escopo: mescla local Windows L4 de School Days HQ v1.02 com upstream/dev
`fdd0b041` sobre a dev pré-sync `18620ef9`.
A branch de integração começou em `3faacd0f`; a revisão de pendências
prosseguiu após `c0941303`. São **hashes de referência**, não garantias
de distribuição. Consulte o registro local, não versionado,
`build/_sync-final-log.md` para comandos e decisão da publicação.

| Área | Estado da verificação | Risco / próxima verificação autorizada |
| --- | --- | --- |
| Sete epílogos L×H após os créditos com WMV/OGG/PNG reais, fades e SaveFeel | **Não verificados** após a mescla; testes sintéticos não provam paridade da mídia real | QA ponta a ponta isolado com mídia licenciada; nunca incluir retail no Git |
| LOAD e carregamento de saves existentes | **Não verificado** com saves dos jogadores (intencionalmente protegidos); apenas testes sintéticos | Teste de leitura em cópias descartáveis com autorização expressa |
| GPK/VFS, prioridade, idiomas estritos e GPKs reais | **Não verificados** com os 29 packs retail nem traduções reais da DLC | QA privado somente leitura mediante autorização; sem fallback inglês |
| GPU física e QA visual de launcher, Display Options, fades e upscaling | **Não executados** | QA em dispositivo real fora desta rodada automatizada headless |
| Android | **Não compilado nem testado** neste sync | Validação de plataforma independente |
| iOS | **Não compilado nem testado** neste sync | Validação de plataforma independente |
| macOS | **Não compilado nem testado** neste sync | Validação de plataforma independente |
| Linux | **Não compilado nem testado** neste sync | Validação de plataforma independente |
| Web | **Não compilado nem testado** neste sync | Validação de plataforma independente |
| `Kotonoha.Infrastructure` / MetalFX | Falha com SDL software renderer **também no upstream puro** | Limitação conhecida de upstream/ambiente; não enfraquecer nem pular o teste |
| `SchoolDaysLauncherTest` | Excedeu 15 segundos com SDL dummy após o sync; classificação no log local | Comparar pré-sync e diagnóstico de 120 s; sem aumento permanente de timeout |
| `ALL_BUILD` DIST | Alvos de teste/utilitários apenas DEV referenciavam APIs deliberadamente ausentes no motor público | Manter esses alvos em DEV/não-DIST, preservando todos os testes DEV; recompilar DIST limpo |
| `settings.json` Windows L4 | Código mantém o arquivo ao lado do executável; `Config.DAT` separado | Smoke em cópia headless, se viável sem GUI, GPU ou assets comerciais |
| Ícone Windows opcional | ICO aprovado é **privado e não versionado** | Worktree limpa deve compilar sem ele; consultar o README School Days |

Este registro é conservador: build Release e CTest sintético aprovados não
certificam o comportamento do jogo comercial, saves reais nem plataformas
não testadas. Preservar `PORT_STATUS.md` e notas históricas. Nenhum GPK,
EXE retail, tradução privada, save real ou ICO opcional deve entrar em
commits públicos.
