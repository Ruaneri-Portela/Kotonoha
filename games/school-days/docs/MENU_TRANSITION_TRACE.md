# Transições do EXIT e TOPMENU no School Days HQ v1.02

Data: 04/11/2026. Escopo: animação de abertura e cancelamento de
`MENU::ExitView`, confirmação de EXIT no título e TOPMENU durante gameplay.

## Evidência em execução

O executável original foi iniciado em cópias isoladas dos perfis `A-full` e
`D-empty`. As capturas sequenciais estão somente em
`build/school-days-release-audit/animation/` (ignorado pelo Git; contém arte do
jogo original). A amostragem GDI não é uma medição exata de quadro; os tempos
numéricos abaixo vêm das chamadas no binário.

| Ação | Observação no original |
|---|---|
| EXIT no título | Uma área escura começa no topo e cresce até cobrir a tela; o diálogo “End this game?” aparece depois do varrimento. |
| NO no EXIT | O diálogo desaparece; a área escura perde opacidade e o título volta a ficar claro. |
| YES no EXIT | Diálogo e título escurecem progressivamente até preto; o processo termina com código 0. |
| TOPMENU durante gameplay | A cena recebe o mesmo varrimento vertical antes de aparecer “Return to Title Screen?”. |
| YES no TOPMENU | Cena e diálogo escurecem até preto; após a troca de estado, o título reaparece gradualmente. |

Quadros úteis: `exit-popup-open-01/02/03/04`,
`exit-cancel-choice-confirm-01/03`, `exit-yes-confirm-01/03/05`,
`topmenu-popup-open-02/03/04/05` e `topmenu-yes-confirm-04/12/18`.

## Rastreamento estático

O projeto Ghidra existente `ReplayDeepLab` foi consultado em modo somente
leitura, com o Ghidra 12.1.4 da pasta `Ferramentas`. Hashes originais:
`SCHOOLDAYS HQ.exe` =
`E4E09563F44A57A023884FB8F023138A4E2168B9FE174B363FE1CFCFB3A98E26`;
`SysMenuSDHQ.dll` =
`494FE7851FE68AE7EB09ED13808D19A45B9D6509E167AECE628A65C3014E8836`.
As decompilações locais estão em
`build/school-days-release-audit/animation-ghidra.txt`.

| Rotina | Papel demonstrado |
|---|---|
| EXE `0x0041DFA0` | Máquina de estados compartilhada do diálogo: prepara camada, chama o varrimento na abertura, inicializa a SysMenu e aplica a transição de fechamento. |
| EXE `0x00427E30` | Carrega a camada usada na transição. |
| EXE `0x00427EA0` | Despacha os três modos da camada. O modo 1 chama `0x0042F6A0`; o modo 0 chama `0x0042F5B0`. |
| EXE `0x0042F6A0` | Durante **500 ms**, calcula `alturaVisível = alturaTotal × tempoDecorrido / 500` e chama `0x0042F5B0`; isto explica o limite horizontal descendo nos quadros. |
| EXE `0x0042F5B0` | Modula a opacidade da camada por `tempoDecorrido × 255 / duração`; no fechamento com modo 0, a opacidade diminui. |
| EXE `0x00427D20` | Inicializa `SysMenuSDHQ` após a abertura da camada. |
| DLL `0x1000A660` | Inicializa `MENU::ExitView` com `System/Exit/Popup_Exit.png` ou `Popup_Title.png` e os dois controles de escolha. |
| EXE `0x0041EF60` | Controla fades de tela por tempo e opacidade. `0x0041E600` chama esta função com **750 ms**, cor preta, no caminho de encerramento. Outras chamadas de `0x0041D410`/`0x0041D9C0` usam **500 ms** ou **1000 ms** conforme a troca de modo. |

`0x0041E600` é o despachante externo dos modos; os caminhos de título e
gameplay voltam à máquina de estados `0x0041DFA0`. A associação entre cada
ramo interno de `getNextMode` e cada clique ainda precisa de rastreamento
dinâmico de chamadas para ser tratada como certeza, mas os dois caminhos foram
observados visualmente com o mesmo efeito.

## Onde mais as rotinas aparecem

O cruzamento de referências do executável identifica os seguintes chamadores.
`0x00427EA0` executa a transição da camada do sistema; `0x0041EF60` executa
fades de tela inteira. Os dois efeitos têm usos distintos.

| Chamador | Uso visto na descompilação |
|---|---|
| `0x0041DFA0` | Diálogo EXIT/TOPMENU: modo 1 durante 500 ms na abertura; modo 0 durante 500 ms no cancelamento. Também chama `0x0041EF60` por 500 ms após resultado afirmativo. |
| `0x00426620` | Painéis `setSystemInit` do fluxo de gameplay: modo 1 durante 750 ms antes do painel; modo 0 durante 500 ms ao fechá-lo. O rastreamento em execução identificou SAVE, LOAD, BACKLOG e OPTION como os quatro painéis acessíveis pela barra durante gameplay. |
| `0x00426BD0` | Modo 2 durante 500 ms na entrada e 500 ms na saída, com chamada a `SetFullFlag(1)`. O despachante entra nesse caminho pelo estado 6, armado por `0x0042A560` ao receber mensagens `0x8010` ou `0x8011`. Isso não demonstra uma ligação direta ao botão FULL de OPTION. |
| `0x0041D410` e `0x0041D9C0` | Fades de título, entrada em gameplay e troca entre título e telas filhas: 500 ou 1000 ms conforme o ramo. |
| `0x0041F110` | Entrada e saída da sequência de logo: fades de 1000 ms. |
| `0x0041E600` | Encerramento externo: fade preto de 750 ms antes de finalizar o processo. |

O arquivo `build/school-days-release-audit/animation-use-sites.txt` contém as
descompilações dos chamadores de `0x00427EA0`. Os endereços acima são do
executável original, não do Kotonoha.

## Textura e implementação Kotonoha

`System/SYSTEM/SYSBASE.PNG`, presente em `System.gpk`, mede 800 × 450 e tem
o padrão escuro quadriculado visto nos quadros do original. A associação
visual foi conferida com a textura extraída para a pasta de auditoria; ainda
não foi encontrado o ponto que grava esse nome no campo `+0x5AC` do host.

No **módulo do jogo**, `SchoolDaysExitTransition` agora controla EXIT e
TOPMENU: varrimento vertical de 500 ms antes do popup, redução de opacidade
por 500 ms após NO, fade preto após YES (500 ms para TOPMENU; 750 ms para
encerramento), bloqueio de entrada durante a transição e revelação preta do
título após TOPMENU. O desenho usa `SYSBASE.PNG` diretamente do GPK montado,
sem distribuir cópia do asset. `SchoolDaysFrontendTransition` continua a
atender as transições já implementadas de START, LOAD, CONFIG e REPLAY.

O módulo do jogo também reproduz a sequência observada em `0x00426620` nos
quatro painéis da barra durante gameplay: preto para branco em 750 ms antes de
abrir o painel, painel para branco em 500 ms ao fechar e revelação da cena em
500 ms. A entrada fica bloqueada durante cada fase animada. O estado aberto do
painel conserva o fluxo existente de SAVE/LOAD, BACKLOG e OPTION. O código fica
em `SchoolDaysSystemPanelTransition.hpp` e `SchoolDaysRuntime.cpp`, sem mudança
na biblioteca do motor.

O caminho `0x00426BD0` não foi associado ao botão FULL: no teste do original,
esse botão recebeu hover mas a janela continuou em 800 × 450 com WINDOW
selecionado. A ligação demonstrada estaticamente é com mensagens externas
`0x8010` e `0x8011`; reproduzir seu efeito no botão FULL seria especulativo.
Na captura Kotonoha desse perfil, OPTION mostra FULL selecionado e acrescenta
o controle de DISPLAY OPTIONS, enquanto o original mostra WINDOW selecionado.
É uma diferença de apresentação/configuração observada, não corrigida por esta
mudança da animação; a captura isolada não determina a causa do estado FULL.

## Verificação após a alteração

Build `build/school-days-dev-control4`, configuração Release, recompilada.
Capturas em execução de EXIT→NO, TOPMENU→YES e EXIT→YES estão em
`build/school-days-release-audit/kotonoha-exit-transition/` (ignorado pelo Git).
O último EXIT→YES encerrou o processo com código 0. Os testes
`SchoolDaysFrontendTransitionTest`, `SchoolDaysHandoffRuntimeTest` e
`SchoolDaysMenuBarRuntimeTest` passaram; `SchoolDaysPortTest` executou como
fixture desses testes. A correção da área física do fade foi validada em
1366 × 768, sem faixa descoberta na borda inferior.

Após a integração dos painéis, a build Release `school-days-dev-control4` foi
executada novamente com o perfil `D-empty` isolado. As capturas de SAVE, LOAD,
BACKLOG e OPTION, da abertura ao retorno à cena, estão em
`build/school-days-release-audit/kotonoha-system-panels/`; as capturas
equivalentes do original estão em `system-panels-original/`. Todos os quatro
painéis abriram e fecharam sem encerrar ou travar a sessão. Os testes acima
foram recompilados e passaram novamente (4/4 com a fixture). As imagens de
auditoria contêm arte original e permanecem fora do Git.
