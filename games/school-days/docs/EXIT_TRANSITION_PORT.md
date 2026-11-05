# School Days HQ exit and title transitions

The original School Days HQ v1.02 shows `MENU::ExitView` after a vertical
`System/SYSTEM/SYSBASE.PNG` reveal. Its exit popup uses
`System/Exit/Popup_Exit.png`; returning to the title from gameplay uses
`System/Exit/Popup_Title.png`. The animation takes about 500 ms before either
popup opens. Cancelling fades the system layer over 500 ms. Confirmation fades
to black before the action: 750 ms for quitting and 500 ms for returning to
the title. The title is then revealed over 500 ms.

These timings and the shared transition path come from tracing the original
executable's functions at `0x0041DFA0`, `0x00427EA0`, `0x0042F6A0`,
`0x0042F5B0` and `0x0041EF60`, plus runtime captures of EXIT and TOPMENU.
The direct call path from each internal `getNextMode` branch to each click has
not been established; the two observed UI paths show the same effect. The
asset association is supported by visual comparison with the original
`SYSBASE.PNG` extracted locally from `System.gpk`.

`SchoolDaysExitTransition` implements the timing and input lock in the game
module. `SchoolDays::Runtime` draws the original texture from the user's game
installation, opens the corresponding original popup, and completes the
requested action only after the fade. No original game asset is included.
The shared engine has no changes in this port.

## Verification

- `SchoolDaysExitTransitionTest`: checks opening, cancellation, confirmation,
  opacity and reveal timing with deterministic timestamps.
- `SchoolDays.ExitRuntime`: with a local School Days HQ installation, opens and
  cancels EXIT during gameplay, confirms TOPMENU via the retail CMAP hit area,
  then opens and cancels EXIT on the title and confirms EXIT again. It asserts
  that popup, state, and quit event changes occur after their animation windows.
- On Windows, the upstream checkout used for this test required a locally
  supplied copy of its default UI font in the ignored build directory and a
  temporary Iconv build workaround. Neither is part of this change.

The automated runtime test checks state and timing. It does not establish
pixel exact visual parity for all display modes.
