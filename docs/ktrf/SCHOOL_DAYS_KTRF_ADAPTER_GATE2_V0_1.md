# School Days KTRF Adapter v0.1 — Gate 2

Status: **implementation gate**

Gate 1 proved that the School Days profile adapter correctly validates the
production `.ktnroute`, exposes physical scene resources, skips routing-only
dispatchers, and forwards ending/hook callbacks.

Gate 2 connects those semantic scene resources to the physical School Days HQ
`.ENG.ORS` asset layout already used by Kotonoha.

## Contract

The frozen profile exposes physical scene resources as:

```text
scheme     = overflow.sdhq:scene-key
media_type = application/x-overflow-ors
value      = GG/GG-...
```

The existing legacy `SceneKey::FromScriptPath()` accepts:

```text
<root>/GG/GG-....ENG.ORS
```

Gate 2 defines the exact inverse mapping:

```text
GG/GG-... -> <assets-root>/GG/GG-....ENG.ORS
```

The resolver is intentionally profile-specific. The generic KTRF reader/runtime
does not gain filesystem or School Days naming rules.

## New native API

```text
SchoolDaysKtrfAssetResolver.h/.c
```

provides:

```text
BuildOrsPath()
ResolveNodeOrs()
CurrentOrs()
AdvanceToOrs()
ProbeOrs()
```

`AdvanceToOrs()` calls the already-certified `AdvanceToScene()` adapter path,
therefore routing-only dispatcher Nodes are traversed before a physical ORS path
is returned.

## Safety / validation

Scene keys are validated before path construction:

- two-digit group prefix;
- `/` separator;
- filename group must match the directory group;
- filename body is limited to ASCII alphanumeric and `-`;
- no `..`, nested separators, or absolute-path injection;
- bounded output buffer.

Dispatcher Nodes are rejected because they intentionally own no physical
resource.

## Gate corpus

The production School Days HQ profile contains:

```text
Nodes               1857
Physical scene Nodes 1855
Dispatchers             2
```

The gate resolves and opens every one of the 1,855 physical ORS files under the
selected assets root. It also requires both dispatcher Nodes to reject physical
asset resolution.

The New Game entry must resolve to:

```text
00/00-00-A00
<assets-root>/00/00-00-A00.ENG.ORS
```

The gate then performs one real native routing step and requires the returned
physical destination ORS to open successfully.

## Run

From repository root:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_school_days_ktrf_adapter_gate2_v0_1.ps1
```

By default the assets root is:

```text
<repo>\assets
```

An alternate extracted School Days asset root can be supplied:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_school_days_ktrf_adapter_gate2_v0_1.ps1 `
  -AssetsRoot "D:\path\to\assets"
```

A PASS requires:

```text
scene_assets=1855/1855
dispatcher_rejections=2/2
scene_key_inverse_mapping=PASS
physical_ors_open=PASS
```

and ends with:

```text
SCHOOL DAYS KTRF ADAPTER v0.1 / GATE 2: PASS
```

## What this does not do yet

Gate 2 proves KTRF scene/resource -> physical ORS resolution and file access.
It does not yet replace the current `sceneIndex` / legacy `SchoolDaysRouter`
handoff inside `Kotonoha::Main`.

That replacement is Gate 3:

```text
KtrfRouter
  -> SchoolDaysKtrfAdapter
  -> ORS Asset Resolver
  -> Gameplay/Event
  -> next physical scene
```
