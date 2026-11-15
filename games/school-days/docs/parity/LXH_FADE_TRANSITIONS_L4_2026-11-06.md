# L4 Windows — L×H: transições pretas (etapa 2)

**Escopo:** somente `D:/Dev/Kotonoha-v3/build/school-days-dev-control4`.
O fade adicional é exigência do remaster; **não** é apresentado como efeito
autenticado da versão comercial HQ. Nenhum ORS/WMV/OGG/PNG ou GPK foi editado.

## Política implementada

- Ativação exclusiva quando L×H está ON e o roteiro é um dos sete finais
  autenticados com `END_ROLL` original + `PLAY_MOVIE` adicional L×H + `Next`.
  Com DLC OFF, Replay/PlayData ou demais cenas: nenhum fade novo.
- Depois do último tick de `END_ROLL`, escurecer todo o Screen por 500 ms com
  clock de roteiro/mídia temporariamente retido. Durante o intervalo ORIGINAL
  entre créditos e `PLAY_MOVIE`, manter preto sem comprimir a timeline. Ao
  disparar o filme adicional, revelar o Screen em 500 ms. O evento de filme,
  OGG, legendas e os fades autorais do ORS não são reescritos ou deslocados.
- Depois do `Next` final L×H, escurecer por 500 ms **antes** do handoff para
  SaveFeel/countdown. Abrir SaveFeel sob preto e revelar em 500 ms. O primeiro
  `SystemUi.Update` de SaveFeel fica suspenso durante a revelação: a contagem
  nativa de 30 segundos só começa quando a tela já está totalmente visível.
- O overlay preto é físico, renderizado **após** legendas, barra e SystemUi,
  sem entrar no cache `LastPresented` ou mudar o fade ORS 24fps. Interações
  narrativas ficam bloqueadas apenas durante transições; SDL_QUIT não é
  bloqueado. `seek`, `bindScene` e retorno ao menu eliminam estado obsoleto.

## Âncoras, não offsets globais

| Final | EndRoll.end (24fps) | L×H PlayMovie.start | Next estendido |
| --- | --- | --- | --- |
| `05-A2-F00` | `04:04:11` | `04:06:11` | `06:13:11` |
| `05-A2-Z00` | `04:10:03` | `04:13:03` | `09:07:03` |
| `05-B0-F00` | `03:56:19` | `03:58:19` | `05:37:19` |
| `05-G0-L03` | `02:50:15` | `02:51:15` | `04:56:15` |
| `05-KC-F00` | `13:51:13` | `13:53:13` | `17:43:13` |
| `05-SB-E01` | `04:37:20` | `04:37:20` | `10:00:20` |
| `05-SC-L03` | `03:32:23` | `03:35:23` | `05:34:23` |

Estes valores foram auditados em ORS privados e são usados em TESTES, não
hardcoded no runtime: `Runtime::configureLxhFade` lê os eventos reais de
cada cena. O caso SB com intervalo zero exige interceptar **antes** de tocar
os eventos daquele tick. O sistema não usa `SkipFRAME` como limite de créditos.

## Verificações realizadas

1. CMake Release target `SchoolDays` / `SchoolDaysLxhFadeTest` na MESMA
   `build/school-days-dev-control4`: PASS.
2. `ctest -C Release -R '^(SchoolDaysSceneTimelineTest|SchoolDaysLxhPolicyTest|SchoolDaysLxhFadeTest)$'`
   3/3 PASS: alfas, sete gaps, SB same-tick, OFF, seek, 30s intactos.
3. Probe privado de sete roteiros ON/OFF, arte Display Options e 21 mídias:
   PASS, dados de terceiros nunca salvos no repositório.
4. `SchoolDays.exe --node 05/<FINAL> --lxh on --renderer software --frames 6`
   em perfis temporários SDL dummy: 7/7 roteiros armaram exatamente as
   âncoras acima. `--lxh off` no B0: nenhum fade armado.
5. QA de reprodução acelerada (24x) com mídia L×H real em perfil isolado
   `qa-l4-lxh-fade-live-20261106`: B0 registrou `LxH entrance fade OUT`
   ao atingir créditos, `entrance fade IN` ao iniciar mídia,
   `countdown fade OUT` no Next e `countdown fade IN` no SaveFeel. A sessão
   voltou ao `gameState=1` (menu). Não é teste de sincronia A/V audível a 1x.

## Validação ainda necessária em etapas de QA Windows

- Inspeção visual do fade real com vídeo/áudio 1x, todos os sete finais,
  inclusive a sobreposição com `BlackFade IN` já autoral em A2F/A2Z/B0/SB.
- Mudança de resolução/renderer/upscaler, legenda por personagem e timing
  sob carga de decoder. Esses itens permanecem nas próximas etapas do plano.
- Testes de Save/Load e Replay no meio do epílogo quando a DLC muda ON/OFF,
  tratados na etapa específica de compatibilidade de saves.

**Proibições cumpridas:** sem alteração de GPK comercial/saves reais,
sem commits/push e sem build Android.
