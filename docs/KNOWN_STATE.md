# Known state after upstream/dev sync — 2026-11-15

**English (US)** | [Português (Brasil)](KNOWN_STATE.pt-BR.md)

Scope: the local Windows L4 School Days HQ v1.02 merge of upstream/dev
`fdd0b041` into the pre-sync development branch `18620ef9`.
The integration branch began at `3faacd0f`; the closure review followed
`c0941303`. These are **reference hashes**, not release guarantees.
See the local, untracked `build/_sync-final-log.md` for test commands and
the publication gate decision.

| Area | Verification state | Risk / next authorized verification |
| --- | --- | --- |
| Seven L×H post-credits epilogues with real WMV/OGG/PNG, fade and SaveFeel | **Not verified** after this merge; synthetic tests are not proof of real media parity | Isolated licensed-media end-to-end QA; never put retail files in Git |
| Loading existing saves and the LOAD route | **Not verified** with actual player saves (deliberately protected); synthetic save/load tests only | A separate read-only test on disposable copies with express authorization |
| GPK/VFS priority, strict locales and real GPKs | **Not verified** against the 29 original retail packs or real DLC translations | Private read-only QA after explicit authorization; no fallback to English |
| Hardware GPU and visual QA of launcher, Display Options, fades and scaling | **Not performed** | Real-device QA outside this automated, headless-only round |
| Android | **Not built or tested** in this sync | Separate platform release gate |
| iOS | **Not built or tested** in this sync | Separate platform release gate |
| macOS | **Not built or tested** in this sync | Separate platform release gate |
| Linux | **Not built or tested** in this sync | Separate platform release gate |
| Web | **Not built or tested** in this sync | Separate platform release gate |
| `Kotonoha.Infrastructure` / MetalFX | Fails on a software SDL renderer **also on pure upstream** | Known upstream/headless limitation; do not weaken or skip the test |
| `SchoolDaysLauncherTest` | With SDL dummy, exceeded 15 seconds after sync; classification documented in local final log | Compare pre-sync test and diagnostic 120-second run; no permanent timeout increase |
| DIST `ALL_BUILD` | Debug-only test/utility targets referenced APIs intentionally absent in the public engine | Keep those targets in non-DIST/DEV builds while preserving all DEV tests; rebuild DIST cleanly |
| Windows L4 `settings.json` | Source path intentionally resolves beside the executable; separate `Config.DAT` | Headless copy smoke, if possible without GUI, GPU or commercial assets |
| Optional Windows icon | The approved ICO is **private and untracked** | A clean worktree must build without it; see the School Days README |

This ledger is intentionally conservative: a passing Release compilation and
synthetic CTest suite do not certify commercial-game behavior, real saves, or
untested platforms. Preserve historical `PORT_STATUS.md` and investigation
notes. No commercial GPK, EXE, translation, real save or optional ICO should
be added to a public Git commit.
