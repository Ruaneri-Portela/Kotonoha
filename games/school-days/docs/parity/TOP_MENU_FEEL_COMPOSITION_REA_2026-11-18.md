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
   at `0x10024100` checks host vtable `+0x13C` (EXE `0x0042BF70`, principal
   host `+0x228`) at `0x10024126` before hit-testing/hover. A nonzero result
   rejects input early; `0x10023FB0` separately gates individual controls.
   The meter FSM `0x10026B40` itself only updates feelings (1500 ms
   interpolation + 2000 ms hold), dispatches directional sound codes 3/4,
   and eventually clears its own latch. Neither the meter code nor the
   button-permission function directly waits for an OGG to finish.
5. **The choice's `+0x228` is NOT the menu-host `+0x228`.** The SETSELECT
   routine `EXE:0x00431740` accesses a *choice context object* and compares
   its own `+0x228` timing value. That field must not be conflated with the
   principal host `+0x228` checked through MenuBar vtable `+0x13C`.
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

Evidence: read-only REA/Ghidra 12.1.4 / llvm-objdump of authenticated retail
bytes; meter `ev_cc8fc4f434bb2f6bc554c112cd1bc63e96a708df8e59d980964ae27dd62d4618`,
button availability `ev_b917ce0fcbb65e86fafc46c4d5b9ca96eb024d0faa61f1f23319034539186db8`,
SETSELECT `ev_69d822c6d7786d33540347b8ef85b1f56bc5278b00c05e36473dd98a96d2f7a5`.
Event dispatch `ev_46c9bb11d3638edc2f11f6dce5cb5d89132c4d8cd42063c91604ad80dcacd8b4`;
skip/route state machine
`ev_c23ce9fffe4d29735164398dc131fd119edde6e13df7e8927041033b23e4dab2`.

## Kotonoha differences corrected

- `Runtime::menuBarProjection()` had `fullBarSuppressed=true` on an unresolved
  prompt but `Runtime::updateMenuBar()` always called `SetMeterOnly(false)`.
  This suppressed **the meter as well as** the surrounding controls. The new
  `meterOnly` projection for an active, unresolved choice keeps the meter
  drawable while hiding all Top Menu chrome and refusing mouse actions.
- `GameScene::renderMenuBar()` also returned early for every unresolved
  choice, preventing even a corrected meter-only presenter from appearing.
  Only the *input* handler retains that early return; the render path proceeds
  to the meter-only projection while choices are open.
- `SchoolDaysMenuBarPresenter::Render()` used a shared CHIP texture alpha,
  setting it to the hover `barAlpha_` before drawing both record 67 and the
  dynamic meter fill. This incorrectly faded or visually buried the meter
  with the Top Menu. The frame and dynamic fill now each get a separate full
  alpha when the feeling latch, choice-only display, or outgoing frozen
  choice state is active. The ordinary surrounding controls keep their native
  300/1000 ms hover fade. The meter still uses regular alpha when no
  independent meter state is active.
- The existing **choice-aware SkipToEnd/NextChoice barrier** and **frozen
  post-choice controls** remain in effect, including validation against
  prompts dispatched during a transport fade. The fix makes no edits to
  engine `lib/`, retail media, or native save formats.

## Limits / unresolved fidelity

Static disassembly proves separate meter drawing, exact geometry and the
feeling-latch alpha exception. It does **not** yet prove an exact frame at
which the principal host's global input gate clears after every SETSELECT.
The current Kotonoha post-choice release rule (next physical scene AND meter
finished AND sound duration elapsed) remains a conservative implementation
of the user's observed menu freeze, **not** an authenticated native equation.
Similarly, a visual original-run witness is still needed to distinguish
which idle chrome sprites are shown on hover while the host input gate is
nonzero. The OGG length is not itself evidence of an input unlock timer.

Game-specific synthetic tests cover prompt meter projection, suppression of
transport actions and click dispatch, alpha independence and repeated choice
tails. `SchoolDaysMenuBarRuntimeTest` additionally uses two tiny generated,
fully opaque **synthetic** PNG fixtures and reads actual SDL software-rendered
pixels: while the choice owns the view, the meter's central sample is visible
and the surrounding menu is absent; on a hovered frozen choice tail, chrome
appears at its authored location but the same meter sample remains fully
opaque and above it. Real-GPU and manual visual parity require a separate
QA playthrough.
