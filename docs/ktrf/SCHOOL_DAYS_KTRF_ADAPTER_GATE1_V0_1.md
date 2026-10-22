# School Days KTRF Adapter v0.1 — Gate 1

Status: implementation gate.

This layer is the profile-specific bridge between the generic KTRF runtime and
School Days HQ. The generic reader/runtime/router remains free of School Days
scene names, callback symbols and ORS path rules.

## Contract

Accepted profile:

```text
profile.id      = overflow.school-days-hq
profile.version = 1.0.0
entry           = sdhq:entry:new-game
```

Expected frozen inventory:

```text
NODE = 1857
RSRC = 1855
CHOI = 287
HOOK = 3
ENDG = 22
```

The 1,857 Nodes are divided into:

```text
1855 ktrf:scene
   2 ktrf:dispatcher
```

Every physical scene Node owns exactly one required Resource:

```text
scheme     = overflow.sdhq:scene-key
media_type = application/x-overflow-ors
value      = <scene key>
```

The two dispatcher Nodes own no physical Resource. They exist only to preserve
routing semantics.

## API boundary

`SchoolDaysKtrfAdapter` wraps `KtrfRouter` and provides:

- profile validation;
- `NODE -> scene-key` zero-copy resolution;
- New Game entry reset;
- School Days integer choice commit;
- Ending index -> ending code forwarding;
- Hook index -> `overflow.sdhq:*` symbol forwarding;
- exact single-transition `Advance()`;
- `AdvanceToScene()` that may traverse dispatcher Nodes until a physical scene.

`Advance()` remains available so conformance/debugging can observe every KTRF
Transition exactly. `AdvanceToScene()` is the integration-facing helper intended
for gameplay scene loading.

The adapter does **not** open an ORS file in Gate 1. Physical filesystem/archive
resolution remains the next layer:

```text
scene-key
   -> School Days asset resolver
   -> ORS bytes/path
   -> Gameplay/Event/OrsParser
```

This keeps the already-certified routing core independent of platform I/O.

## Callback bridge

KTRF Endings are exposed to School Days integration as integer codes `0..21`.

KTRF ExternalHooks must use:

```text
contract = overflow.sdhq.callback/1.0.0
symbol   = overflow.sdhq:*
arguments = []
```

The adapter resolves the typed KTRF entity before forwarding it to the game
integration callback.

## Gate

Run from repository root:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_school_days_ktrf_adapter_gate1_v0_1.ps1
```

A PASS validates the production `school-days-hq.ktnroute` and requires:

```text
scene_nodes=1855
dispatcher_nodes=2
resources=1855
choices=287
endings=22
hooks=3
new_game_scene=00/00-00-A00
resource_resolution=PASS
callback_bridge=PASS
```

The next gate connects the scene-key output to the actual School Days ORS asset
layout and makes scene handoff consumable by `Gameplay`.
