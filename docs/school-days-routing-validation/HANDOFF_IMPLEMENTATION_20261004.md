# School Days HQ — external handoff integration (2026-10-04)

## Scope

This research branch introduces an explicit boundary between recovered `RouteProc` routing and the outer Kotonoha execution/UI layer. It does **not** implement the final Save/Load UI or terminal checkpoint format.

## Evidence driving the design

The installed game was observed at runtime in two relevant cases:

- EP1 -> EP2: after the episode ends, the original game shows `Salva este jogo?`. A save created from that UI contains the **post-transition** state (`01/01-00-A00`, `ROUTE=1`, `SCENE=0`) and loading it resumes directly in EP2.
- Ending path: the original game shows the same `Salva este jogo?` prompt after EndRoll. Letting the countdown expire returns to the title without terminating the process. A stable post-ending SaveFile sample was not obtained, so terminal save serialization and terminal-save Load semantics remain unknown.

Static analysis also established that `callback_38` is an external callback slot. In the installed EXE it sets a deferred handoff/menu signal; it does not itself implement the UI.

## Router change

`SchoolDaysRouter::NextResult` now preserves external callback identities in order through `callbacks` and exposes `HasCallback()`.

`EffectKind::Callback` is no longer described as ignored. The router records the callback name and continues to apply the remaining recovered effects. No claim is made that the generated callback `value` field reproduces the original callback argument.

The router remains UI-agnostic.

## Kotonoha handoff state

Kotonoha now owns `SchoolDaysPendingHandoff` with two active kinds:

- `Episode`: an advanced transition carrying `callback_38` and a valid destination.
- `Terminal`: a terminal transition after its recovered ending/global effects have been applied.

For an episode handoff, the router is already in the **post-transition state** while the physical destination scene remains pending. This matches the captured EP1 checkpoint.

For a terminal handoff, Kotonoha no longer returns `SDL_APP_SUCCESS` from the route terminal. The process remains alive and the terminal state is held for the future Save/Continue -> Title flow.

## Duplicate-effect guard

While a School Days handoff is pending, `Kotonoha::Main()` does not call the current gameplay timeline or `SchoolDaysRouter::ResolveNext()` again. This prevents callbacks, global writes, and ending registrations from being applied more than once across frames.

## Development control

With `KOTONOHA_DEV_CHECKPOINTS=ON`, `F5` acknowledges a pending handoff:

- Episode handoff: loads the already-resolved destination.
- Terminal handoff: logs that the title transition is not implemented yet and deliberately keeps the terminal pending.

`F6` remains the router state dump key.

The F5 control is development-only and is not exposed in release builds.

## Explicitly not implemented yet

- visual `Salva este jogo?` UI and countdown;
- save-slot UI;
- SLog writer / `SaveFile*.DAT` compatibility;
- `GlobalFlag.DAT` writer;
- terminal SaveFile serialization;
- Load of a terminal checkpoint;
- final Title controller transition after terminal handoff;
- exact `callback_00` / `callback_2C` runtime semantics;
- KTRF / `.ktnroute` serialization.

`callback_00` and `callback_2C` are preserved in `NextResult.callbacks` but are not assigned invented semantics.

## Validation target

The next Windows validation should confirm:

1. `t25` returns destination `01/01-00-A00`, carries `callback_38`, and leaves router state at `ROUTE=1`, `SCENE=0`.
2. Kotonoha logs an episode handoff and does not start EP2 until F5 in a DEV build.
3. F5 resumes the pending EP2 destination.
4. A real terminal no longer closes the process and is held as a terminal handoff.
5. Existing routing-only dispatchers `03/03-KB-E00` and `03/03-B2-A00` continue to resolve exactly as before.

## Known research gap

A post-ending SaveFile sample was not captured. Do not infer terminal checkpoint `SceneKey`, `ROUTE`, `SCENE`, or Load behavior from the non-terminal EP1 sample.
