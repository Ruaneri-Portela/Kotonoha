# Zero no Tsukaima — resumo de suporte

[English (US)](STATUS.md) | **Português (Brasil)**

**Escopo implementado/testado:**
- Detecção automática das 3 edições de PS2 e isolamento de saves/sistema por edição (`concerto`, `nocturne`, `symphony`):
  - **Concerto (`ZeroNoTsukaima0.bin` / `SLPS_257.09`, volume `ZERO`)**: 1.921 scripts Squirrel em texto puro (`0000.KS..1920.KS`), 26 vídeos `.PSS` de `NORMAL.BIN[69]`, animações de camada (`LayerVibrateActionModule`, `LayerJumpActionModule`, `LayerWaveActionModule`, `LayerDecelMoveModule`, `LayerAccelMoveModule`), afeto (`like`), medidor tsundere (`tndl`, `tndlStart`, `tndlEnd`), itens (`item`), avaliação de rota/beijo (`checkRoute`, `checkKiss`, `isMax`), navegação de mapa (`moveInit`, `moveItem`, `move`), encontros (`dateItem`, `date`, `resetSelect`, `resetSelect2`) e ganchos de batalha (`battleInit`, `battleAdd`, `battleEnemy`, `battle`).
  - **Nocturne (`ZeroNoTsukaima1.bin` / `SLPS_258.30`, volume `ZERO2`)**: 434 cenas KAG `.NUT` compiladas, leitura completa do rodapé `SQFunctionProto` (`OVERRIDE.NUT` com 230 subfunções), desbloqueio de galeria `CG*` e `ECG*`, parâmetros `notrans`/`stopaction`/`zoom`/`origin`, `[時間経過暗転]`, mapa (`moveinit`/`moveadd`/`move`), roleta (`tndlslot`) e intervalos de capítulo (`coffee`).
  - **Symphony (`ZeroNoTsukaima2.bin` / `SLPS_258.98`, volume `ZERO3`)**: 461 cenas KAG `.NUT` compiladas, precedência de streams `.STV` sobre cabeçalhos `.PSH` em `SOUND_ID.LST` (todas as 27 faixas `BGMB01..BGMB27`), `[hideall]`, códigos de afeto de 3 letras (`riz`, `tav`, `sie`, `anl`, `syl`, `tef`, `kyu`, `ere`), transições de capítulo `[tndlcheck]`, seletor de 18 livros mágicos (`[book]`, `[bookjump]`, `[bookdone]`), `[tndlbingo]`, composição dupla de CGs `multilayers.nut` (`ev30d`, `ev73b`) e desbloqueio de galeria `EV*`/`TUN*`.

**Limitações:** A interface interativa da batalha por turnos de Concerto e a verificação completa de longplay em todas as rotas das três edições seguem em andamento.

O relatório [PORT_STATUS.md](PORT_STATUS.md) contém análises técnicas do jogo original, observações antigas e testes associados a revisões e assets locais específicos. É evidência histórica, não garantia de funcionamento em todas as plataformas.

[README do port](README.pt-BR.md) · [Todos os ports](../README.pt-BR.md)
