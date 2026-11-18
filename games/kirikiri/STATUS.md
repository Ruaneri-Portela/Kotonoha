# Kirikiri2 / KirikiriZ — support summary

**English (US)** | [Português (Brasil)](STATUS.pt-BR.md)

**Implemented/tested scope:** Standard XP3 v1/v2 archives (chained/zlib indexes, segmented files, patch overlay precedence), TLG5/TLG6/PIMG images, clean-room PSB v2 + MDF binary parser (`PsbFile`), KAGEnv `.ks.scn` scenario runner (`ScnRunner` with PT-BR `VNX+/` localization, speaker nameplates `cn_name_*.png`, multi-channel voices, E-mote keyframe actions, savepoint snapshot persistence, and `00_01.ks`..`00_15.ks` chapter progression), native `VNX+/` UI screens (Title menu `title_cn@*`, Save/Load `saveload_cn@*`, Config `config_cn@*`, Backlog `backlog_cn@*`, Extra/Chapter select `extra_cn@*`, in-game message box/toolbar `window@help__*` and `qlpopup@*`), plain KAG3 scripts, and bounded TJS2 bootstrap evaluation are implemented.

**Limits:** Retail `.xp3` archives encrypted by third-party native protection filters (`PackinOne.dll`) are indexed without DRM circumvention; unencrypted overlays (`VNX+/`) and standard unpacked Kirikiri assets render directly.

[Port README](README.md) · [All ports](../README.md)
