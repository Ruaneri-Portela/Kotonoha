# School Days HQ KTRF Profile v1.0

Status: **Design + exporter implementation draft**

Oracle:

- tag: `school-days-routing-oracle-v1`
- commit: `614461c2b14951ba117b9d2dedb4983cfa8ae8e6`
- executable table: `src/SchoolDaysRouteData.generated.inc`

This profile explains how the frozen School Days HQ executable routing oracle maps into generic KTRF concepts. It does not redefine KTRF core semantics.

Implementation details and validation procedure are documented in `../SCHOOL_DAYS_EXPORT.md`.

## Namespace

Prefix:

`overflow.sdhq`

URI:

`urn:kotonoha:profile:overflow:school-days-hq`

Required profile features currently emitted by the exporter:

```text
overflow.sdhq:routing-profile@1.0.0
overflow.sdhq:scene-key-locator@1.0.0
```

## Oracle boundary

KTRF v0.1 targets the **frozen executable oracle**, not every intermediate artifact produced during route research.

The executable oracle contains:

```text
routes                 55
logical nodes        1857
transitions          2458
conditions           1464
source effects       6183
feeling deltas        302
feeling resolutions   772
choice nodes           287
endings                 22
```

Earlier structural research also identified six causally-dead raw branches:

```text
t971
t1243
t1248
t1494
t1824
t1825
```

Those six IDs are **absent from the frozen 2,458-transition executable table**. They were part of the broader research model before the executable Normal New Game oracle was generated/frozen.

Therefore the KTRF exporter:

1. verifies that none of those six IDs has silently reappeared in the executable source table;
2. records the six IDs in top-level provenance metadata as `excluded_structural_dead_transition_ids`;
3. does **not** synthesize KTRF Transition entities for branches that are absent from the executable oracle.

This is a critical conformance rule. Reintroducing those branches would expand the oracle rather than serialize it.

## Entry point

Normal New Game:

- source coordinate: Route 0 / Scene 0;
- SceneKey: `00/00-00-A00`;
- KTRF entry ID: `sdhq:entry:new-game`;
- trigger: `ktrf:new-game`.

## Node mapping

Each frozen `(ROUTE, SCENE)` entry becomes one Node.

Node ID:

`sdhq:node:<SceneKey>`

`SceneKey`, route and scene remain profile metadata.

## Routing-only Nodes

These are `ktrf:dispatcher` Nodes without physical ResourceLocators:

- `03/03-B2-A00`
- `03/03-KB-E00`

They prove that Node identity cannot be physical-file identity.

Every other frozen oracle Node is emitted as `ktrf:scene`.

## Variables

Recovered integer routing state becomes `ktrf:int32` Variables.

Session IDs:

```text
sdhq:var:session:<source-symbol>
```

Global IDs:

```text
sdhq:var:global:<source-symbol>
```

Router-internal values made explicit by this profile:

```text
sdhq:var:internal:choice_result
sdhq:var:internal:callback34
```

Examples:

```text
sdhq:var:session:ROUTE
sdhq:var:session:SCENE
sdhq:var:session:001
sdhq:var:session:002
sdhq:var:session:BS05SBI01
sdhq:var:global:REP05_SB_I01
```

Source symbols are retained in metadata.

Reset/default values preserve the executable oracle baseline:

```text
choice_result  -2
callback34      0
dword_3A6F40    0
dword_3A2294    1
other recovered routing integers 0
```

## Conditions

Recovered condition families lower into generic Expressions where semantics match:

- Choice comparison -> `choice_result` Variable/literal comparison;
- SessionValue -> Variable/literal comparison;
- SessionComparison -> Variable/Variable comparison;
- GlobalValue -> Variable/literal comparison;
- Callback34 -> profile-declared internal Variable comparison;
- FlagOr -> lossless core boolean composition for the exact frozen Eq/Ne forms.

Every source condition has exactly one final boolean Expression carrying `source_condition_index` metadata. Helper Expressions may also exist.

Transition priority preserves recovered first-match order.

## Effects

Every one of the frozen 6,183 source Effects is preserved one-to-one with `source_effect_index` metadata.

Mapping:

- `SetSessionConst` -> `ktrf:set`
- `SetSessionFromSession` -> `ktrf:copy`
- `SetGlobalConst` -> `ktrf:set`
- `RegisterEnding` -> `ktrf:register-ending`
- `Callback` -> `ktrf:call-hook`

Recovered source Effect order is preserved exactly at the beginning of each Transition's Effect list.

## Profile-synthetic lifecycle Effects

The executable oracle performs routing lifecycle mutations after the generated source Effect slice. Those mutations are made explicit in IR instead of being hidden inside KTRF core.

After every selected Transition:

```text
choice_result = -2
```

The shared synthetic Effect is:

```text
sdhq:effect:profile:reset-choice-result
```

For non-terminal Transitions the executable oracle then mirrors the destination route/scene coordinate into profile Variables. The exporter emits transition-specific `ktrf:set` Effects for:

```text
ROUTE = destinationRoute
SCENE = destinationScene
```

Therefore non-terminal Effect order is:

```text
source Effects
-> choice-result reset
-> destination ROUTE mirror
-> destination SCENE mirror
```

Terminal Effect order is:

```text
source Effects
-> choice-result reset
```

These synthetic Effects are not counted among the original 6,183 source Effects.

## Choices and feelings

`SetSELECT`/choice-mask Nodes become Choices.

Frozen count:

```text
287
```

Result convention:

- `-2` pending;
- `-1` timeout;
- `0` first option;
- `1` second option;
- further option indexes are represented when a higher choice-mask bit exists.

Feeling deltas become Choice outcome `ktrf:add` Effects.

All 302 source feeling deltas and all 772 source feeling-resolution rows are preserved with source-index metadata.

Routing remains deferred until `ktrf:next`.

## Endings

The oracle contains 22 reachable native Endings with source codes `0..21`.

Ending registration is explicit and distinct from terminality.

Three terminal transitions do not directly execute `RegisterEnding`; validators MUST NOT require “every terminal transition registers an Ending”.

## Hooks

Recovered callbacks become ExternalHooks:

- `overflow.sdhq:callback_00`
- `overflow.sdhq:callback_2C`
- `overflow.sdhq:callback_38`

The static IR does not rename `callback_38` to a Save/Continue action.

The School Days runtime adapter may interpret it as an episode/terminal handoff based on separately validated runtime behavior.

The frozen generated callback `value` is retained as source metadata but is not promoted to a hook argument because the executable oracle preserves only the callback symbol for routing behavior.

## Resources

Physical ORS resolution uses the profile locator scheme:

```text
overflow.sdhq:scene-key
```

Locator value is the exact SceneKey. The runtime adapter resolves that key to the installed physical resource.

KTRF therefore does not bake `assets/` or another installation-specific filesystem layout into routing identity.

ORS-only physical scripts are not automatically Nodes in the Normal New Game routing graph.

## Transition mapping

Each executable-oracle transition becomes exactly one KTRF Transition:

```text
sdhq:transition:<source-transition-id>
```

`priority` is the local source order within the source Node and starts at zero.

Every School Days transition currently uses:

```text
ktrf:next
```

Zero source conditions -> no predicate.

One source condition -> direct final source-condition Expression.

Multiple source conditions -> ordered `ktrf:and` conjunction.

The six structural dead-branch IDs listed in the oracle-boundary section are provenance-only exclusions and therefore have no KTRF Transition record.

## Conformance target

A School Days exporter/loader must preserve oracle semantics including:

- 55 source routes as profile grouping/metadata;
- 1,857 logical Nodes;
- 2,458 executable Normal New Game Transitions;
- first-match Transition order;
- 1,464 recovered conditions after semantic lowering;
- 6,183 recovered source Effects;
- 302 feeling deltas;
- 772 feeling resolutions;
- 287 Choice Nodes;
- 22 reachable Endings;
- 47 non-terminal `callback_38` handoffs;
- 23 terminal `callback_38` handoffs;
- both routing-only dispatchers;
- explicit provenance for the six structural dead branches excluded before executable-oracle freeze.

Exact total Expression and Effect collection counts after generic lowering are not required to match the raw source-array counts because helper Expressions and explicit profile-synthetic lifecycle Effects are added.

Every added record must be deterministic and distinguishable from recovered source records through metadata/ID conventions.

Semantic equivalence, not accidental source-array shape, is the conformance requirement.

## Current validation status levels

The implementation distinguishes:

```text
Schema validity
Generic semantic validity
School Days profile validity
Executable differential equivalence
```

The first three are implemented by the current KTRF tooling.

Executable differential equivalence is the next stage and remains required before KTRF may replace the frozen generated C++ router.
