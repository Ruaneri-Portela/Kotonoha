# Auditoria de divulgação — School Days HQ (04/11/2026)

## Veredito

**Apta para demonstração pública como versão alfa, com limitações declaradas. Não
aprovada como remaster equivalente ao original nem como versão final.** O núcleo
de roteiro, persistência e telas principais é funcional, mas esta auditoria não
executou todas as 55 rotas, 22 finais, cenas de Replay, variantes de vídeo e
configurações de tela. A publicação deve exigir dados fornecidos pelo próprio
usuário; nenhum GPK, save original ou captura desta auditoria pertence ao pacote.

Esta é uma avaliação técnica de funcionamento, não uma análise de direitos de
distribuição, nomes ou conteúdo. O executável original foi usado apenas como
referência de comportamento em uma instalação local.

**A pasta de QA `build/school-days-dev-control2/games/school-days/Release`
não pode ser publicada:** contém `Packs/` com os 29 GPK, `Save/` com saves e
`GlobalFlag.DAT` originais, além da ponte `KOTONOHA_DEV_CONTROL=ON` e outros
artefatos de teste. A avaliação de alfa acima se refere ao código do jogo e
exige uma compilação pública limpa em diretório novo.

Foi preparada `build/school-days-public-candidate/public-staging/` como candidata
local de alfa, sem divulgação automática. Ela foi compilada com
`KOTONOHA_DEV_CONTROL=OFF`, contém executável, DLLs e recursos do próprio
projeto, e não contém os arquivos originais mencionados acima.

## Fontes e perfis

- [Pesquisa v1.02](https://github.com/tsubaki-0x/School-Days/blob/master/docs/VERIFIED_V102_RUNTIME.md): o ensaio A/B/C/D do original demonstrou que `GlobalFlag.DAT` sozinho habilita o título concluído e Replay; `SaveFile*.DAT` sozinho não habilita.
- [Kotonoha público](https://github.com/tsubaki-0x/Kotonoha): o README ainda classifica o projeto como Alpha. O checkout local v3 foi a base da compilação e das mudanças; o repositório público não foi presumido idêntico a esta árvore.
- Original instalado em `D:\SCHOOL DAYS HQ\Overflow\SCHOOLDAYS HQ`; build recompilada em `build/school-days-dev-control2/games/school-days/Release/SchoolDays.exe`.
- Perfil limpo: diretório `--documents` temporário, sem `GlobalFlag.DAT` nem saves.
- Perfil concluído: cópia isolada do `GlobalFlag.DAT` original; Replay Data foi testado depois com cópias isoladas dos `SaveFile*.DAT` originais.
- As capturas locais estão em `build/school-days-release-audit/`, ignorado pelo Git. Elas contêm arte proprietária e **não devem ser distribuídas**.

## Observações em runtime

| Fluxo | Original v1.02 | Kotonoha v3 após correção | Resultado |
|---|---|---|---|
| Título concluído | Arte AllClear e REPLAY habilitado | Mesma família de arte e REPLAY habilitado só com `GlobalFlag.DAT` | Passou no perfil isolado |
| Título limpo | Estado padrão e REPLAY bloqueado, conforme ensaio A/B/C/D publicado | Estado padrão e REPLAY cinza/bloqueado | Passou na build; original limpo apoiado pelo ensaio publicado |
| EXIT no título | Popup “End this game?” com título escurecido ao fundo | Mesmo recurso visual, agora composto com transparência sobre o título | Passou visualmente, nos perfis limpo e concluído |
| YES/NO no EXIT | Confirmação/cancelamento do original | NO volta ao título; YES encerra com código 0 | Passou na build |
| Pergunta de salvar ao fim do episódio | `SaveFeelView` tem contagem de 30 segundos documentada na pesquisa v1.02 | O teste de runtime abriu SaveFeel após o handoff, entrou em SAVE, gravou slot 0 e avançou exatamente ao episódio seguinte | Passou em runtime automatizado; sem comparação visual lado a lado |
| Replay H-scene | Grade inicial e primeira cena com o quarto do personagem | Grade e primeira cena `02/02-2S-W03` renderizadas em runtime | Passou como amostra; não prova todas as cenas |
| Replay Data | Dez registros e comentários dos saves originais na primeira página | A mesma primeira página de metadados com os saves copiados; selecionar um inicia sessão `playDataOpen=true` em `00/00-00-A00` | Passou como amostra |
| TOPMENU em gameplay | Pergunta antes de retornar ao título | Abre “Return to Title Screen?” pelo MenuBar | Passou na cena de inspeção; fundo da cena estava preto no instante do salto `--node` |

O teste de Replay foi observado em execução, e não apenas em modelo: a primeira
cena mostrou o mesmo quarto do original após a transição. Os textos capturados
estavam em tempos diferentes, portanto as imagens não são uma comparação de
pixels ou de sincronização quadro a quadro. O teste `SchoolDaysReplayRuntimeUiTest`
também confirmou abertura da grade, PlayData, Save/Load/BackLog temporários,
isolamento da narrativa e retorno de propriedade de BGM.

Uma verificação adicional separou movimento do cursor e clique no Replay original:
a primeira miniatura já estava colorida após o movimento, antes de clicar; um
único clique escureceu a grade e iniciou a transição. Isso esclarece a aparente
necessidade de dois cliques observada numa tentativa anterior. A build também
iniciou a cena com um clique na miniatura.

## Correção aplicada nesta auditoria

- `SchoolDaysSystemUi::OpenExit` usa `SDL_BLENDMODE_BLEND` na base
  `Popup_Exit/Popup_Title`: o PNG original possui alfa entre 221 e 255; antes
  era desenhado opaco e tornava o fundo preto.
- `Runtime::renderMenu` mantém a arte do título sob `ExitView`, incluindo o
  título concluído. Outros painéis continuam substituindo a arte do título.
- Se a imagem de confirmação faltar ou falhar, `Runtime::openExit` informa o
  erro e mantém a sessão aberta. Antes, o botão EXIT enviava `SDL_EVENT_QUIT`
  sem mostrar a pergunta. O teste `SchoolDaysHandoffRuntimeTest` agora verifica
  explicitamente que nenhum evento de saída é enfileirado nesse caso.
- `SchoolDaysAssets::resolve` recupera uma voz existente em `Voice03.GPK`
  quando o ORS `03-BK-H00` pede seu nome com um espaço indevido. A busca da
  grafia exata continua tendo prioridade para instalações extraídas e overlays.
- Nenhum arquivo do motor compartilhado em `lib/` foi modificado.
- O teste opcional `SchoolDays.Retail` agora recebe no Windows o mesmo caminho
  de DLL do motor usado pelos demais testes. Antes, o CTest podia parar na
  caixa de erro do carregador sem executar o censo.

Antes: a captura de EXIT na build mostrava pergunta e botões sobre fundo preto.
Depois: a captura mostra título escurecido como no original. A recompilação
Release e a execução com os dois perfis confirmaram o resultado.

## Gates executados

- `cmake --build build/school-days-dev-control2 --config Release --target SchoolDays`: passou.
- `ctest --test-dir build/school-days-dev-control2 -C Release -R '^SchoolDays(ReplayRuntimeUiTest|UiModelTest|HandoffRuntimeTest|MenuBarRuntimeTest|ReplayRuntimeIsolationTest|FrontendTransitionTest)$' --output-on-failure`: 7/7 passaram, incluindo a fixture `SchoolDaysPortTest`.
- O executável de testes precisou da fonte `NotoSansJP-Regular.ttf` em sua
  pasta `assets/fonts`; ela já existe na distribuição do jogo. A ausência da
  cópia no diretório de teste causou as primeiras falhas do teste, sem indicar
  defeito na build do jogo.
- `git diff --check`: sem erros de whitespace.
- `ctest --test-dir build/school-days-dev-control2 -C Release -R '^SchoolDays\.Retail$' --output-on-failure`:
  1/1 passou lendo os 29 GPK originais, 1.857 ORS e 86.536 eventos; as 1.855
  referências de roteiro do KTRF encontraram ORS. Esse censo verifica leitura
  e correspondência de nomes, não a execução visual de cada evento.
- Compilação pública em diretório novo: `BUILD_TESTING=OFF`,
  `KOTONOHA_DEV_CONTROL=OFF`, `KOTONOHA_BUNDLE_LOCAL_GAME_ASSETS=OFF`;
  `SchoolDays` Release compilou sem erro. O staging por lista positiva tem
  82 arquivos, 47.775.769 bytes, sem `Packs/` ou `Save/` e zero arquivos
  `.GPK`, `.ORS`, `.WMV`, `.OGG`, `.DAT`, `.PDB` ou `.LIB`.
- O executável do staging, com `--launcher --no-config` e sem dados do jogo,
  mostrou o seletor de pasta e a rota válida. Com `--school-days` apontando
  somente para a instalação original externa, mostrou o título limpo;
  `--frames 250` encerrou com código 0. As capturas estão apenas no diretório
  local ignorado pelo Git.
- Após a correção do fallback de EXIT, `SchoolDaysHandoffRuntimeTest` passou
  novamente (2/2 com sua fixture), a candidata pública foi recompilada, o
  executável do staging foi atualizado com hash idêntico, a varredura proibida
  permaneceu em zero e o smoke test do staging encerrou com código 0.
- Após recuperar a voz com nome incorreto, `SchoolDaysAssetsListTest` passou
  tanto no fallback quanto na prioridade da grafia exata. `SchoolDays.Retail`
  passou lendo a voz real de `Voice03.GPK` por meio do resolvedor do jogo e
  verificando o cabeçalho `OggS` (2/2 testes com a fixture local). O estágio
  público foi recompilado, passou novamente no smoke test de 250 quadros com
  dados externos e mantém zero arquivos originais. SHA-256 do executável:
  `C3A33EAAEB1E18A7016E2FB1DFD977630AD6EAF6FA520AF12114D2CB9C805D17`.

## Limites para uma versão final

1. Falta uma campanha completa automatizada ou manual percorrendo as 55 rotas,
   22 finais, handoffs, saves e Replay de todos os itens. Esta auditoria prova
   os fluxos amostrados, não paridade integral.
2. Transições entre título, Replay, Replay Data e gameplay ainda não têm
   correspondência de quadro e tempo demonstrada em todos os caminhos. O
   diálogo de EXIT foi corrigido e testado; isso não fecha os outros caminhos.
3. Variantes Standard/Wide/WideFull/WideNote, trocas de modo de tela e fontes
   ainda precisam de comparação visual em runtime. A tipografia de escolhas
   usa fonte TTF do Kotonoha e pode diferir do raster original.
4. A instalação original não contém 138 vozes e um fundo citados nos ORS.
   Uma referência de voz adicional contém um espaço indevido no roteiro; seu
   arquivo existente foi recuperado por uma correção específica do jogo.
   As seis introduções BGM ausentes usam agora seus loops existentes. O
   inventário detalhado está em [MISSING_RETAIL_ASSETS.md](MISSING_RETAIL_ASSETS.md).
5. A restauração de `SystemBGM` em Replay passou nos testes internos, mas não
   recebeu comparação auditiva com o original. O streaming de filmes GPK
   grandes também não recebeu gate de memória prolongado.

Critério para promover além de alfa: percorrer todos os finais e entradas de
Replay com logs de estado/áudio/vídeo, comparar transições e variantes de tela
com o original, e repetir o ensaio de perfil limpo e concluído na distribuição
sem dados incluídos.
