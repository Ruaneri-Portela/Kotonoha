# Status do Port de Miss Each Other (MEO)

[English (US)](STATUS.md) | **Português (Brasil)**

## Visão Geral

O port de **Miss Each Other** (`MEO`) executa *Miss Each Other* da Overflow (FILMEngine 1.0) nativamente no Kotonoha Engine utilizando o decodificador de pacotes compartilhado `ProviderCMP` (`MEO CMP`) e o runtime de apresentação por linha do tempo em `SummerRadish::SummerRuntime`.

## Recursos Suportados

- **Montagem de Arquivos CMP**: Monta `SCRIPT.CMP`, `GRAPHIC.CMP` e `SOUND.CMP` (incluindo layouts de varejo em `Personal/MISSEACHOTHER/PACKS`).
- **Áudio e Vídeo**:
  - Reprodução de vídeo MPEG-1 (`.M1V`) e Sofdec (`.SFD`) em tempo real via `Kotonoha::MediaPlayer`.
  - Decodificação de pontos de loop `.BGM` e streaming de faixas de voz `.SND` sincronizadas em blocos de 24 quadros (`MeoAudio`).
- **Execução de Scripts e Linha do Tempo**:
  - Sincronização de linha do tempo `<Frame TIME="...">` a 24 FPS (`<Text>`, `<Log>`, `<EraseText>`, `<Bgm>`, `<BgmStop>`, camadas de vídeo `<Objects>`).
  - Avaliação completa de blocos `<Code>` (`<CALC>`, `<SET>`, `<ADD>`, `<SUB>`, `<GLOBAL>`, `<LOCAL>`, `<IFBLOCK>`, `<BLOCK>`, `<OPE>`, `<OPGE>`).
  - Escolhas temporizadas (`<SelectStartTime>` / `<SelectEndTime>` / `SetSelectScript`) e transições condicionais de rota (`SetTimeoutScript`).
  - Transições de tela (`FDIN_BL`, `FDIN_WH`, `FDOT_BL`, `FDOT_WH`) e indicador de afeição (`ViewMisaoLove`).
- **Menus e Persistência**:
  - Menu de título nativo (`START`, `LOAD`, `SCENE`, `EXIT`) e menu de pausa em jogo (`GRAPHIC/SYSTEM.TEX`).
  - Persistência de salvamento e carregamento (`MEO-PRESENTATION-1`) preservando posição na linha do tempo, deslocamento da faixa de voz e variáveis locais/globais de afeição.
