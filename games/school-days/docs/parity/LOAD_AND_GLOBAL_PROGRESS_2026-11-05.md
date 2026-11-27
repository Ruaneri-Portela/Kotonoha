# School Days HQ v1.02: LOAD, progresso global e Route Map

Investigação e validação em 05/11/2026. O código específico continua em
`games/school-days`; o motor compartilhado não foi modificado nesta etapa.

## LOAD de um save comum

Antes, `Runtime::loadSlot()` lia `SaveFile*.DAT`, restaurava o estado e
imediatamente abria a cena do checkpoint. Isso ignorava a tela de espera do
original.

No jogo comercial v1.02, o ensaio visual de LOAD pelo título, no primeiro
slot de um perfil completo isolado, mostrou:

1. O painel LOAD escurece progressivamente após o clique. As capturas de
   aproximadamente 0,05, 0,4 e 0,9 s documentam essa saída.
2. A partir de aproximadamente 1 segundo aparece a tela com a frase
   **"Please wait. Now loading."**, visível integralmente nas capturas de
   1,25 e 1,7 s.
3. A tela de espera escurece até preto (captura de 2,4 s) e o conteúdo
   restaurado aparece depois (capturas de 3,1 e 4,3 s).

O arquivo correto é `System/SAVELOAD/POPUP_LOAD.PNG`, embutido em `System.gpk`.
Essa textura original de 800×450 já contém a frase, moldura e fundo. Não é
necessário recriar o texto nem incluir a imagem na distribuição. Sua extração
foi feita somente para comparação local.

A projeção implementada em `SchoolDaysLoadTransition.hpp` e
`SchoolDaysRuntime.cpp` mantém o painel enquanto escurece, exibe o asset
original, escurece a tela de espera até preto e só então chama `loadSlot()`.
Uma revelação preta final cobre a troca de cena. Os tempos são aproximados das
capturas de runtime, ainda não confirmados por rastreamento de relógio interno
do binário comercial: saída 1000 ms, exibição 900 ms, saída do card 500 ms,
revelação 500 ms. SAVE/LOAD privado de Replay Data continua com seu próprio
fluxo e não assume a apresentação da narrativa principal.

Capturas do original e da control4 ficam somente em
`build/school-days-load-audit/` (pasta ignorada pelo Git). O teste de runtime
usou saves e packs originais **em um perfil isolado**, sem distribuir imagens.

## O que significa "100%"

Há três conceitos distintos no retail:

- `SaveFile*.DAT` guarda um checkpoint de um slot; sua presença não conclui o
  título e não desbloqueia Replay por si só.
- `GlobalFlag.DAT` guarda cenas lidas, checkpoints SP e estado dos finais. Sua
  existência com as flags corretas é suficiente para o título concluído e
  Replay, mesmo sem SaveFiles, conforme a matriz original A/B/C/D em
  `Ferramentas/SchoolDaysHQ-RE-Lab/docs/SAVE_MODE_100_PERCENT_PROBE.md`.
- `AllClear` representa a conclusão do conjunto de 22 finais e é uma condição
  separada da porcentagem visual do Route Map. O original conta `End00...` em
  EXE `0x00420040` e promove `AllClear` em `0x0041FEE0` quando o conjunto está
  completo (pesquisa `SPEC-GLOBALFLAG.md`). Um final individual registra sua
  própria condição; não equivale a 100% nem a AllClear.

O Route Map obtém o percentual do número de cenas marcadas lidas, com um total
de 1825 cenas contabilizáveis. O original calcula
`GetReadScriptCount()*100/GetScriptMax` em `SysMenuSDHQ.dll:0x1000C8A0`;
Kotonoha implementa a projeção em `SchoolDaysRouteMapOracle.cpp`. O valor é
inteiro truncado. Assim, 18 cenas equivalem a 0%, e 19 cenas passam a exibir
1%. Um único episódio completo não equivale necessariamente a 1%, pois a
quantidade de cenas percorridas varia conforme a rota.

## Fechar sem criar um save manual

O progresso de leitura é global e diferente do ponto de retomada do slot. O
original grava GlobalFlag no fechamento normal e também em outros pontos de
persistência; o contrato documentado inclui término natural sem modificar os
SaveFiles. A v3 registra cenas concluídas no progresso global e chama
`flushGlobal()` em passagens de cena, handoffs, retorno ao título e shutdown,
independentemente de `saveSlot()`. Portanto fechar normalmente depois de
concluir cenas não deveria zerar a porcentagem obtida. O ponto exato dentro
da cena para retomar depois depende de um checkpoint/salvamento; a porcentagem
e a posição de retomada não são a mesma coisa.

Crash ou encerramento forçado não foram comprovados com a mesma garantia. A
marcação ocorre quando uma cena completa seus eventos, não em cada frame ou
segundo exibido.

## Bolinhas/nós do Route Map

No original, uma seleção disponível representa um selector `SP%03d` que pode
restaurar um checkpoint tag3 do SaveLog. Na v3:

- `SchoolDaysRouteMapModel::Activate()` emite `NodeRequested` para um nó
  disponível; `Runtime::loadCheckpoint()` despacha a seleção para
  `SchoolDaysKtrfGameplayBridge::LoadRouteMapSelector()`, que monta a cena.
- A cor azul no mapa depende de uma flag `SP...` no GlobalFlag. Essa flag
  **não garante** que o SaveLog atual já contém o registro tag3 necessário.
- `SchoolDaysKtrfSession::PlanRouteMapCheckpoint()` exige um registro tag3 do
  selector na sessão. Em inicialização fria, apenas o GlobalFlag é carregado.
  Portanto um nó azul pode não conseguir iniciar o salto sem um SaveLog válido
  dessa sessão. O tratamento agora valida o checkpoint antes de aposentar a
  cena anterior e mantém o Route Map aberto caso a ativação falhe.

Foi também testado na v3 um perfil contendo **somente** o `GlobalFlag.DAT`
concluído, sem SaveFiles: o título abre o Route Map em 100% com nós azuis;
clicar no primeiro nó manteve o Route Map aberto e não trocou de cena. As
capturas do teste estão em `build/school-days-load-audit/kotonoha-global-only-*`.

Não há evidência suficiente para criar checkpoints fictícios ou mapear um SP
global diretamente a uma cena; isso poderia saltar escolhas e estados de rota.
É necessário comparar com o executável original a seleção de nós no perfil
somente GlobalFlag, após carregar um SaveFile, e durante gameplay. Uma tentativa
visual em Episódio 2 no original não mudou de tela, mas isso sozinho não prova
qual condição bloqueou a ativação.

## Limites da comparação

As telas de LOAD com save foram comparadas em runtime e o asset foi encontrado
no pack original. Os percentuais de perfis completamente vazio/completo foram
comparados em auditorias anteriores, e a fórmula da contagem possui evidência
estática do original. A experiência de atingir **exatamente um final partindo
de um perfil limpo** ainda precisa de um percurso controlado; por isso nenhum
  percentual específico para "um final" é afirmado neste documento.

## Compilação e testes

As builds Windows Release `build/school-days-dev-control2` (suíte) e
`build/school-days-dev-control4` (executável DEV) compilaram. O teste
`SchoolDaysFrontendTransitionTest` valida as fases, a emissão única da
restauração e o bloqueio de entrada. O `SchoolDays.Save` foi ajustado para
aguardar a transição antes de testar os controles da cena restaurada.
Após recompilar, `ctest --test-dir build/school-days-dev-control2 -C Release
--output-on-failure` terminou com **57/57 testes passando**. No teste em
runtime, a control4 abriu um save real, exibiu o card original e restaurou a
cena sem falhas. A verificação de um único final em perfil limpo permanece
pendente de um percurso controlado.
