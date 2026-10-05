# School Days HQ Oracle → KTRF Routing IR

Status: **Implementation draft v0.1**

This document specifies the deterministic lowering from the frozen School Days HQ Routing Model v1 executable oracle into KTRF Canonical Routing IR v0.1.

Oracle:

- tag: `school-days-routing-oracle-v1`
- commit: `614461c2b14951ba117b9d2dedb4983cfa8ae8e6`
- generated executable table: `src/SchoolDaysRouteData.generated.inc`

Exporter:

- `tools/ktrf/export_sdhq_to_ir.py`

Profile validator:

- `tools/ktrf/validate_sdhq_ir.py`

Runner:

- `tools/ktrf/run_sdhq_export.ps1`

The generated JSON is written under `build/ktrf/` by default and is not yet a frozen source artifact. It becomes a candidate conformance artifact only after generic validation, profile validation and later differential execution against the C++ oracle.

## 1. Input boundary

The exporter reads the already-frozen generated C++ routing tables.

It does **not**:

- inspect the original game executable;
- inspect the original RouteProc DLL;
- infer frontend behavior;
- infer Save/Load behavior;
- convert `callback_38` into a Save/Continue command;
- synthesize raw research branches that are absent from the executable oracle;
- add ORS-only scripts as natural routing Nodes.

This keeps KTRF work strictly downstream from the certified executable oracle.

## 2. Frozen input inventory

The exporter refuses to continue when these source counts differ:

```text
routes                 55
nodes                  1857
transitions            2458
conditions             1464
effects                6183
feeling deltas          302
feeling resolutions     772
choice nodes             287
endings                   22
callback_38 nonterminal   47
callback_38 terminal      23
```

### 2.1 Structural dead-branch provenance

Earlier reverse-engineering of the broader structural route model identified six causally-dead raw branches:

```text
t971
t1243
t1248
t1494
t1824
t1825
```

Those IDs are **not present** in the frozen 2,458-transition executable oracle.

This means there are two distinct historical layers:

```text
broader structural research model
        ↓ feasibility/oracle generation
frozen executable Normal New Game oracle
        ↓
2458 transitions
```

KTRF v0.1 serializes the second layer because that is the executable conformance oracle.

The exporter therefore:

1. verifies those six IDs are absent from the executable source table;
2. records them as `excluded_structural_dead_transition_ids` provenance metadata;
3. never manufactures KTRF Transitions for absent branches.

Reintroducing them would create a model larger than the frozen oracle and would be a conformance error.

## 3. Node lowering

Every frozen route-table entry becomes exactly one KTRF Node.

ID:

```text
sdhq:node:<SceneKey>
```

Examples:

```text
sdhq:node:00/00-00-A00
sdhq:node:03/03-KB-E00
sdhq:node:05/05-SB-I02
```

Profile metadata preserves:

- source node index;
- SceneKey;
- Route number;
- Scene number;
- episode;
- source transition slice;
- source choice mask.

The two logical routing-only dispatchers are exported as `ktrf:dispatcher` with no ResourceLocator:

```text
03/03-B2-A00
03/03-KB-E00
```

Every other oracle Node is exported as `ktrf:scene`.

## 4. Resource lowering

KTRF does not guess a filesystem path from a SceneKey.

Instead the School Days profile defines the locator scheme:

```text
overflow.sdhq:scene-key
```

The locator value is the exact SceneKey.

Example:

```json
{
  "scheme": "overflow.sdhq:scene-key",
  "value": "05/05-SB-I02"
}
```

The School Days runtime adapter owns the mapping from SceneKey to the physical ORS resource.

The two routing-only dispatchers deliberately have no locator.

The ORS-only scripts `01/01-00-OP2` and `05/05-9O-B00` are not synthesized into routing Nodes.

## 5. Variable lowering

All recovered routing state is represented as typed Variables.

Session IDs:

```text
sdhq:var:session:<source-symbol>
```

Global IDs:

```text
sdhq:var:global:<source-symbol>
```

Two router-internal values are explicit:

```text
sdhq:var:internal:choice_result
sdhq:var:internal:callback34
```

Current recovered routing values use `ktrf:int32`.

Defaults preserve the executable oracle reset baseline:

```text
choice_result  -2
callback34      0
dword_3A6F40    0
dword_3A2294    1
other recovered integer state 0
```

`ROUTE` and `SCENE` remain Variables because source effects and observable router state use them. They are not generic KTRF Node identity.

## 6. Condition lowering

Every source condition gets one final boolean Expression carrying `source_condition_index` metadata.

This allows profile validation to prove coverage of all 1,464 source conditions even when helper Expressions are required.

### Choice

`ConditionKind::Choice` becomes a comparison against `sdhq:var:internal:choice_result`.

### SessionValue

Becomes Variable/literal comparison.

### SessionComparison

Becomes Variable/Variable comparison.

### GlobalValue

Becomes global Variable/literal comparison.

### Callback34

Becomes a comparison against `sdhq:var:internal:callback34`.

### FlagOr

The executable oracle evaluates:

```text
(sourceA != 0 || sourceB != 0) ? 1 : 0
```

and compares the resulting integer.

The frozen model uses the forms that can be represented losslessly as core boolean Expressions:

```text
A != 0
B != 0
OR(A_nonzero, B_nonzero)
boolean equality representing the original Eq/Ne result
```

The exporter intentionally fails rather than guessing if future input cannot be represented exactly by this lowering.

### Compare operators

```text
Eq -> ktrf:eq
Ne -> ktrf:ne
Gt -> ktrf:gt
Le -> ktrf:le
```

## 7. Multi-condition transitions

The source router evaluates each transition's condition slice as logical AND.

- 0 conditions: omit `predicate`;
- 1 condition: reference that final condition Expression directly;
- 2+ conditions: create one ordered `ktrf:and` Expression.

## 8. Source Effect lowering

Each of the 6,183 source Effects gets exactly one KTRF Effect with `source_effect_index` metadata.

Mapping:

```text
SetSessionConst       -> ktrf:set
SetSessionFromSession -> ktrf:copy
SetGlobalConst        -> ktrf:set
RegisterEnding        -> ktrf:register-ending
Callback              -> ktrf:call-hook
```

Source Effect order is preserved exactly at the beginning of every Transition Effect list.

Callback `value` is preserved as source metadata but is not invented as a call argument because the frozen executable oracle preserves the callback symbol for routing behavior.

## 9. ExternalHook lowering

Current hook catalog:

```text
overflow.sdhq:callback_00
overflow.sdhq:callback_2C
overflow.sdhq:callback_38
```

Contract:

```text
overflow.sdhq.callback/1.0.0
```

KTRF core does not assign frontend meaning to these symbols.

## 10. Ending lowering

Every recovered ending code becomes an Ending:

```text
sdhq:ending:0
...
sdhq:ending:21
```

A `RegisterEnding` source Effect references the corresponding Ending entity.

Terminality remains independent from Ending registration. This preserves terminal transitions whose Ending was registered earlier rather than directly by that terminal edge.

## 11. Choice and feeling lowering

Every source Node whose `choiceMask != 0` becomes one KTRF Choice.

Frozen count:

```text
287 Choices
```

Every set bit in `choiceMask` becomes one Choice option. Timeout is represented explicitly with value `-1`.

The result Variable is:

```text
sdhq:var:internal:choice_result
```

Routing policy is:

```text
ktrf:deferred
```

All 772 source feeling-resolution rows are mapped to Choice option/timeout metadata.

All 302 source feeling deltas become `ktrf:add` Effects and preserve source delta index, target source variable and delta value.

Semantic sequence remains:

```text
choice commit
-> result variable write
-> feeling Effects
-> timeline continues
-> later ktrf:next
-> transition evaluation
```

## 12. Profile-synthetic lifecycle Effects

The generated source Effect table is not the complete executable lifecycle by itself.

`SchoolDaysRouter::ResolveNext()` performs additional state actions after the selected source Effects. To preserve executable semantics in a generic KTRF interpreter, the exporter materializes those actions as explicit KTRF Effects.

### 12.1 Choice-result reset

After every selected transition, including terminal transitions:

```text
choiceResult = -2
```

The exporter creates one shared Effect:

```text
sdhq:effect:profile:reset-choice-result
```

Every Transition references it immediately after its recovered source Effects.

### 12.2 ROUTE/SCENE destination mirror

For every non-terminal selected Transition, the executable oracle mirrors the destination coordinate into:

```text
ROUTE = destinationRoute
SCENE = destinationScene
```

KTRF Node movement alone would not mutate those School Days profile Variables, so two transition-specific synthetic `ktrf:set` Effects are emitted.

Execution order is:

```text
recovered source Effects
-> choice_result reset
-> ROUTE destination mirror   [non-terminal]
-> SCENE destination mirror   [non-terminal]
-> logical destination Node becomes current
```

Terminal transitions stop after the choice-result reset and have no destination mirror.

These profile-synthetic Effects are explicitly distinguishable from the 6,183 recovered source Effects.

## 13. Transition lowering

Each **executable-oracle** transition becomes exactly one KTRF Transition.

ID:

```text
sdhq:transition:<source-id>
```

The recovered transition ID is preserved in metadata.

`priority` is the transition's local order inside its source Node, beginning at zero. This preserves first-match behavior independently from physical array placement.

All current School Days transitions use:

```text
triggers = ["ktrf:next"]
```

Terminal source transitions:

- `terminal: true`;
- no destination.

Non-terminal source transitions:

- `terminal: false`;
- direct destination Node reference.

## 14. Exclusion provenance is not executable data

The six structural dead-branch IDs are carried only as top-level metadata:

```json
{
  "excluded_structural_dead_transition_ids": [
    971,
    1243,
    1248,
    1494,
    1824,
    1825
  ]
}
```

They are **not** KTRF Transitions in this oracle serialization.

This prevents two opposite mistakes:

- silently forgetting that broader structural research found them;
- silently expanding the frozen executable oracle by reintroducing them.

A future separate archival format may preserve broader raw research topology if desired, but that is not the purpose of the executable KTRF conformance document.

## 15. Determinism

For a fixed frozen source file, the exporter must produce stable:

- IDs;
- declaration order;
- transition priority;
- metadata;
- JSON indentation;
- LF line endings;
- UTF-8 text.

The output metadata records SHA-256 of the generated executable table.

A later canonical hashing specification will define which IR fields participate in portable content identity.

## 16. Validation layers

The runner performs five stages:

```text
1. Python syntax
2. frozen executable-oracle export
3. generic JSON Schema + semantic validation
4. School Days profile conformance validation
5. exporter regression tests
```

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\ktrf\run_sdhq_export.ps1
```

Success ends with:

```text
SCHOOL DAYS HQ KTRF IR EXPORT: PASS
```

## 17. What this PASS means

A successful export proves:

- the frozen executable source tables can be parsed;
- all 1,857 frozen Nodes are represented;
- all 2,458 executable Transitions are represented exactly once;
- all 1,464 source Conditions are covered;
- all 6,183 source Effects are covered;
- all feeling resolution/delta source rows are represented;
- all 287 Choice Nodes are represented;
- generic IR structural rules pass;
- generic IR semantic rules pass;
- School Days profile inventory/lowering rules pass;
- the six research-only dead-branch IDs remain explicitly documented as exclusions;
- selected high-value handoff/terminal mappings pass regression tests.

It does **not yet prove executable equivalence**.

The next conformance level is differential execution:

```text
same initial routing state
same choice/trigger input

C++ oracle  --------> result A
KTRF IR interpreter -> result B

A must equal B
```

Only zero-divergence differential verification can authorize replacing the generated C++ oracle with KTRF runtime execution.
