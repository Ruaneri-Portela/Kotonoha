# School Days HQ v1.02: Top Menu, SetSELECT, and feeling-meter layering

**Scope:** original Windows v1.02 retail **static** reverse engineering
(`SCHOOLDAYS HQ.exe`, `SysMenuSDHQ.dll`) compared with Kotonoha v3 Windows L4.
The retail executable/installer were never launched; original GPKs and user
saves were not modified. This report distinguishes binary observations,
player-observed UX, and gaps in proving complete retail equivalence.

## Authenticated retail UI behavior

1. **The meter is not part of the disappearing chrome.** The retail
   `SysMenuSDHQ.dll:0x10024CA0` render path draws the base/hover MenuBar, then
   separately draws the meter frame (`this+0x80`) and its three components
   (`this+0x98`, `+0x9C`, `+0xA0`). The meter draw path runs both when the host
   feeling-update latch (vtable `+0x154`, EXE `0x0042C3A0`) is zero and when
   it is nonzero. It is not conditional on the hover-chrome visibility
   (`this+0xBC`).
2. **Native meter geometry is reserved.** The exact 24-byte CHIP record table
   at `SysMenuSDHQ.dll:0x1004CE20` contains record 67 (meter frame) at
   `x=116, y=3, w=538, h=22`, atlas `x=1, y=346`; meter fills include records
   27/28. Control 0 ends at x=112; replay control regions begin at x=676;
   ordinary control row is y=26..53. The authored rectangles do not compete
   with the central meter area at x=116..654, y=3..25.
3. **The meter is immune to chrome alpha while animating.** The ordinary
   `0x100255C0` fade controller writes alpha through `0x10025690`, including
   when hovering in/out (300 ms in, 1000 ms out). At `0x1002585F..0x10025877`,
   however, it checks the host feeling-update latch. When nonzero it skips
   updating the alpha on the four meter sprites, while still changing the
   chrome alpha. Meter FSM capture at `0x10026B66..0x10026BC2` explicitly
   initializes these meter sprites to full ARGB `0xFFFFFFFF`.
4. **Input permission is a separate mechanism.** The MenuBar input handler
   at `0x10024100` checks host vtable `+0x13C` (EXE `0x0042BF70`,
   `SysMenu` interface `+0x228` = *outer host `+0x254`*) at `0x10024126`
   before hit-testing/hover. A nonzero result
   rejects input early; `0x10023FB0` separately gates individual controls.
   The meter FSM `0x10026B40` itself only updates feelings (1500 ms
   interpolation + 2000 ms hold), dispatches directional sound codes 3/4,
   and eventually clears its own latch. Neither the meter code nor the
   button-permission function directly waits for an OGG to finish.
5. **Three unrelated `+0x228` offsets must not be conflated.** The SETSELECT
   routine `EXE:0x00431740` accesses a *choice context object* and compares
   its own `+0x228` deadline. The `SysMenu` interface is a subobject rooted at
   `outerHost+0x2C`, so its `+0x228` is physically `outerHost+0x254`
   (the **actual** MenuBar global input gate). The `outerHost+0x228` field
   tracked by EXE `0x00426620` is a *third* and different 0..11 transition FSM.
6. **Skip has a script-aware stop, not a free scene advance.** The native EXE
   event queue `0x00426DB0` dispatches `0x800A` (SkipToEnd/right, via
   vtable `+0xFC(2)` → `0x0042A4A0`) and `0x800E` (SuperSkip, via
   `+0x12C(1)`/`+0xFC(5)`), subject to host state and the shared
   `host+0x254` input gate; SuperSkip also checks its enabled flag and
   additional host predicates. The `0x00425BF0` skip/scene FSM is bounded
   by the current script's `SkipFRAME` marker (`0x004315C0`/
   `0x004315A0`) before calling native `_GetNextScriptFile@12` at its
   state-7 route transition. Independent static corpus audit found
   `SkipFRAME == SetSELECT < Next` in **287/287** prompt-bearing scenes.
   This substantiates why normal skip cannot legally consume an unresolved
   authored prompt; it does not prove that every V3 protective fade-race
   guard is an exact native control-flow equivalent.
7. **The input and render gates are separate host fields, with different
   release points.** The SysMenu interface vtable at `EXE:0x004D2894` is
   installed at `outerHost+0x2C`. Slot `+0x13C` (`EXE:0x0042BF70`)
   reads interface `+0x228` = **outerHost+0x254** (input), while slot
   `+0x140` (`EXE:0x0042BF90`) reads interface `+0x22C` =
   **outerHost+0x258** (render suppression). `SysMenu:0x10024F45`
   checks the latter and skips both chrome and Feel draw paths when nonzero.
   The `[SetSELECT]` dispatch path around `EXE:0x0043ADA7..0x0043ADCB`
   leads to `EXE:0x0043B12E` (`0x432060(1)` for input) followed by
   `0x0043B141` (`0x4320A0(1)` for rendering). A resolved result
   (`selected > -2`, including omission `-1`) calls `EXE:0x0043F330`
   from `0x00431A02`, **before** `_SetFeeling@8` at `0x00431A32`:
   `0x43F330` stores the result and calls only `0x4320A0(0)` at
   `0x0043F35E`, clearing render suppression without clearing input.
   Separate script-context paths `0x00431420` and `0x00431E00` clear
   both fields through paired setters; determining their exact frame in
   each post-choice handoff remains open. Therefore the **render gate is
   definitely not held until SeUp/SeDown ends**, but this alone does not
   establish when every menu control becomes actionable.

Evidence: read-only REA/Ghidra 12.1.4 / llvm-objdump of authenticated retail
bytes; meter `ev_cc8fc4f434bb2f6bc554c112cd1bc63e96a708df8e59d980964ae27dd62d4618`,
button availability `ev_b917ce0fcbb65e86fafc46c4d5b9ca96eb024d0faa61f1f23319034539186db8`,
SETSELECT `ev_69d822c6d7786d33540347b8ef85b1f56bc5278b00c05e36473dd98a96d2f7a5`.
Event dispatch `ev_46c9bb11d3638edc2f11f6dce5cb5d89132c4d8cd42063c91604ad80dcacd8b4`;
skip/route state machine
`ev_c23ce9fffe4d29735164398dc131fd119edde6e13df7e8927041033b23e4dab2`.

## Input-release state branches and per-control permissions (additional static audit)

The EXE scheduler `0x00425BF0` dispatches by **outerHost+0x224** (not the
MenuBar gate `+0x254`, nor the transition FSM `+0x228`) through table
`0x00426600`, states `0..7`. Its parent dispatcher at
`0x0042740D..0x00427437` takes *different* paths according to
`outerHost+0x220`: state `3` uses normal gameplay `0x425550` ->
`0x4252E0` -> `0x425331` -> SetSELECT `0x431740`, whereas state `4`
calls the transition FSM `0x425BF0`. These paths are mutually exclusive
for an individual dispatch. Observed transition-state targets are:

| State | Target | Possible input-gate release |
| --- | --- | --- |
| 0 / 1 / 2 | `0x425C4E` / `0x425CA1` / `0x425CF3` | Not directly through the inspected reset calls |
| 3 | `0x425D3F` | Subphase `+0x2D4==1` calls `0x425AD0` at `0x425D78`; other conditions can call it at `0x425F19` |
| 4 / 5 | `0x425FF4` / `0x426052` | Timed/presentation steps, not an identified Feel/OGG release predicate |
| 6 | `0x4260AF` | Can call `0x425AD0` at `0x426218` |
| 7 | `0x4262D6` | Calls RouteProc `_GetNextScriptFile@12` at `0x42630D`, then `0x431420` at `0x4263E6` after preparing the script key, subject to active script-context flag |

`0x425AD0` calls `0x431E00` at `0x425B49`. If its script controller is
active (`controller+4 != 0`), `0x431E00` clears the MenuBar input and render
gates before the script rebind. Likewise `0x431420` clears both gates if its
controller is active. These are **conditional state/rebind edges**, not a
single fixed delay counted from the choice click. The branch for a specific
A03-to-A04 handoff and its exact scheduler frame remain unproven. In
particular, the original 48-tick result tail is not itself a comparison
for clearing `+0x254` in the inspected setter/rebind paths.

Within the SysMenu button-availability function `0x10023FB0`, a separate
25-entry class selector/jump table around `0x100240CC..0x100240E0`
implements the following predicates
*after* the global input gate has allowed event handling:

| Control indices | Predicate class |
| --- | --- |
| `0..3`, `13..14` | Always allowed by this *particular* availability function |
| `4` (NextChoice/SuperSkip) | `!host+0x104 && !host+0x110 && SuperSkipFlag` |
| `5..9` (speed selections) | `!host+0x110 && host+0x88` |
| `10..12` (Save/Load/BackLog) | `!host+0x104` |
| `15..24` | `host+0x98` |

The listed `host+...` names are **SysMenu interface virtual-method slots**,
not byte offsets in the outer object. Their EXE implementations include
`+0x104 -> 0x42BEB0` (reads physical `outerHost+0x150`),
`+0x98 -> 0x42BEF0` (reads physical `outerHost+0x1E0`), while
`+0x88 -> 0x427490` and `+0x110 -> 0x428210` query script/skip conditions,
including the scene-key lookup. The input handler tests `+0x13C` at
`0x10024126` *before* hover/hit-test, and the click dispatch
(`0x1002459E` and its per-control branches) checks availability again.
Consequently, clearing `outerHost+0x254` does **not** guarantee every
individual control is actionable; the scene and flags still matter. Neither
`0x10023FB0` nor the inspected input-release setters directly tests the
Feel latch or audio completion. A global claim excluding all indirect
conditions is **not** established.

## Kotonoha differences corrected

**Correction to this report on 18 November, after further user-observed
retail behavior:** an *unanswered* `SetSELECT` displays **choices only, no
Feel meter**. The earlier implementation/documentation treating the pending
choice itself as a meter-only state was wrong; see the post-choice state
machine below. The visual alpha separation remains authentic, but does not
imply the meter must always be visible.

- `Runtime::menuBarProjection()` had `fullBarSuppressed=true` on an unresolved
  prompt and was previously changed to `meterOnly=true` while awaiting a
  reply. That incorrectly displayed Feel during the actual choices. The
  corrected projection hides both chrome and Feel while awaiting `SetSELECT`;
  input remains protected.
- `GameScene::renderMenuBar()` also returned early for every unresolved
  choice. Its render path is now allowed to execute, but the presenter hides
  both parts during the prompt, displaying only the *conditional* later
  post-choice effect; only the input handler retains the early rejection.
- `SchoolDaysMenuBarPresenter::Render()` used a shared CHIP texture alpha,
  setting it to the hover `barAlpha_` before drawing both record 67 and the
  dynamic meter fill. This incorrectly faded or visually buried the meter
  with the Top Menu. The frame and dynamic fill now each get a separate full
  alpha when the feeling latch or a real post-choice changed feeling
  presentation is active. The ordinary surrounding controls keep their native
  300/1000 ms hover fade. The meter still uses regular alpha when no
  independent meter state is active.
- The existing **choice-aware SkipToEnd/NextChoice barrier** and **frozen
  post-choice controls** remain in effect, including validation against
  prompts dispatched during a transport fade. The fix makes no edits to
  engine `lib/`, retail media, or native save formats.

## Corrected SetSELECT → Feel chronology

The following sequence combines authenticated retail callsites and the
player's direct visual observation (the latter is **not** a retail frame
capture):

1. **Prompt opens:** `EXE:0x00431740`, `0x0043181B..0x00431828` sends host
   sound **5** (`SeView`); it also overrides effective playback to 1x. The
   options are visible, but **neither Top Menu chrome nor the Feel meter**
   should be displayed as a standalone UI.
2. **Choice is confirmed or omitted:** a valid result first clears the native
   visual-suppression field `outerHost+0x258` at `0x00431A02` via
   `0x0043F330`, independently of input gate `outerHost+0x254`. This is a
   permission to render, not a command to display Feel or to enable buttons.
   Selection index >=0 sends host sound
   **1** (`SeSelect`) at `0x00431AA0..0x00431AAD`; omission/index -1 sends
   sound **0** (`SeCancel`) at `0x00431AC1..0x00431ACE`; -2 remains pending.
   `_SetFeeling@8` is called via `EXE:0x00431A32` for the committed path,
   including index -1. That call does *not* prove nonzero points: it routes to
   game-specific decisions and may leave both raw feelings unchanged.
3. **Feeling value changes:** `SysMenu:0x10026B40` Capture reads raw `001`
   and `002`. Detect at `0x10026C5C..0x10026D4C` emits host sound **3**
   (`SeUp`) or **4** (`SeDown`) **only when one of the raw values actually
   differs from the previous sample**, preferring `001`. If identical, it
   enters PostTarget without the directional sound. The standalone meter
   now appears only when this *real Detect event* occurs for a manually
   selected (index >=0) choice; the prompt and unchanged/omitted outcomes
   never enable it by the selection or feeling-update latch alone.
   Upstream of Detect, RouteProc `_SetFeeling@8` at
   `RouteProcSDHQ.dll:0x1000BBA0` dispatches route-specific effect tables.
   `0x10005EB0` parses the resulting per-scene deltas and invokes writer
   `0x10005C60`; the writer **returns on zero delta** at `0x10005C64`, but
   for nonzero deltas writes the variable and only raises the callback
   `interface+0x30(1)` if the variable name is exactly UTF-16 `001` or `002`
   (`0x100551EC`, `0x100551F4`). This independently substantiates that a
   mere invocation of `_SetFeeling` is not enough to cause Feel. The real
   latch lives at **outerHost+0x79C** (SysMenu interface `+0x770`), not
   `outerHost+0x770`. For omission `-1`, the route-specific branch may still
   invoke the effect function; the decision to hide the *standalone* meter on
   omission follows the user's original-game observation, not a proven
   assertion that all omission routes have delta zero.
4. **Effect and release:** changed Feel stays at full independent alpha;
   sound and meter follow their individual authored durations. The visual
   suppression gate has **already cleared before** the feeling change, yet
   input permission has its own field and per-control predicates. The
   precise original host input-unlock timestamp is still unproven, so the
   existing conservative Kotonoha action freeze remains unchanged and is
   not described as exact retail timing.

Implementation: `choiceFeelEligible` distinguishes an actual selected option
from omission; `choiceFeelVisible` is raised only by meter Detect code3/4.
`VisibilityForFrozenChoiceTail(..., showMeter)` separates frozen controls
from the optional Feel sprite, and `meterOnly` is now a **post-choice** state,
never a pending-SetSELECT state. On the 18 November follow-up, the player
reported an additional regression: *stale pre-choice hover alpha* caused
chrome to reappear automatically after a manual or omitted result. The
presenter now drops hover selection, pointer latch and fade alpha when the
choice takes ownership; while frozen, chrome stays hidden. A real pointer
motion into the top strip **during the frozen result** is remembered only as
a position, never as a hover fade or an actionable control. On release,
chrome may fade in if that pointer remains over the strip; if it has moved
away, nothing automatically reappears. AUTO ON is an independent
full-opacity CHIP sprite that remains visible in the pending prompt,
unchanged/omitted result, changed Feel presentation and scene handoff.
The ordinary feeling latch alone no longer exposes the meter after a
no-delta refresh; only raw delta detection does so outside the choice-specific
presentation. A result-origin refresh remains marked as choice-owned through
the frozen exit: even if the RouteProc rule for an **omitted** choice produces
an actual `001`/`002` delta, that change cannot leak into the generic
standalone Feel path after the choice freeze releases. A later *fresh*
nonchoice gameplay refresh is independent. No additional fabricated
SeUp/SeDown is played solely because the player omitted a choice.

## Limits / unresolved fidelity

Static disassembly proves separate meter drawing, exact geometry, the
feeling-latch alpha exception, the **distinct `+0x254` input and `+0x258`
render gates**, and the immediate render-gate clear on a resolved choice.
It does **not** yet prove the exact frame at which the input gate clears
after every SETSELECT or when all per-control predicates become permissive.
The current Kotonoha post-choice release rule (next physical scene AND meter
finished AND sound duration elapsed) remains a conservative implementation
of the user's observed menu freeze, **not** an authenticated native equation.
Similarly, a visual original-run witness is still needed to distinguish
which idle chrome sprites are shown on hover while the input gate is
nonzero. The OGG length is not itself evidence of an input unlock timer.

Game-specific synthetic tests cover prompt meter projection, suppression of
transport actions and click dispatch, alpha independence and repeated choice
tails. `SchoolDaysMenuBarRuntimeTest` additionally reads actual SDL software
pixels using fully opaque **synthetic** PNG fixtures. During an unanswered
choice and after manual/omitted confirmation, chrome and unearned Feel are
absent while the blue AUTO ON chip remains. Changed Feel has its own full
alpha without exposing chrome, including after the next scene binds.
After release, no stale **pre-choice** hover is inherited. A pointer moved
into the upper strip during the frozen result may reveal chrome on unlock
without a second movement; an actual move away still keeps chrome hidden.
A delayed latch with unchanged raw feelings cannot resurrect Feel,
and a synthetic omitted result with an actual raw delta remains visually
suppressed after releasing the frozen choice tail.
These tests do **not** by themselves prove retail GPU visual parity.

### Direct retail execution attempt, isolated (18 November 2026)

The user authorized a **new runtime test of the original EXE**, superseding
the earlier static-only restriction for this research. The protected QA copy
at `D:\Dev\Preserved\SchoolDaysHQ\QA-Retail-20261118-worker5` was launched
under a Windows **Low-integrity** token, with separate empty `Save`, copied
EXE/DLL/Config and a `Packs` junction used only for read access under MIC.
The actual screenshot `Evidence/screen-initial-20261118-115716.png` records
`ExeModule: GraphicBase initialisation error / Failed to load DX3D device..`.
The original process never reached the title or SetSELECT, so no screen-based
observation of the prompt, omitted choice, Feel or AUTO may be inferred from
this attempt. The QA original `Save` stayed empty; the retail files and
personal saves were not modified. See the QA `Evidence/QA-runtime-evidence.txt`.

**Second isolated retail attempt (physical QA copy, medium-integrity Windows):**
copied all 30 retail GPKs into a **normal directory, not a junction** at
`D:\Dev\Preserved\SchoolDaysHQ\QA-Retail-Physical-20261118-worker5`,
with a separate empty Save and redirected profile directories. The copied
`Script.GPK` matched the retail SHA-256 exactly. Using FFmpeg's DXGI Desktop
Duplication `ddagrab` (ordinary GDI fullscreen screenshots were black),
actual original v1.02 evidence was captured in that QA's `Evidence/` folder:

- `ddagrab-medium-20261118-1221.png`: native title with START/LOAD/OPTION;
- `ddagrab-after-start-8s-20261118-122315.png`: genuine narrative scene
  with no upper MenuBar while the pointer was away from the top;
- `ddagrab-hover-top-20261118-1223.png`: moving the pointer into the upper
  strip revealed the original menu chrome, controls and Feel artwork;
- `ddagrab-speed32-away-20261118-1224.png`: after selecting x32 and moving
  the pointer away, the chrome disappeared but the orange x32 status
  indicator remained independently visible.
- `ddagrab-run2-hover-before-auto-20261118-1227.png`: second retail run with
  upper hover and the native `AUTO MODE OFF` control;
- `ddagrab-run2-AUTO-ON-away-20261118-1227.png`: after clicking AUTO and
  moving the pointer to the center, only the **blue `AUTO MODE / ON`** chip
  remains at top left, without chrome or Feel on the narrative video;
- `ddagrab-run2-AUTO-ON-x4-away-20261118-1228.png`: after engaging x4 and
  moving the pointer away, **both** the independent blue AUTO ON and orange
  x4 selected-speed chips persist, with ordinary chrome/Feel hidden.

These captures **directly confirm hover-dependent chrome and independent
AUTO/speed status sprites in ordinary gameplay**, not yet the exact
post-choice behavior or AUTO ON during a choice. The first medium retail clone
exited with Windows `0xC0000005` (EventID1000, faulting module `ntdll.dll`)
shortly after x32 was activated; timing alone does not establish causation.
A separate medium clone run captured AUTO ON with conservative x4 playback.
Do not reinterpret normal-playback screenshots as proof of pending SetSELECT
or omitted/selected choice results without scene-specific captures.

### User-supplied manual SetSELECT recording (18 November 2026)

The user subsequently supplied a **17.13-second, 1280x720, 60-fps H.264
recording** named `QA-Retail-Physical-20261118-worker5 2026.11.18 -
12.57.21.02.mp4`. They explicitly confirmed that **they selected the option
manually, not AUTO**. These are direct sampled-frame observations
(approximate recording-relative timestamps):

| Time | Retail visual evidence |
| --- | --- |
| `0.0..2.5s` | Normal gameplay chrome shown on upper hover; AUTO shows OFF |
| `4.5..6.3s` | A03 choices `So what?` / `No` visible with **no normal chrome and no standalone Feel**, even when the cursor is near the upper strip (`~x590,y30`) |
| `~6.4..6.8s` | Player manually confirms the upper choice; the **standalone Feel meter starts appearing** while selected text is still fading |
| `~7.5..10.9s` | Text disappears; Feel remains isolated while pointer is already over the meter/top strip (`~x635,y24`); normal chrome remains hidden |
| `~11.0s` | Full MenuBar chrome returns while cursor **is** in the top strip. No new motion at the exact unlock instant is apparent; this does **not** demonstrate a spontaneous return with pointer away |
| `11.5..17s` | Normal narrative continues with chrome under top hover |

The top-right `A gravação foi iniciada` pop-up belongs to the **recorder**,
not the game's MenuBar. This clip does **not** show omission, AUTO ON during
SetSELECT, a manual no-delta selection, or whether an earlier menu action was
clickable while frozen. The visible timing corroborates the independent
Feel phase but does **not** prove that OGG playback completion governs the
input gate. The port's hover-probe regression now distinguishes a live
pointer already at the top **during** the frozen result from inherited
pre-choice chrome alpha; only the former may reveal chrome upon release.
