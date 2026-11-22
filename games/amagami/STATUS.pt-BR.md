# Amagami — resumo de suporte

[English (US)](STATUS.md) | **Português (Brasil)**

**Escopo implementado/testado:**
- Leitura completa do disco de PS2 (`SLPS_259.18`): 2.308 scripts SCF em `SCENARIO.ARC`, 222 classes de sistema em `AMAGAMI.IMG`, texturas TIM2 e malhas fragmentadas `.zb` em `GRAPH0..2.ARC`, além de `SOUND/MUSIC.AFS`, `SOUND/SOUND.AFS`, `SOUND/VOICE.AFS` e `SOUND/MUSIC.ARC`.
- Decodificador SPU2 ADPCM (blocos de 16 bytes) para o banco Sony `IECS` `systemBank.hd`/`systemBank.bd` em `SOUND/MUSIC.ARC` (todas as 49 ondas `Vagi` `0..48` a 39.000 Hz em PCM mono de 16 bits, mapeadas via `se/<0..48>` e notas MIDI `12..60`).
- Tabelas exatas de BGM do `SLPS_259.18` nos modos Classic (`0x0028e710`, `MC01..MC37` com fallback Arrange nas faixas vocais/ED `33, 34, 38..40`) e Arrange (`0x0028e7b4`, `M01..M40`), além da tabela de som ambiente (`0x0028e6d0`).
- Tabela completa de 252 descritores de texturas `Sprite` do PS2 (`0x001b5700..0x001b7a70`, incluindo `0x001b74d0` `bgsam/` e `kaiwa/` e sufixos de dias da semana em `0x00289868`), tabela de prefixos de poses/figurinos (`0x001b7a70`) e cobertura de primitivas para `Object`, `Interface`, `Sprite`, `FontChar`, `FontChar2`, `FontCharEx`, `NodeMapIcon`, `ControlPad`, `Music` e `Sound`.
- Prólogo original jogável, grafo de eventos de campanha/calendário, modo de conversa, cenas de minigame/mahjong e save/load determinístico por replay.

**Integração com a UI de Kotonoha:**
- O menu de pausa nativo (`戻る`, `セーブ`, `ロード`, `タイトルへ`, `設定`) é implementado por `AmagamiMenu`, subclasse de `Kotonoha::InGameMenu` que preserva o layout e os rótulos originais.
- Salvar e Carregar abrem o `Kotonoha::VNSaveMenu` em `amagami/`; Configurações abre o menu do motor; Título retorna pelo callback do runtime.
- O `AmagamiBacklog` continua renderizando o LogList nativo por meio do `Kotonoha::VNBacklog`; `Esc`/clique direito abre o menu de pausa e `B`/`PageUp` abre o histórico.
- `AmagamiEngineTest` verifica que `Esc` abre e fecha o menu de pausa conectado usando o disco comercial.

**Limitações:** A validação completa de longplay em todas as 2.308 permutações de cenários das 6 heroínas segue em andamento.

O relatório [PORT_STATUS.md](PORT_STATUS.md) contém análises técnicas do jogo original, observações antigas e testes associados a revisões e assets locais específicos. É evidência histórica, não garantia de funcionamento em todas as plataformas.

[README do port](README.pt-BR.md) · [Todos os ports](../README.pt-BR.md)


## Melhorias de compatibilidade Vita (open-amagami)

- O diagnóstico Vita inicia em `GameMain.scriptMain` e carrega os
  cenários SCF sob demanda a partir de `SCENARIO.CPK`. `Class.findClass`
  não antecipa a carga, mas `K2X_Script.loadClass` e referências literais
  de classes carregam os cenários necessários.
- As primitivas básicas de `K2X_Script` incluem a consulta ao relógio de
  frames e estado do ambiente. O loop gráfico passa a usar cadência de
  60 Hz com relógio monotônico de alta resolução do SDL3.


O diagnóstico Vita reconhece os controles Xbox (A para confirmar, B para cancelar)
e preserva o esquema PlayStation (Circle/Cross). Alterações de mapeamento SDL
recalculam os botões segurados. As chamadas ControlPad originadas em cenários
SCF retornam os bits originais do PS2, e as classes do sistema Vita continuam
a receber os bits SceCtrl. Testes sintéticos cobrem as duas disposições.
