# School Days HQ Oracle → KTRF Routing IR

Status: **Implementation draft v0.1**

This document specifies the deterministic lowering from the frozen School Days HQ Routing Model v1 oracle into KTRF Canonical Routing IR v0.1.

Oracle:

- tag: `school-days-routing-oracle-v1`
- commit: `614461c2b14951ba117b9d2dedb4983cfa8ae8e6`
- generated table: `src/SchoolDaysRouteData.generated.inc`

Exporter:

- `tools/ktrf/export_sdhq_to_ir.py`

Profile validator:

- `tools/ktrf/validate_sdhq_ir.py`

Runner:

- `tools/ktrf/run_sdhq_export.ps1`

The generated JSON is written under `build/ktrf/` by default and is not yet a frozen source artifact. It becomes a candidate conformance artifact only after generic validation, profile validation and later differential execution against the C++ oracle.

## 1. Input boundary

The exporter reads the already-frozen generated C++ data tables.

It does **not**:

- inspect the original game executable;
- inspect the original RouteProc DLL;
- infer frontend behavior;
- infer Save/Load behavior;
- convert `callback_38` into a Save/Continue command;
- prune known dead transitions;
- add ORS-only scripts as natural routing Nodes.

This keeps KTRF work downstream from the certified oracle.

## 2. Frozen input inventory

The exporter refuses to continue when these source counts differ:

```text
routes                55
nodes                 1857
transitions           2458
conditions            1464
effects               6183
feeling deltas         302
feeling resolutions    772
choice nodes            287
endings                  22
callback_38 nonterminal  47
callback_38 terminal     23
```

The six known dead transitions remain present:

```text
t971
t1243
t1248
t1494
t1824
t1825
```

## 3. Node lowering

Every recovered route-table entry becomes exactly one KTRF Node.

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

This avoids baking one installation layout into KTRF.

The two routing-only dispatchers deliberately have no locator.

The ORS-only scripts `01/01-00-OP2` and `05/05-9O-B00` are not synthesized into routing Nodes.

## 5. Variable lowering

All recovered routing state is represented as typed Variables.

Session symbols use IDs such as:

```text
sdhq:var:session:ROUTE
sdhq:var:session:SCENE
sdhq:var:session:001
sdhq:var:session:BS05SBI01
```

Global symbols use:

```text
sdhq:var:global:<source-symbol>
```

Two router-internal values are made explicit:

```text
sdhq:var:internal:choice_result
sdhq:var:internal:callback34
```

Current recovered state is integer-valued, so the profile uses `ktrf:int32`.

Defaults preserve the executable oracle reset baseline:

```text
choice_result  -2
callback34      0
dword_3A6F40    0
dword_3A2294    1
other recovered integer state 0
```

`ROUTE` and `SCENE` remain Variables because source effects and observable router state use them. They are not used as generic KTRF Node identity.

## 6. Condition lowering

Every source condition gets one final boolean Expression carrying `source_condition_index` metadata.

This permits profile conformance checks to prove coverage of all 1,464 source conditions even when helper Expressions are required.

### Choice

```text
ConditionKind::Choice
```

becomes a comparison against `sdhq:var:internal:choice_result`.

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

and then compares the resulting integer.

The frozen model only uses `Eq`/`Ne` against `0` for this family. The exporter lowers it losslessly into core boolean Expressions:

```text
A != 0
B != 0
OR(A_nonzero, B_nonzero)
boolean equality representing the original Eq/Ne result
```

The exporter intentionally fails rather than guessing if a future input uses a FlagOr comparison that cannot be represented by this proven lowering.

### Compare operators

Recovered operators lower as:

```text
Eq -> ktrf:eq
Ne -> ktrf:ne
Gt -> ktrf:gt
Le -> ktrf:le
```

## 7. Multi-condition transitions

The source router evaluates every condition in a transition with logical AND.

When a source transition has:

- 0 conditions: KTRF omits `predicate`;
- 1 condition: KTRF references that condition Expression directly;
- 2+ conditions: KTRF creates one `ktrf:and` Expression preserving source condition order.

## 8. Effect lowering

Each of the 6,183 source Effects gets exactly one KTRF Effect with `source_effect_index` metadata.

Mapping:

```text
SetSessionConst       -> ktrf:set
SetSessionFromSession -> ktrf:copy
SetGlobalConst        -> ktrf:set
RegisterEnding        -> ktrf:register-ending
Callback              -> ktrf:call-hook
```

Source effect order is preserved exactly at the beginning of every Transition effect list.

Callback `value` is preserved as metadata but is not invented as a call argument because the frozen executable oracle preserves the callback symbol and does not use the generated `value` as an argument.

## 9. ExternalHook lowering

Recovered callbacks become opaque hooks.

The current frozen catalog is:

```text
overflow.sdhq:callback_00
overflow.sdhq:callback_2C
overflow.sdhq:callback_38
```

Contract:

```text
overflow.sdhq.callback/1.0.0
```

KTRF does not assign frontend meaning to these symbols.

## 10. Ending lowering

Every recovered ending code becomes an Ending:

```text
sdhq:ending:0
...
sdhq:ending:21
```

A `RegisterEnding` source Effect references the corresponding Ending entity.

Terminality remains independent from Ending registration.

This preserves the three terminal transitions whose Ending was registered earlier rather than directly by the terminal edge.

## 11. Choice and feeling lowering

Every source Node whose `choiceMask != 0` becomes one KTRF Choice.

Frozen count:

```text
287 Choices
```

Every set bit in `choiceMask` becomes one Choice option.

Timeout is represented explicitly with value `-1`.

The result Variable is:

```text
sdhq:var:internal:choice_result
```

Routing policy is:

```text
ktrf:deferred
```

All 772 source feeling-resolution rows are mapped to Choice option/timeout metadata.

All 302 source feeling deltas become `ktrf:add` Effects and preserve:

- source delta index;
- target source variable;
- delta value.

This keeps the semantic sequence:

```text
choice commit
-> result variable write
-> feeling Effects
-> timeline continues
-> later ktrf:next
-> transition evaluation
```

## 12. Profile-synthetic Effects

The generated source Effect table is not the complete executable lifecycle by itself.

`SchoolDaysRouter::ResolveNext()` performs additional state actions after the selected source effects. To preserve oracle execution semantics in a generic KTRF interpreter, the exporter materializes those actions as explicit KTRF Effects.

### 12.1 Choice-result reset

After every selected source transition, including terminal transitions, the executable oracle resets:

```text
choiceResult = -2
```

The exporter creates one shared Effect:

```text
sdhq:effect:profile:reset-choice-result
```

Every Transition references it immediately after its recovered source Effects.

This is not counted as one of the original 6,183 source Effects.

### 12.2 ROUTE/SCENE destination mirror

For every non-terminal selected transition, the executable oracle then mirrors the destination coordinate into:

```text
ROUTE
after source effects
SCENE
after source effects
```

KTRF Node movement alone would not mutate these profile Variables, so the exporter creates two transition-specific synthetic `ktrf:set` Effects:

```text
...:route
...:scene
```

They execute after the choice-result reset.

Order is therefore:

```text
recovered source Effects
-> choice_result reset
-> ROUTE destination mirror   [non-terminal only]
-> SCENE destination mirror   [non-terminal only]
-> logical destination Node becomes current
```

For terminal transitions the last Effect is the choice-result reset and no destination mirror is emitted.

This explicit lowering is preferable to hiding School Days-specific lifecycle behavior inside the generic KTRF runtime.

## 13. Transition lowering

Each source transition becomes exactly one KTRF Transition.

ID:

```text
sdhq:transition:<source-id>
```

The recovered transition ID is preserved in metadata.

`priority` is the transition's local order inside its source Node, beginning at zero.

This preserves first-match routing independently from source-array placement.

All transitions use:

```text
triggers = ["ktrf:next"]
```

Terminal source transitions:

- set `terminal: true`;
- have no destination.

Non-terminal source transitions:

- set `terminal: false`;
- reference the destination Node directly.

## 14. Dead branches

The exporter marks the six certified dead transitions in metadata but does not remove them.

This is required because KTRF represents the recovered routing program, not a reachability-pruned walkthrough graph.

## 15. Determinism

The exporter is deterministic for a fixed generated source file.

Stable inputs produce stable:

- IDs;
- array order;
- metadata;
- JSON indentation;
- LF line endings;
- UTF-8 encoding.

The output metadata records SHA-256 of the generated oracle table.

A later canonical hashing specification will define which IR fields participate in portable content identity.

## 16. Validation layers

The runner performs five stages:

```text
1. Python syntax
2. frozen oracle export
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

- every frozen source table can be parsed;
- every source Node/Transition/Condition/Effect is represented;
- every feeling resolution/delta is represented;
- generic IR structural rules pass;
- generic IR semantic rules pass;
- School Days profile inventory rules pass;
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
