# Auditoria separada: menus e Route Map

Data: 04/11/2026. Ordem de investigação: (1) título e menus de sistema,
(2) Route Map, (3) gameplay e comparação de perfil base/100%. Este documento
registra as duas primeiras etapas; a sequência temporal de EXIT/TOPMENU está
em [MENU_TRANSITION_TRACE.md](MENU_TRANSITION_TRACE.md).

## Propriedade das funções

| Área do jogo | Entrada | Estado consultado | Código Kotonoha |
|---|---|---|---|
| Título | START, LOAD, REPLAY, OPTION, EXIT | `EndClear`, perfil visual e cursor | `SchoolDaysTitleModel`, `Runtime::openTitle` |
| LOAD/SAVE | Páginas, slots, comentário e Route Map | `SaveFile*.DAT` + metadados de `GlobalFlag.DAT` | `SchoolDaysSaveLoadModel`, `Runtime::openSaveLoad` |
| Replay | H-scenes e Replay Data | desbloqueios globais e slots válidos | `SchoolDaysReplayModel`, `Runtime::openReplay` |
| Config | Páginas e escolhas de vídeo/áudio | `Config.DAT` e configurações do jogo | `SchoolDaysConfigMenuModel`, `Runtime::openConfig` |
| EXIT/TOPMENU | Pergunta YES/NO | modo de origem e resultado do diálogo | `SchoolDaysExitViewModel`, `Runtime::openExit`, `Runtime::openMainMenuConfirm` |
| Route Map | Episódio, subpágina, nós, porcentagem e retorno | `ROUTE`, `SCENE`, checkpoints globais `SP...`, cenas lidas | `SchoolDaysRouteMapModel`, `SchoolDaysRouteMapOracle`, `SchoolDaysRouteMapRenderPlan`, `Runtime::openRouteMap` |
| Gameplay | Cena, escolhas, handoffs, MenuBar e persistência | sessão local + estado global | `SchoolDaysKtrfGameplayBridge`, `Runtime::activateMenuAction` |

Todos esses componentes ficam em `games/school-days`; esta etapa não mudou o
motor em `lib/`.

## Matriz de perfis observada em runtime

As instalações originais usadas são cópias isoladas em
`Ferramentas/SchoolDaysHQ-RE-Lab/runtime/save-mode-probe/`. A build usou os
mesmos recursos do original com um diretório `--documents` separado. Capturas
locais: `build/school-days-release-audit/menu-matrix/` (ignorado pelo Git).

| Perfil | Título original | LOAD original | Route Map original | Kotonoha após correção |
|---|---|---|---|---|
| D: vazio | normal, REPLAY inativo | sem slots | Episódio 1, 0%, quatro nós pretos | mesmo estado no título, LOAD e mapa |
| C: só `SaveFile*.DAT` | normal, REPLAY inativo | sem metadados visíveis | Episódio 1, 0%, quatro nós pretos | LOAD vazio e mapa 0% após correção; título verificado |
| B: só `GlobalFlag.DAT` | perfil documentado na pesquisa v1.02; não repetido nesta captura | não repetido | não repetido | não repetido nesta matriz |
| A: saves + `GlobalFlag.DAT` | arte concluída, REPLAY ativo | dez slots preenchidos na página 1 | Episódio 1, 100%, quatro nós azuis | mesmo estado; troca para Episódio 2 e retorno ao LOAD também passaram |

A abertura do mapa pelo LOAD no título **não mostra marcador de cena atual**.
Nó desbloqueado adiciona a faixa azul do CHIP; nó sem checkpoint fica com o
círculo preto da imagem base. A porcentagem é independente do slot selecionado:
vem das cenas lidas no registro global.

## Correção feita no Route Map

Antes, `Runtime::refreshRouteMap` consultava `SP...` da sessão e o render plan
desenhava uma camada para todo nó. Na build, isso produzia nós azuis e um
marcador rosa até no perfil vazio. A leitura direta do KTRF global também não
resolvia: seus valores padrão podem ser não zero sem `GlobalFlag.DAT`.

Agora a disponibilidade usa a presença/valor da entrada no `GlobalFlag`
persistido (`SchoolDaysPersistence::GlobalIntegerIsSet`); a imagem base cobre
nós bloqueados; a faixa 1 do CHIP cobre nós liberados; e o marcador de cena
só é considerado em gameplay. O trecho de página 2 e o botão de retorno ao
LOAD foram comparados visualmente no original e na build com perfil A.

Verificação: `SchoolDays` Release recompilou; `SchoolDaysPortTest`,
`SchoolDaysHandoffRuntimeTest` e `SchoolDaysUiModelTest` passaram (3/3); os
perfis D e A foram reexecutados na build após a última alteração.

## Próxima etapa: gameplay

Usar checkpoints de entrada equivalentes em perfis base e 100%, comparar
MenuBar, escolhas, saves, retorno ao título e replay por cena. O primeiro
ponto já identificado é a animação de EXIT/TOPMENU documentada no arquivo
vinculado acima. O mapa em gameplay e a ativação de cada nó ainda não têm
comparação completa em runtime; as capturas desta etapa cobrem o acesso pelo
LOAD do título e a navegação entre páginas.
