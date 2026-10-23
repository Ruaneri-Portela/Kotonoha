# Windows Engine Integration Evidence — 2026-10-04

## Scope

This note records native Windows validation of the recovered School Days HQ normal-New-Game router inside the real Kotonoha engine with the physical episode ORS set loaded from `assets/00` through `assets/05` and original media supplied separately through the configured assets path.

## Full natural path — PASS

A real interactive run progressed through the six episode prefixes and reached a native ending with the router enabled.

Observed milestones included:

```text
02 -> 03
Route 15 routing-only dispatcher
03 -> 04
04 -> 05
Ending 16
```

The final router state registered Ending 16 through terminal transition `t2357`.

Representative final log:

```text
ending=16
global REP05_SB_I01=1
callback ignored in normal-new-game router: callback_38
terminal transition t2357 ending=16 registrations=1
```

The current engine integration intentionally returns `SDL_APP_SUCCESS` on a router `Terminal`, so the executable closes after the terminal transition. This is an integration policy still to be replaced with the correct post-ending behavior; it is not evidence of a routing failure.

A separate isolated playback of `05-SB-I00`, `05-SB-I01`, and `05-SB-I02` confirmed that the physical final ORS sequence reaches its ending presentation / EndRoll normally. The earlier routed run was manually skipped forward, explaining why the ending presentation was not observed before `t2357` in that run.

## Routing-only dispatcher 1 — PASS

`03/03-KB-E00` was validated in the full engine with real assets:

```text
BS03KBE00=39
ROUTE=15
SCENE=31
next t917 -> 03/03-KB-E00
routing-only node 03/03-KB-E00 (no ORS); resolving immediately
ROUTE=15
SCENE=48
next t907 -> 03/03-KB-G00
```

The engine correctly avoided physical Gameplay lookup for the virtual node and continued into physical `03/03-KB-G00`.

## Routing-only dispatcher 2 — PASS

The second and final known no-ORS RouteProc node was validated with a targeted DEV checkpoint whose physical predecessor was `03/03-SB-I03` (Route 18 / Scene 60).

Observed log:

```text
ROUTE=12
SCENE=0
next t1212 -> 03/03-B2-A00
routing-only node 03/03-B2-A00 (no ORS); resolving immediately
BS03B2A02=0
ROUTE=12
SCENE=2
next t629 -> 03/03-B2-A02
```

With `001=10` and `002=20`, the dispatcher correctly selected the `001 <= 002` branch (`t629`) and continued into physical `03/03-B2-A02`.

## Windows acceptance status

```text
Full EP1 -> EP6 interactive path: PASS
Native ending reached:             PASS (Ending 16)
Routing-only nodes:                2 / 2 PASS
Physical post-dispatch continuation: PASS
Terminal registration:            PASS
Post-ending UI behavior:           PENDING
Android ARM64 revalidation:         PENDING
```

## Remaining integration question

`callback_38` / post-ending behavior is the next Windows integration target. The current normal-New-Game router records the callback as outside its implemented scope and the engine then terminates on `NextKind::Terminal`. The desired behavior must be recovered and implemented separately from the already-validated narrative routing graph.
