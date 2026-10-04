# Routing-Only Nodes (No Physical ORS)

## Discovery

Real Android execution reached `03/03-KB-E00` through transition `t917` and the engine aborted with:

```text
destination not loaded: 03/03-KB-E00
```

This was initially indistinguishable from a missing or failed-to-parse ORS at the engine integration layer.

Cross-checking the recovered route-table mapping shows that `03/03-KB-E00` is intentionally a RouteProc node with no physical ORS file.

## Proven no-ORS RouteProc nodes

| Route | Scene | SceneKey | Physical ORS | Role |
|---:|---:|---|---|---|
| 12 | 0 | `03/03-B2-A00` | no | routing-only dispatcher |
| 15 | 31 | `03/03-KB-E00` | no | routing-only dispatcher |

The repository-wide scene inventory audit passed and proved that these are the complete router-only SceneKeys across episodes 00..05.

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

## Completeness audit — PASS

The repository-wide inventory audit passed on the working checkout.

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\schooldays-routing-verifier\run_scene_inventory_audit.ps1
```

The audit computes the exact set differences:

```text
router SceneKeys - physical ORS SceneKeys
physical ORS SceneKeys - router SceneKeys
```

Observed set difference:

```text
router - ORS:
03/03-B2-A00
03/03-KB-E00

ORS - router:
01/01-00-OP2
05/05-9O-B00
```

Counts also matched the formal baseline: 1,857 router SceneKeys and 1,857 physical ORS SceneKeys, with no duplicate SceneKeys and no malformed ORS paths. The routing-only catalog is therefore complete for the audited script inventory.
