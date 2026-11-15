# Known state after upstream/dev sync — 2026-11-15

**English (US)** | [Português (Brasil)](KNOWN_STATE.pt-BR.md)

Scope: the local Windows L4 School Days HQ v1.02 merge of upstream/dev
`fdd0b041` into the pre-sync development branch `18620ef9`.
The integration branch began at `3faacd0f`; the closure review followed
`c0941303`. These are **reference hashes**, not release guarantees.
See the local, untracked `build/_sync-final-log.md` for test commands and
the publication gate decision.

### Local Noto Sans JP font required by headless tests

Obtain the authentic **Noto Sans JP Regular** TrueType font from the official
[Google Fonts / Noto Sans JP project](https://fonts.google.com/noto/specimen/Noto+Sans+JP)
(SIL Open Font License 1.1). Use the actual `NotoSansJP-Regular.ttf` font, not
the tiny placeholder that a clean checkout may stage. Put it at
`assets/fonts/NotoSansJP-Regular.ttf` in the local source tree **without
committing the font**. For Windows Release CTest, stage this file into the
ignored build outputs as follows:

- `build/<build-dir>/bin/Release/assets/fonts/NotoSansJP-Regular.ttf` for
  `Kotonoha.Presentation` (`KotonohaPresentationTests.exe`) and
  `SchoolDaysPortTest.exe`, which also prepares the synthetic `school_days_assets`
  fixture used by subsequent tests.
- `build/<build-dir>/outputs/Release/SchoolDays/assets/fonts/NotoSansJP-Regular.ttf`
  for `SchoolDaysRuntimeSmoke` (`SchoolDays.exe`).

Rebuild/stage assets after installing the font; if the package step has already
run, copy the licensed font into those ignored build directories manually.
The missing/placeholder font caused **73/77** instead of the expected **75/77**
in a clean DEV run; with the correct local font, the two remaining failures
were the already documented Infrastructure/MetalFX and launcher timeout. No
font bytes belong in Git, and no retail files or real saves are needed.

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
| `SchoolDaysLauncherTest` | SDL dummy launcher passes with a command-line-only 120-second diagnostic (~26 seconds), but exceeds the unchanged 15-second CTest limit. In pre-sync DEV, its prerequisite `SchoolDaysPortTest` times out at 15 seconds before the launcher can run | Headless/fixture slowness; no proof that the sync introduced a launcher defect; do not increase committed timeout |
| DIST `ALL_BUILD` | **PASS** in a clean Windows Release worktree after `96c3c03` isolates debug-dependent test/utility targets to non-DIST. The same unresolved debug symbols occurred on pre-sync `18620ef9` | Compile and test the entire debug suite in DEV; no debug API in player executable. Real DIST gameplay still unverified |
| Windows L4 `settings.json` | **Headless smoke passed** in a copied Release directory using synthetic game data, SDL dummy, isolated `--documents` and no `--config`. The file appeared beside the copied EXE, not in isolated `%APPDATA%`; `Config.DAT` is separate | Actual player's personal profile was never accessed |
| Optional Windows icon | The approved ICO is **private and untracked**; a clean worktree without it **configured and compiled successfully** | Keep out of Git; see the School Days README |
| Shared `lib/` parity | Six behavioral deviations (subtitle API/renderer, backlog viewport culling, absolute overlay path fallback and named-section translation API) were restored byte-for-byte from `upstream/dev`, one commit per file | See `docs/LIB_DIVERGENCES.md`. Other-platform runtime/visual parity is still untested; Windows build and static use analysis are the authorized evidence |
| `Kotonoha.Directories` / `StoragePaths` upstream discrepancy | **Reproduced on pure upstream `fdd0b041`:** `Kotonoha.Directories` fails with `Desktop Documents root excludes the save directory` under SDL dummy. Shared `lib/src/Filesystem/StoragePaths.cpp` is identical to upstream. The upstream test expects `layout.documents == base` and `layout.saves == base / "save"`, while the actual upstream implementation uses `base / "Kotonoha" / locations.packageIdentifier` and that directory's `save` subfolder | The local test retains the stricter, accurate namespaced expectations and passes. The upstream test should be reported upstream by a separately authorized workflow; **no upstream push or PR** was performed |

**Exact upstream issue to relay (not sent):** in
`lib/tests/DirectoryTests.cpp`, the desktop loop calling
`directoryLayout(locations, "amagami")` asserts `layout.documents == base`
(`Desktop Documents root excludes the save directory`) and
`layout.saves == base / "save"`. In the *same* upstream commit, the desktop
branch of `directoryLayout` in `lib/src/Filesystem/StoragePaths.cpp` sets
`result.documents = locations.userData / "Kotonoha" /
locations.packageIdentifier` and then
`result.saves = result.documents / "save"`. These expectations are mutually
incompatible for a nonempty package identifier. Reproduced on pure
`upstream/dev` `fdd0b041`, without changing `StoragePaths.cpp` or weakening
the local namespaced test.

This ledger is intentionally conservative: a passing Release compilation and
synthetic CTest suite do not certify commercial-game behavior, real saves, or
untested platforms. Preserve historical `PORT_STATUS.md` and investigation
notes. No commercial GPK, EXE, translation, real save or optional ICO should
be added to a public Git commit.
