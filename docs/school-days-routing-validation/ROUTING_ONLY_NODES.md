# Routing-Only Nodes (No Physical ORS)

## Discovery

Real Android execution reached `03/03-KB-E00` through transition `t917` and the engine aborted with:

```text
destination not loaded: 03/03-KB-E00
```

This was initially indistinguishable from a missing or failed-to-parse ORS at the engine integration layer.

Cross-checking the recovered route-table mapping shows that `03/03-KB-E00` is intentionally a RouteProc node with no physical ORS file.

## Known no-ORS RouteProc nodes

| Route | Scene | SceneKey | Physical ORS | Role |
|---:|---:|---|---|---|
| 12 | 0 | `03/03-B2-A00` | no | routing-only dispatcher |
| 15 | 31 | `03/03-KB-E00` | no | routing-only dispatcher |

These two entries explain the historical count difference between route-model nodes and physical episode-4 ORS files.

## Route 15 / Scene 31 semantics

`03/03-KB-E00` has no timeline or choice of its own. RouteProc immediately evaluates accumulated state:

- if session `001 <= 002`: `t907 -> 03/03-KB-G00` (Route 15 / Scene 48);
- if session `001 > 002`: `t908 -> 03/03-KB-F00` (Route 15 / Scene 42), also writing `BS03KBF00=31`.

Several real script scenes can route into this dispatcher, including `03/03-KB-E08` through `t917`.

## Route 12 / Scene 0 semantics

`03/03-B2-A00` is also routing-only:

- if session `001 <= 002`: `t629 -> 03/03-B2-A02`;
- if session `001 > 002`: `t630 -> 03/03-B2-A01`.

## Engine rule

The engine must not treat every missing `sceneIndex` destination as a routing-only node. That would hide real missing assets or parser failures.

Instead, only SceneKeys explicitly identified by the recovered router as routing-only may be immediately resolved again without entering `Gameplay`.

Any other missing destination remains a hard integration failure.

## Android incident

The device trace that exposed the issue included:

```text
BS03KBE00=39
ROUTE=15
SCENE=31
next t917 -> 03/03-KB-E00
destination not loaded: 03/03-KB-E00
```

The process closing was therefore not a narrative-routing contradiction. It was an engine integration bug: Kotonoha attempted to load a physical Gameplay for a node that is only a RouteProc dispatcher.

## Fix

`SchoolDaysRouter::IsCurrentSceneRoutingOnly()` identifies the two recovered no-ORS nodes.

`Kotonoha::Main()` now immediately calls `ResolveNext()` again when an advanced destination is absent from `sceneIndex` **and** the current router node is explicitly routing-only.

This preserves strict failure behavior for all ordinary missing destinations.
