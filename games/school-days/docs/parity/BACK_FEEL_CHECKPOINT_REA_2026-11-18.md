# School Days HQ v1.02 — normal Back, RouteMap checkpoints and feeling replay

**2026-11-18; Windows L4, game-specific.** This record separates observations
from the authenticated retail binaries, the Kotonoha reproduction, and what
still requires a visual retail comparison. Retail EXE/installer and real saves
were **not executed or modified**. The original EXE and DLL were read for
static reverse engineering only, via REA 6.7.0/Ghidra 12.1.4 and independent
disassembly of the same bytes.

## Retail observations (static evidence)

- `RouteProcSDHQ.dll:0x10005EB0` applies per-scene feeling effects. On reverse
  navigation, `0x10005CE0` subtracts the scene's feeling delta before
  predecessor resolution. This follows `SPEC-BACK-ROUTING.md` in the preserved
  REA research and is already encoded in the generated game-specific Back
  table (`SchoolDaysBackRules::InverseFeelingDelta`). No blanket feeling reset
  or `0..100` clamp occurs on the examined reverse path.
- `SCHOOLDAYS HQ.exe:0x004331A0` retrieves the selected `SLog` type-3/SP
  checkpoint from the manager, applies its `FlgH` via `0x00432670`, and prunes
  the selected checkpoint and later ones. Read-only xrefs of the EXE `.text`
  found its call from `0x00428437` (`0x00428400`), itself called from
  `0x00423CC6`. The caller at `0x00423CB0` tests the selector against decimal
  `100`, branching away for smaller values. Thus this restore/prune operation
  is reached by **RouteMap checkpoint selection**, not the ordinary Back route.
- `EXE:0x00428A20` (apply saved `FlgH`) had direct callsites at
  `0x004326B0` (checkpoint restoration) and `0x00433B7D` (save load), not an
  ordinary Back handler. Indirect paths were not exhaustively proven absent.
- In `RouteProcSDHQ.dll`, `_GetNextScriptFile@12` at `0x10006870` reaches
  Route0 handler `0x1000D440`. This calls `0x1000D320` for `SP` processing
  before its scene/choice switch. `A03` corresponds to `SP101`. This
  establishes call order inside the handler; it does **not** establish the
  earlier timing of the host's feeling application.

REA/Ghidra read-only evidence IDs: EXE
`ev_a3c252b39ebfce4aca83e2de602286b598d955f1733752fb163c05fcfa012364`;
RouteProc
`ev_3d134d756d8c55a47fe09cd3cbc281b37dff9e530fa66f54a457512b983a07dd`.
The previously consolidated R2 implementation notes mistakenly treated the
checkpoint restoration as part of ordinary Back. The additional callsite
audit here narrows that interpretation to the RouteMap selector path.

## Port regression: RED before fix, GREEN after fix

Fixture: tracked route, synthetic/extracted game data; `KtrfSchoolDaysSessionTest`.
Original route0 first prompt is `A03`:

| Step | Feeling `001` | Feeling `002` |
| --- | ---: | ---: |
| Before first choice at A03 | 0 | 0 |
| Select first option and advance to A04 | 0 | 5 |
| Apply the normal Back inverse for A04 alone | 0 | 0 |
| **Before correction**, after full normal Back A04 → A03 | **0** | **5 (wrong)** |
| **After correction**, after normal Back A04 → A03 | 0 | 0 |

Cause: `SchoolDaysKtrfSession::CommitBack` correctly applied the source's
inverse (`A04: 002 -= 5`) but then called
`SchoolDaysPersistence::RestoreBackCheckpoint(A03)`. `SP101` had recorded
`002=5` at first choice exit; the extra restore overwrote the inverse with
`+5`, and pruned the type-3 checkpoint so inspecting the log *after* Back hid
the cause. The correction removes that `RestoreBackCheckpoint` call from the
normal `CommitBack` path only. The separate `CommitRouteMapCheckpoint` path
retains its original restore/prune contract, and choice-history type-4 entries
remain available for Replay Data.

Regression coverage repeats A03 → A04 → Back twice, includes a save/load of
the reopened uncommitted A03 choice with historical type-4 record, and then
chooses the *other* A03 option (A05: `001 += 5`) and returns through Back.
Each normal Back must restore both raw feelings, and normal Back must **not**
consume the `SP101` checkpoint reserved for RouteMap selection. This test
does not rely on the rendered bar's interpolation.

## Separate post-choice MenuBar question

The existing retail static audit
`CHOICE_RESULT_METER_SFX_RETAIL_2026-11-05.md` identifies the conditional
directional UI SE calls (`SeUp`/`SeDown`) and the menu meter's 1500 ms
interpolation plus 2000 ms hold. A second REA/Ghidra read-only pass found
separate code paths for meter updating and button permission:

- `SysMenuSDHQ.dll:0x10024C60` checks host vtable `+0x154` (EXE getter
  `0x0042C3A0`, *interface* field `+0x770` = **outerHost+0x79C**) and, if
  active, only calls the meter state
  machine `0x10026B40` and geometry `0x10026540`. The meter FSM keeps its own
  state in `this+0x44`, emits directional SE host code 3/4 through vtable
  `+0x50`, and clears the meter signal through host `+0x30` at completion.
- MenuBar **input** instead enters `SysMenuSDHQ.dll:0x10024100`. At
  `0x10024126` it checks host vtable `+0x13C` (EXE `0x0042BF70`, a getter
  of the low byte at *interface* `+0x228` = **outerHost+0x254**); a nonzero
  value rejects the input before
  hit-testing. The button-permission method `0x10023FB0` applies per-control
  host-state checks, e.g. `+0x104`, `+0x110`, `+0x88` and `+0x98`. Its observed
  predicates do not inspect `this+0x44`, wait for audio codes 3/4, or check
  elapsed OGG playback time.
- The EXE also uses the *distinct* `outerHost+0x228` in the state machine at
  `0x00426620` (states 0–11); the SETSELECT context has its own `+0x228`
  deadline. Neither is the menu input gate. Later static analysis proved a
  separate render-suppression field at `outerHost+0x258`, getter
  `0x0042BF90`, and proved that a resolved SetSELECT clears it at
  `0x00431A02` before `_SetFeeling@8`, independently of `outerHost+0x254`.
  Both gates can be reset by script-context routines `0x00431420` and
  `0x00431E00`. The exact frame when clicks are re-enabled remains
  **UNKNOWN**, not an authenticated conjunctive predicate.

REA/Ghidra evidence IDs: meter
`ev_cc8fc4f434bb2f6bc554c112cd1bc63e96a708df8e59d980964ae27dd62d4618`;
button permissions
`ev_b917ce0fcbb65e86fafc46c4d5b9ca96eb024d0faa61f1f23319034539186db8`.
Consequently, Kotonoha's existing rule that waits for a new scene, the meter
latch and the known 3925/4023 ms sound lengths remains a *conservative*
implementation of the player's direct observation, not yet proven
frame-for-frame equivalent to retail. No speculative change was made to that
menu gate during the Back/Feel correction.

## Safety and scope

- Only game-specific source, regression tests and documentation are changed.
- No new EXE/installer execution, no GPK rewrite, no real-save modification,
  no `lib/` edits, and no remote push.
- Automated tests establish state and routing behavior, **not** real-GPU or
  frame-accurate GUI parity.
