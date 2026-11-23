# Miss Each Other (MEO) Port Status

**English (US)** | [Português (Brasil)](STATUS.pt-BR.md)

## Overview

The **Miss Each Other** (`MEO`) port runs Overflow's *Miss Each Other* (FILMEngine 1.0) natively in Kotonoha Engine using the shared `ProviderCMP` (`MEO CMP`) asset decoder and the timeline presentation runtime in `SummerRadish::SummerRuntime`.

## Supported Features

- **CMP Archive Mounting**: Mounts `SCRIPT.CMP`, `GRAPHIC.CMP`, and `SOUND.CMP` (including `Personal/MISSEACHOTHER/PACKS` retail layouts).
- **Audio & Video**:
  - Real-time MPEG-1 (`.M1V`) and Sofdec (`.SFD`) video playback via `Kotonoha::MediaPlayer`.
  - `.BGM` loop point decoding and `.SND` 24-frame synchronized voice track streaming (`MeoAudio`).
- **Script & Timeline Execution**:
  - 24 FPS `<Frame TIME="...">` timeline synchronization (`<Text>`, `<Log>`, `<EraseText>`, `<Bgm>`, `<BgmStop>`, `<Objects>` movie layers).
  - Full `<Code>` evaluation (`<CALC>`, `<SET>`, `<ADD>`, `<SUB>`, `<GLOBAL>`, `<LOCAL>`, `<IFBLOCK>`, `<BLOCK>`, `<OPE>`, `<OPGE>`).
  - Timed choices (`<SelectStartTime>` / `<SelectEndTime>` / `SetSelectScript`) and conditional route transitions (`SetTimeoutScript`).
  - Screen fades (`FDIN_BL`, `FDIN_WH`, `FDOT_BL`, `FDOT_WH`) and affection meter overlay (`ViewMisaoLove`).
- **Menus & Persistence**:
  - Native title menu (`START`, `LOAD`, `SCENE`, `EXIT`) and in-game pause menu (`GRAPHIC/SYSTEM.TEX`).
  - Save/load state persistence (`MEO-PRESENTATION-1`) preserving timeline position, voice track offset, and local/global affection variables.
