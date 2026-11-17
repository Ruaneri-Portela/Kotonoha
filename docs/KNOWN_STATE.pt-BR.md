# Estado conhecido após o sync upstream/dev — 2026-11-15

[English (US)](KNOWN_STATE.md) | **Português (Brasil)**

Escopo: mescla local Windows L4 de School Days HQ v1.02 com upstream/dev
`fdd0b041` sobre a dev pré-sync `18620ef9`.
A branch de integração começou em `3faacd0f`; a revisão de pendências
prosseguiu após `c0941303`. São **hashes de referência**, não garantias
de distribuição. Consulte o registro local, não versionado,
`build/_sync-final-log.md` para comandos e decisão da publicação.

### Fonte Noto Sans JP local exigida pelos testes headless

Obtenha a fonte TrueType autêntica **Noto Sans JP Regular** no projeto oficial
[Google Fonts / Noto Sans JP](https://fonts.google.com/noto/specimen/Noto+Sans+JP)
(SIL Open Font License 1.1). Use o arquivo verdadeiro
`NotoSansJP-Regular.ttf`, não o placeholder pequeno que uma árvore Git limpa
pode preparar. Coloque-o em `assets/fonts/NotoSansJP-Regular.ttf` na fonte
local, **sem comitar o arquivo**. Para o CTest Windows Release, prepare também
os seguintes destinos ignorados pelo Git:

- `build/<build-dir>/bin/Release/assets/fonts/NotoSansJP-Regular.ttf` para
  `Kotonoha.Presentation` (`KotonohaPresentationTests.exe`) e
  `SchoolDaysPortTest.exe`, que também prepara os fixtures sintéticos
  `school_days_assets` usados por testes posteriores.
- `build/<build-dir>/outputs/Release/SchoolDays/assets/fonts/NotoSansJP-Regular.ttf`
  para `SchoolDaysRuntimeSmoke` (`SchoolDays.exe`).

Recompile ou faça o staging dos assets depois de instalar a fonte; se o
empacotamento já tiver ocorrido, copie a fonte licenciada para essas pastas
ignoradas do build. A fonte ausente ou sintética causou **73/77** em vez de
**75/77** na DEV limpa; com a fonte correta, restaram somente as duas falhas
conhecidas de Infrastructure/MetalFX e timeout do launcher. Não versionar
bytes dessa fonte, nem usar arquivos retail ou saves reais.

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
| `SchoolDaysLauncherTest` | Launcher com SDL dummy passa no diagnóstico de linha de comando com 120 segundos (~26 segundos), mas excede o limite original de 15 segundos. Na dev pré-sync, a dependência `SchoolDaysPortTest` excede 15 segundos antes de executar o launcher | Lentidão headless/fixture; não há prova de defeito introduzido pela mescla; não aumentar timeout versionado |
| `ALL_BUILD` DIST | **PASS** em worktree limpa Windows Release após `96c3c03` restringir testes/utilitários dependentes de debug a builds não-DIST. Os mesmos símbolos de debug já falhavam na pré-sync `18620ef9` | Compilar e executar todos os testes de debug em DEV; não incluir API de debug no público. Gameplay real DIST não verificado |
| `settings.json` Windows L4 | **Smoke headless aprovado** em cópia Release, com dados sintéticos, SDL dummy, `--documents` isolado e sem `--config`. Arquivo criado ao lado do EXE copiado e não no `%APPDATA%` isolado; `Config.DAT` separado | Perfil real do jogador não foi acessado |
| Ícone Windows opcional | ICO aprovado é **privado e não versionado**; worktree limpa sem ele **configurou e compilou com sucesso** | Continuar fora do Git; consultar o README School Days |
| Paridade compartilhada em `lib/` | Seis divergências de comportamento (API/renderização de legendas, culling do backlog, fallback de overlay absoluto e API de tradução por seção) foram restauradas byte a byte de `upstream/dev`, uma por commit | Ver `docs/LIB_DIVERGENCES.pt-BR.md`. Paridade de execução/visual em outras plataformas não testada; build Windows e análise de usos são as evidências autorizadas |
| Divergência upstream de `Kotonoha.Directories` / `StoragePaths` | **Reproduzida no upstream puro `fdd0b041`:** `Kotonoha.Directories` falha com `Desktop Documents root excludes the save directory` usando SDL dummy. `lib/src/Filesystem/StoragePaths.cpp` é idêntico ao upstream. O teste upstream espera `layout.documents == base` e `layout.saves == base / "save"`, mas o código upstream usa `base / "Kotonoha" / locations.packageIdentifier` e a subpasta `save` desse diretório | O teste local mantém expectativas mais rigorosas e corretas para o namespace, e passa. O upstream deveria ser avisado em fluxo autorizado separadamente; **nenhum push ou PR upstream** foi realizado |

**Problema exato a relatar ao upstream (não enviado):** em
`lib/tests/DirectoryTests.cpp`, o laço desktop que chama
`directoryLayout(locations, "amagami")` exige `layout.documents == base`
(`Desktop Documents root excludes the save directory`) e
`layout.saves == base / "save"`. Porém, no *mesmo* commit upstream,
`directoryLayout` em `lib/src/Filesystem/StoragePaths.cpp` define
`result.documents = locations.userData / "Kotonoha" /
locations.packageIdentifier` e depois
`result.saves = result.documents / "save"`. Essas expectativas são
incompatíveis quando o identificador do pacote não está vazio. Falha
reproduzida no `upstream/dev` puro `fdd0b041`, sem mudar
`StoragePaths.cpp` nem enfraquecer o teste local com namespace.

| Cores das legendas | **cores de legenda sem verificação visual**; `styles.ass` contém cores de design e correção da Setsuna, com testes sintéticos apenas | Correspondência de estilos testada em modo headless; aparência real não avaliada |

### Verificação POSIX independente da gravação GlobalFlag — 17/11/2026

O helper específico do jogo `SchoolDaysAtomicFile` foi isolado em C++17,
sem dependência de SDL, vcpkg ou do motor. Seu teste standalone compilou e
passou no Windows local e nos runners POSIX hospedados pelo **GitHub Actions**
do fork (não em uma instalação Linux local):

| Runner | Imagem e versão registradas no CI | Resultado |
| --- | --- | --- |
| `ubuntu-latest` | `ubuntu-26.04`, `20261111.196.1` | C++17 nativo e ASan/UBSan **PASS** |
| `macos-latest` | `macos-26-arm64`, `20261102.0473.1` | C++17 nativo **PASS** |

Execuções: <https://github.com/tsubaki-0x/Kotonoha/actions/runs/44013431802>
e <https://github.com/tsubaki-0x/Kotonoha/actions/runs/44014281654>
(17/11/2026 UTC). Os testes cobrem 1.000 substituições, falhas injetadas
de escrita/flush/rename/fsync do diretório, limpeza de órfãos antigos e 220 tentativas
aleatórias de interrupção via SIGKILL em cada runner POSIX. No macOS, o helper
usa `F_FULLFSYNC` quando disponível, com fallback para `fsync` somente em
`ENOTSUP`, e trata `EINTR` e escritas parciais. Isso verifica o **helper de
persistência standalone**, não a compilação completa do School Days para
Linux/macOS, resistência física a perda de energia nem a execução do jogo
comercial nessas plataformas. O workflow permanece só em uma branch CI
temporária do fork, fora das branches de distribuição.

Este registro é conservador: build Release e CTest sintético aprovados não
certificam o comportamento do jogo comercial, saves reais nem plataformas
não testadas. Preservar `PORT_STATUS.md` e notas históricas. Nenhum GPK,
EXE retail, tradução privada, save real ou ICO opcional deve entrar em
commits públicos.

### Incompatibilidade conhecida do teste MCP na DIST — 17/11/2026

`Kotonoha.MCPCommandLine` é registrado em
`lib/tests/CMakeLists.txt:20-22`, fora do escopo autorizado de alterações
específicas do jogo. O teste positivo inalterado em
`lib/tests/MCPCommandLineTests.cpp:39-60` exige `--mcp` / `--mcp-port`, mas o
parser da DIST exclui deliberadamente essas opções em
`lib/src/core/CommandLine.cpp:24-42`. Por isso, **falha na DIST** devido à
expectativa positiva incompatível, sem indicar MCP ativo no executável
público. O teste original continua registrado, executável e inalterado;
não foi pulado, desativado nem enfraquecido. O teste complementar
`SchoolDays.DistDebugAbsence` passa na DIST e comprova a rejeição de MCP,
porta/raiz MCP e logging de debug. CTest Windows Release final desta feature:
DEV **80/81** (só a falha conhecida MetalFX/software) e DIST **74/75**
(só o teste positivo MCP registrado na engine).
