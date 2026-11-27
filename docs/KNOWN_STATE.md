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

| Subtitle styles / colors | **Not visually verified**; `styles.ass` now contains design-only speaker colors and a Setsuna correction, with synthetic tests only | Color selection is tested headlessly, not validated against real rendered scenes |

### Independent atomic GlobalFlag POSIX verification — 2026-11-17

The game-specific `SchoolDaysAtomicFile` helper was extracted into a C++17
implementation with no dependency on SDL, vcpkg or the engine. Its standalone
test compiled and passed on local Windows, and on **GitHub Actions** hosted
POSIX runners for the fork (not on a local Linux installation):

| Runner | Image and version from CI log | Result |
| --- | --- | --- |
| `ubuntu-latest` | `ubuntu-26.04`, `20261111.196.1` | Native C++17 and ASan/UBSan **PASS** |
| `macos-latest` | `macos-26-arm64`, `20261102.0473.1` | Native C++17 **PASS** |

Runs: <https://github.com/tsubaki-0x/Kotonoha/actions/runs/44013431802>
and <https://github.com/tsubaki-0x/Kotonoha/actions/runs/44014281654>
(2026-11-17 UTC). Tests cover 1,000 replacements, injected write/flush/rename/
directory-sync failures, old orphan cleanup, and 220 randomized SIGKILL crash
attempts on each POSIX runner. The macOS helper uses `F_FULLFSYNC` where
available, falling back to `fsync` only on `ENOTSUP`, and handles `EINTR` and
partial writes. This confirms the **standalone storage helper**, not an entire
School Days macOS/Linux build, real physical power-loss durability, or
commercial gameplay on those operating systems. The workflow lives only on
a temporary fork CI branch and must not be merged into release branches.

This ledger is intentionally conservative: a passing Release compilation and
synthetic CTest suite do not certify commercial-game behavior, real saves, or
untested platforms. Preserve historical `PORT_STATUS.md` and investigation
notes. No commercial GPK, EXE, translation, real save or optional ICO should
be added to a public Git commit.

### Known DIST MCP command-line test mismatch — 2026-11-17

`Kotonoha.MCPCommandLine` is registered in the shared-engine
`lib/tests/CMakeLists.txt:20-22`, outside the permitted game-specific edit
scope. The unchanged positive test at `lib/tests/MCPCommandLineTests.cpp:39-60`
expects `--mcp` / `--mcp-port` to be available, but the DIST parser
intentionally excludes those options in `lib/src/core/CommandLine.cpp:24-42`.
It therefore **fails in DIST** as a known incompatible positive assertion,
not as evidence that the public binary enables MCP. The test remains
registered, runnable and unmodified; it has not been skipped or weakened.
The separate `SchoolDays.DistDebugAbsence` test passes in DIST and explicitly
rejects MCP, MCP port/root and debug logging options. Final Windows Release
CTest for this feature: DEV **80/81** (only known software MetalFX failure),
DIST **74/75** (only this registered MCP positive test).

### Pending choice is a transport barrier — 2026-11-17

School Days `SkipToEnd` can no longer seek across an unresolved `SetSELECT`.
Its game-specific runtime identifies an unresolved authored prompt even before
its input window appears, computes one protected destination, captures it when
the upper menu initiates a black fade, and revalidates the same destination
before navigating. A late prompt, stale scene or advanced clock cancels the
transport without a seek, fade continuation or choice commit. In particular,
the first permitted jump remains `SkipFRAME - 24` ticks; ordinary no-choice
scenes and pre-choice `SkipToNextChoice` keep their existing behavior.

The safeguard also applies to gameplay RIGHT, the LEFT/Back input while a
prompt is actively waiting, direct menu control 3, mouse/touch and synthetic
gamepad skip input; replay-owned transports retain their recorded-choice
semantics. The upper MenuBar **controls, background chrome and Feel meter**
are suppressed while an uncommitted choice owns input, but an enabled
**AUTO ON status sprite remains independently visible**. Legitimate player clicks, timed-out prompts, AUTO mode,
pause and selected playback speed remain separate from this transport barrier.

**Post-choice menu correction (user's direct observation, November 17, extended
November 18):** after the player confirms or omits a choice, the upper bar
must not automatically reappear due to inherited hover alpha; its controls
remain frozen during the choice exit. If the pointer actually moves into the
upper strip **during** the frozen result and is still there at release, the
ordinary MenuBar can return without another mouse movement. A pointer outside
the top never reveals chrome just because the scene advances. AUTO ON remains
visible throughout. This refinement is grounded in the user's supplied A03
manual-choice retail video (November 18, ~7.5–11.0 s).
The isolated feeling meter appears without hover **only if a real raw feeling
change was detected after a selected choice**. An unanswered `SetSELECT`
displays options only (not the meter); omission/unchanged feelings do not
invent a standalone meter. The menu becomes interactive
again only after the next physical scene is associated **and** the feeling
meter has finished its animation and any directional `SeUp` / `SeDown` effect.
Existing audited resource lengths are 3.924172 s / 4.022857 s respectively;
the game-specific release gate uses their ceiling in milliseconds. When the
choice does not change feelings, no second sound or artificial four-second
delay is introduced. `SeSelect` remains the immediate choice-confirmation cue;
ESC / keyboard configuration keeps its existing behavior.

Deterministic synthetic C++ tests on Windows exercise visible choices,
black-fade races, repeated inputs (2/3/10/50), RIGHT/LEFT, direct control 3,
`SkipToNextChoice`, manual selection, choice-free scripts, timed choice expiry,
post-choice inert buttons, hover/meter projection, `SeUp` and `SeDown` unlock
boundaries and soundless confirmation,
and fixed-seed randomized sequences (**256 sequences, 12 actions each,
seed `0x5EEDBEEF`**). The old test suite for Replay Data and H-scene playback
is preserved unchanged. The barrier failures were recorded before any
production fix in the ignored `build/_skip-choice-log.md`; no retail script,
real save or commercial GPK was used.

**Not verified:** GUI/manual playthrough with real School Days media, real GPU
behavior, timing fidelity against the original commercial binary where the
existing documentation does not establish it (the top-menu description above
was observed by the user, not independently captured in retail; arrow-key
behavior at an open prompt remains unverified), mobile gamepad/touch
hardware, Linux/macOS/Android/iOS builds. Synthetic tests and a successful
Windows build cannot substitute for the user's manual gameplay QA. The
untracked `build/CHECKLIST-QA-SKIP-CHOICE.md` records the manual procedure.

Windows Release verification for this local choice-barrier feature, with
synthetic fixtures only: full DEV build **PASS**, DEV CTest **80/81** (only
the historical `Kotonoha.Infrastructure` software MetalFX failure), full
DIST build **PASS**, and DIST CTest **74/75** (only the existing positive
`Kotonoha.MCPCommandLine` mismatch detailed above). Both variants passed the
`SchoolDaysNextChoiceRuntimeTest` randomized barrier cases. The separately
run headless DEV stress passed **36/36** isolated `--documents`/`--config`
smoke cases; `ValidateBuildMetadata.py` passed. The DIST MCP mismatch does
**not** satisfy a strict acceptance gate permitting only the MetalFX failure.
No `lib/` test was changed or suppressed to conceal it. DIST-specific stress
is not verified because `stress_l4.py` currently invokes DEV-only `--node` /
`--ktrf` controls.

**Normal Back feeling regression (REA 2026-11-18):** original static EXE
callsite inspection distinguishes RouteMap `SP>=100` checkpoint restore/prune
(`0x004331A0`) from ordinary `RouteProcSDHQ` reverse navigation. The old port
incorrectly restored the type-3 checkpoint *after* applying the inverse
feeling on ordinary Back, causing A03 → A04 → Back to retain `002=5` instead
of returning to `002=0`. The correction leaves RouteMap checkpoint restoration
intact and applies only the ordinary reverse effect for Back. New regression
tests cover same-branch replay, alternate choice A05 and pending-choice
save/load. See
`games/school-days/docs/parity/BACK_FEEL_CHECKPOINT_REA_2026-11-18.md`.
The post-choice bar's precise original action-unlock timing still lacks
independent retail frame/video confirmation.

**Top Menu / Feel rendering (REA 2026-11-18; revised after gameplay report):** original `SysMenuSDHQ.dll`
draws the feeling frame/fills separately from the hover MenuBar, with the
meter in the central `x=116..654, y=3..25` region. `0x10025690` explicitly
does not apply the chrome fade alpha to meter sprites while a feeling update
is active; `0x10026B40` initializes those sprites at full opacity. In the
port, the initial correction erroneously presented the Feel meter **while the
choice was still pending**, and forced the meter visible after confirmations
that changed no feelings. The corrected runtime shows **only choices plus
SeView (sound code5)** at prompt entry, then plays `SeSelect` for a confirmed
option or `SeCancel` for omission; only a genuine changed-feeling event
(`SeUp`/`SeDown`, codes3/4) activates standalone Feel. The additional
November 18 correction clears stale pointer/fade state on entering SetSELECT,
does not show frozen choice chrome automatically, and preserves AUTO ON as a
standalone sprite during the pending prompt, chosen/omitted result and handoff.
A late unchanged feeling refresh cannot resurrect Feel. Even if an omitted
choice's route executes a nonzero raw `001`/`002` delta, its choice-origin
refresh cannot leak into the generic Feel renderer after the frozen exit.
The meter retains
independent alpha during an actual feeling effect. This does **not**
unfreeze Top Menu actions or permit SkipToEnd/SuperSkip to bypass choices.
An SDL software pixel-readback regression now checks that a pending choice
draws neither Feel nor chrome, an omitted/unchanged choice never draws an
isolated meter (even after a delayed no-delta refresh), and a genuine
changed-Feel display stays full-alpha without waking chrome. The AUTO ON
chip remains visible in all these phases. Original skip dispatch and the
`SkipFRAME == SetSELECT < Next` ordering in
287/287 authored choice scenes were verified statically. See
`games/school-days/docs/parity/TOP_MENU_FEEL_COMPOSITION_REA_2026-11-18.md`.
Precise native post-choice input unlock remains an open verification item,
not a proven consequence of the OGG cue's duration.
