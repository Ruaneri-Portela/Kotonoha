# Pure Mail — support summary

**English (US)** | [Português (Brasil)](STATUS.pt-BR.md)

**Implemented/tested scope:** DVD graphics (`.CRGB`, `.CHAR`, `.ANIM`, `.CGG`, `.CGM`, `.EDG`, `.DIB`, `.ROL`), animation, audio (`.ADPS`, `.ADP4`), bytecode VM (`op 0x00..0x21`, bank `0..5`), all 53 retail `SYS` commands (including `SYS 7` route choices, `SYS 4` dialogue, `SYS 21` trace, `SYS 64` transitions, `SYS 176..195` computer/monologue overlays, `SYS 208..215` omake galleries & ending staff roll), save/load, and full story route progression are implemented.

**Limits:** External Windows Registry Easter-egg desktop icons (`SYS 50`) are omitted on clean systems without those third-party games installed.

The more detailed [PORT_STATUS.md](PORT_STATUS.md) is an investigation log containing original-game analysis, older implementation notes, and tests tied to specific revisions or local assets. Treat it as historical evidence, not a guarantee for every platform.

[Port README](README.md) · [All ports](../README.md)
