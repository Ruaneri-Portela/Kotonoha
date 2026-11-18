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

### Escolha pendente bloqueia o transporte — 17/11/2026

O `SkipToEnd` do School Days não pode mais avançar por cima de um `SetSELECT`
não resolvido. O runtime específico do jogo reconhece a escolha ainda antes
de a janela de interação aparecer, calcula um destino protegido, fixa-o quando
o menu superior inicia o fade preto e revalida esse mesmo destino antes do
salto. Escolha surgida durante o fade, mudança de cena ou avanço do relógio
cancelam o transporte, sem seek, continuação do fade ou confirmação de opção.
O primeiro salto permitido ainda leva a `SkipFRAME - 24` ticks; cenas sem
escolhas e o percurso normal anterior à escolha em `SkipToNextChoice` mantêm
o comportamento existente.

A proteção abrange também a seta DIREITA, a seta ESQUERDA/Back enquanto a
escolha aguarda resposta, controle direto 3 do menu, mouse/toque e entradas
sintéticas de gamepad para skip. A reprodução de Replay Data preserva as
escolhas registradas. Os **botões, o fundo do Top Menu e o medidor Feel** ficam
ocultos durante a escolha não confirmada. O indicador **AUTO ON**, quando
ativado, permanece visível independentemente desses elementos.
Seleção legítima, timeout, AUTO mode, pausa,
velocidade escolhida continuam independentes dessa barreira.

**Correção do menu após escolha (observação direta do usuário em 17/11,
ampliada em 18/11):** depois da seleção ou omissão, a barra superior não
pode reaparecer automaticamente por causa do hover anterior. Seus comandos
permanecem congelados durante a saída da escolha. Quando o mouse entra de
verdade na região superior **durante** o resultado e permanece ali, a MenuBar
pode voltar após a liberação, mesmo sem outro movimento naquele instante.
Com o cursor fora do topo, a mudança de cena não revela o menu. O indicador
AUTO ON continua visível em todas essas fases. Refinamento confirmado pelo
vídeo retail de escolha manual A03 enviado pelo usuário (18/11, ~7,5–11 s).
Sem hover, o medidor de sentimentos aparece sozinho **apenas quando uma
escolha confirmada efetivamente altera os pontos**. A tela de escolhas
pendentes mostra só as opções; uma escolha omissa ou sem delta não cria a
apresentação isolada do Feel. O desbloqueio exige que a próxima cena
física seja associada **e** que a animação do medidor e eventual efeito
`SeUp` / `SeDown` terminem. As durações já documentadas são 3,924172 s e
4,022857 s; o código usa o arredondamento para cima em milissegundos. Se a
escolha não alterar os sentimentos, não se inventa um segundo som nem uma
espera artificial de quatro segundos. O `SeSelect` continua no instante da
confirmação, e Esc/configuração por teclado permanecem como antes.

Testes C++ determinísticos e sintéticos no Windows cobrem escolha visível,
corrida do fade preto, 2/3/10/50 entradas rápidas, DIREITA/ESQUERDA,
controle direto 3, `SkipToNextChoice`, escolha manual, cenas sem escolha,
timeout, botões inertes após seleção, hover/medidor, limites de desbloqueio
para `SeUp` e `SeDown` e escolha sem mudança de sentimentos, além de
sequências pseudoaleatórias com semente fixa (**256
sequências, 12 ações cada, semente `0x5EEDBEEF`**). Os testes existentes
de Replay Data e H-scene foram preservados. As falhas anteriores à correção
estão registradas em `build/_skip-choice-log.md`, fora do Git; não foram
usados scripts retail, saves reais ou GPKs comerciais.

**Não verificado:** teste manual/visual com a mídia comercial, GPU real,
fidelidade exata ao executável original onde a documentação existente não
estabelece os fatos (a observação do menu veio do usuário, sem captura
independente do retail; setas durante escolhas ainda não verificadas), gamepad ou
toque em hardware móvel e compilações para Linux/macOS/Android/iOS. Testes
sintéticos e build Windows não substituem o QA que será executado pelo
usuário. O procedimento está no checklist não rastreado
`build/CHECKLIST-QA-SKIP-CHOICE.md`.

Verificação Windows Release desta feature local, somente com fixtures
sintéticas: build DEV completo **PASS**, CTest DEV **80/81** (apenas a falha
histórica `Kotonoha.Infrastructure`/MetalFX com software), build DIST
completo **PASS** e CTest DIST **74/75** (apenas o teste positivo
`Kotonoha.MCPCommandLine` incompatível, já descrito acima). Ambas as
variantes passaram as sequências aleatórias de barreira do
`SchoolDaysNextChoiceRuntimeTest`. O stress DEV headless executado
separadamente passou **36/36** casos com `--documents` e `--config` isolados;
`ValidateBuildMetadata.py` passou. A incompatibilidade MCP na DIST **não**
atende a um critério rígido de aceitação que permite somente a falha MetalFX.
Nenhum teste em `lib/` foi modificado ou suprimido para escondê-la. O stress
específico da DIST não foi verificado porque `stress_l4.py` atualmente usa
os controles `--node` / `--ktrf`, exclusivos de DEV.

**Regressão de Feel no Back normal (REA 18/11/2026):** análise estática dos
chamadores no EXE original distingue o restore/prune de checkpoint `SP>=100`
do RouteMap (`0x004331A0`) do Back comum da `RouteProcSDHQ`. O port antigo
restaurava o snapshot tag3 **depois** de aplicar a inversão de sentimentos do
Back, reintroduzindo `002=5` ao retornar de A04 para A03 quando o correto era
`002=0`. A correção preserva o restore de checkpoints no RouteMap e remove
somente a reaplicação indevida no Back normal. O teste reproduz escolha/Back
repetidos, ramificação alternativa A05 e Save/Load de escolha ainda pendente.
Detalhes em
`games/school-days/docs/parity/BACK_FEEL_CHECKPOINT_REA_2026-11-18.md`.
O instante exato do desbloqueio da barra pós-escolha no original ainda
necessita comparação visual independente do retail.

**Top Menu / sobreposição do Feel (REA 18/11/2026, corrigida após novo relato):** no original,
`SysMenuSDHQ.dll` desenha separadamente o quadro e os preenchimentos do Feel,
em região própria `x=116..654, y=3..25`. A rotina `0x10025690` não aplica o
fade do Top Menu aos sprites do Feel enquanto a atualização de sentimentos
está ativa; `0x10026B40` os inicializa com opacidade integral. A correção
inicial do port exibia equivocadamente o medidor **antes de responder à
escolha** e o forçava depois de qualquer confirmação. Agora, o prompt mostra
**apenas as escolhas**, com `SeView` (código5), e a seleção emite `SeSelect`
(ou `SeCancel` na omissão). Somente a detecção de mudança verdadeira de pontos
emite `SeUp`/`SeDown` (códigos3/4) e exibe o Feel isolado. Se não houver delta,
o menu congelado permanece sem chrome, mesmo após uma atualização tardia sem
mudança real. Mesmo se uma regra de omissão modificar `001`/`002`, a origem
de escolha bloqueia que o Feel apareça tardiamente após o congelamento.
A correção limpa o hover/alpha herdado ao entrar na escolha e
mantém o AUTO ON desenhado separadamente. O Feel permanece independente do
alpha do Top Menu quando ativado;
Skip/SuperSkip não atravessam escolhas.
A regressão com leitura de pixels do SDL/software comprova que uma escolha
pendente não mostra Feel nem chrome, que omissão ou ausência de delta não
criam um medidor isolado e que, quando ativo, o Feel não é coberto pelo menu.
A análise estática original também confirmou o despacho de Skip e a ordem
`SkipFRAME == SetSELECT < Next` nas 287/287 cenas de escolha auditadas.
Detalhes em
`games/school-days/docs/parity/TOP_MENU_FEEL_COMPOSITION_REA_2026-11-18.md`.
O momento exato de desbloqueio nativo dos botões após uma escolha ainda não
está comprovado; a duração do OGG sozinha não demonstra esse predicado.
