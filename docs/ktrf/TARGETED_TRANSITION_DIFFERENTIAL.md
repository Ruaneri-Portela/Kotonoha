# KTRF Targeted Transition Differential — School Days HQ

Status: **Implementation draft v0.1**

This layer closes the gap left by the 22 causal ending witnesses without confusing targeted state injection with historical gameplay reachability.

The witness corpus currently covers:

```text
transitions              1081 / 2458  (43.979%)
source nodes              1025 / 1857  (55.197%)
choice nodes               165 / 287   (57.491%)
terminal transitions        19 / 23     (82.609%)
callback_38 nonterminal      29 / 47     (61.702%)
callback_38 terminal         19 / 23     (82.609%)
routing-only nodes            1 / 2
```

Therefore the initial witness gap is:

```text
transitions                1377
source nodes                832
choice nodes                122
terminal transitions          4
callback_38 nonterminal       18
callback_38 terminal           4
routing-only nodes             1
```

## 1. Purpose

The 22-ending differential proves zero divergence for real causal witness paths, but it does not execute every transition in the frozen executable oracle.

The targeted layer asks a narrower question for every transition not covered by those witnesses:

> Can we construct one routing pre-state for which this transition is the first matching `ktrf:next` branch, inject the same pre-state into both runtimes, and obtain exactly the same observable post-state?

This is a transition-semantic test, not a claim that the injected state occurred naturally in the original game.

## 2. Components

Targeted solver/differential:

```text
tools/ktrf/diff_sdhq_targeted_transitions.py
```

Runner:

```text
tools/ktrf/run_sdhq_targeted_transition_diff.ps1
```

Compiled oracle adapter:

```text
tests/SchoolDaysOracleTrace.cpp
```

Test-only state access:

```text
KOTONOHA_ROUTER_TEST_ACCESS
```

The production Kotonoha target does not receive that definition.

## 3. Oracle-integrity boundary

The frozen oracle tag remains:

```text
school-days-routing-oracle-v1
614461c2b14951ba117b9d2dedb4983cfa8ae8e6
```

The KTRF research branch adds only a test-only way to write the already-existing `SchoolDaysRouteState` before calling the same `ResolveNext()` implementation.

It does not replace or fork the routing tables, condition evaluator, effect evaluator, transition ordering or destination logic.

The target-only accessor is excluded from production builds unless `KOTONOHA_ROUTER_TEST_ACCESS` is defined for the trace executable.

## 4. Fixture synthesis

For each uncovered transition, the solver groups all transitions sharing the same source Node and preserves explicit priority order.

It extracts only Variables referenced by local predicates and constructs small finite candidate domains from:

- Variable defaults;
- integer literals used by comparisons;
- values immediately below/above comparison literals;
- `-1`, `0`, `1` for Variable/Variable relations;
- valid Choice result values for Choice Nodes.

For each candidate assignment it evaluates the same KTRF core predicate operators used by the reference interpreter and records the first matching transition.

Search stops for a source Node once every requested local transition has a fixture or the configured combination limit is reached.

If any target transition cannot be synthesized, the tool does not silently drop it. It emits `TARGET FIXTURE GENERATION INCOMPLETE`, writes the unresolved transition IDs to the JSON report and fails.

## 5. Coordinate consistency

School Days `ROUTE` and `SCENE` are both profile Variables and the logical source coordinate.

Targeted synthesis therefore does not treat them as arbitrary free predicate Variables. They are fixed to the source Node coordinate for every injected fixture.

This prevents creation of internally inconsistent test states such as:

```text
current Node = Route 15 / Scene 31
ROUTE        = 4
SCENE        = 8
```

## 6. Choice gate

The C++ oracle blocks `ResolveNext()` when the current Node has a Choice and `choice_result == -2`.

For a targeted fixture on a Choice Node, the solver therefore selects only valid option/timeout result values rather than the pending sentinel.

The targeted transition layer injects the post-choice routing state directly. It does **not** claim to validate the Choice feeling-resolution operation itself.

Full Choice-resolution differential coverage is a separate acceptance layer.

## 7. Injected C++ trace protocol

The trace adapter retains the original causal `STEP` command and adds test-only commands:

```text
RESET
INJECT <route> <scene> <choice-result> <callback34> <feeling-applied>
SESSION <symbol> <value>
GLOBAL <symbol> <value>
RESOLVE <expected-transition-id>
```

`RESOLVE` calls the same frozen `SchoolDaysRouter::ResolveNext()` behavior and emits the same JSON post-state format used by witness differential testing.

## 8. Differential comparison

For every synthesized fixture the tool compares:

- selected source transition ID;
- Advanced/Terminal result;
- post-step profile `ROUTE`/`SCENE`;
- nonterminal current Node coordinate;
- nonterminal current SceneKey;
- destination SceneKey;
- callback order versus KTRF ExternalHook order;
- accumulated Ending registrations;
- newly registered Ending ID;
- `choice_result` reset behavior;
- `callback34`;
- every recovered session Variable;
- every recovered global Variable.

The comparison reuses the indexing/conversion logic from the witness differential module rather than defining a new School Days routing model.

## 9. Run

From repository root:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_sdhq_targeted_transition_diff.ps1
```

The runner first reconfirms the full witness differential and coverage audit, then targets the remaining transition IDs.

Expected success tail:

```text
TARGETED TRANSITION DIFFERENTIAL PASS
combined_transition_coverage=2458/2458 (100.0%)
divergences=0

KTRF / SCHOOL DAYS TARGETED TRANSITION CLOSURE: PASS
```

Default machine-readable report:

```text
build/ktrf/school-days-hq.targeted-transition-diff.json
```

## 10. What 100% transition closure means

If the targeted layer reaches `2458/2458`, every executable transition in the frozen Normal New Game oracle has at least one direct C++ versus KTRF execution comparison when witness and targeted corpora are combined.

It still does not mean every possible integer valuation, every possible historical path or every Choice-resolution row has been exhaustively tested.

The next independent layers are therefore:

1. exhaustive Choice/FeelingResolution differential coverage;
2. effect-stress probes for value-sensitive operations such as copy/add;
3. broader reachable-state or projected-state differential validation where practical;
4. only then binary `.ktnroute` serialization design.

This distinction is intentional: coverage claims must name exactly what was covered.
