# KTRF Broader State Differential — School Days HQ

Status: **Implementation draft v0.1**

This layer is the final broad semantic-hardening pass planned before the School Days KTRF IR semantic freeze gate.

Earlier gates already established:

```text
executable Transition coverage       2458 / 2458
Choice nodes                           287 / 287
Choice outcomes / FeelingResolution    772 / 772
FeelingDelta rows                      302 / 302
source Effects                        6183 / 6183
```

All direct differentials reported zero divergence. The broader state matrix asks a different question: do the two runtimes still select and execute the same routing behavior in deterministic states surrounding predicate boundaries, including states that deliberately do not resolve to a Transition?

## 1. Scope

Tool:

```text
tools/ktrf/diff_sdhq_state_matrix.py
```

Runner:

```text
tools/ktrf/run_sdhq_state_matrix.ps1
```

Default report:

```text
build/ktrf/school-days-hq.state-matrix-diff.json
```

The tool reuses the validated KTRF `Selector`, reference `Interpreter`, post-state comparison helpers and the compiled `SchoolDaysOracleTrace`. It does not introduce another independent routing implementation.

## 2. State families

The matrix is deterministic and finite. It contains four overlapping scenario families.

### A. Default coherent state for every Node

Every one of the 1,857 logical Nodes is injected with:

```text
ROUTE = Node route
SCENE = Node scene
all other Variables = reset/default values
```

This includes routing-only dispatcher Nodes and the otherwise unreachable structural Node present in the executable catalog.

For each of the 287 Choice Nodes, the default scenario deliberately leaves:

```text
choice_result = -2
```

and requires both runtimes to remain unresolved. This directly tests the pending-Choice routing gate rather than only committed Choice outcomes.

### B. One satisfying fixture for every Transition

The existing targeted-transition solver is run against all 2,458 executable Transition IDs, not only the historical witness gap.

This guarantees that the broader matrix itself contains at least one selecting state for every executable Transition.

### C. Single-variable boundary perturbations

For every base fixture, every locally relevant predicate Variable is varied one at a time over the finite domain derived by the existing selector.

Domains include, where applicable:

- reset/default values;
- comparison constants;
- the integer immediately below and above comparison constants;
- `-1`, `0`, `1` for Variable-versus-Variable relations;
- every valid committed Choice result for a Choice Node.

The other fixture Variables remain at their known-satisfying values. A perturbation may keep the same Transition, select a different first-match Transition, or make the Node unresolved. All three outcomes are valid test cases.

### D. Limited pairwise boundary combinations

For source Nodes with at least two locally relevant Variables, one deterministic satisfying anchor is selected and pairs of Variables are varied together.

The default cap is:

```text
32 pairwise candidates per source Node
```

This is intentionally bounded so the matrix broadens interaction coverage without pretending to enumerate the full Cartesian product of the int32 state space.

## 3. Deduplication

Scenarios are deduplicated by:

```text
(source Node, complete explicit assignment set)
```

A scenario may therefore belong to multiple generation categories. The report records category membership separately from the count of unique executed scenarios.

## 4. Oracle prediction and comparison

Before execution, the KTRF selector predicts the first matching source Transition for each injected state.

The expected source Transition ID is then supplied to the existing trace protocol:

```text
RESOLVE <transition-id>
```

For an unresolved state the expected ID is:

```text
RESOLVE -1
```

No new C++ routing API is required. The frozen `ResolveNext()` already reports `transitionId=-1` for unresolved routing, and `SchoolDaysOracleTrace` fails immediately if the compiled oracle selects a different Transition than predicted.

After selection agrees, the KTRF reference Interpreter executes the same injected state and the complete observable post-state is compared.

## 5. Compared state

Resolved scenarios compare:

- selected source Transition ID;
- Advanced/Terminal result;
- post-step `ROUTE`/`SCENE`;
- nonterminal current Node coordinate and SceneKey;
- destination SceneKey;
- callback / ExternalHook order;
- accumulated Ending registrations;
- newly registered Ending ID;
- `choice_result` reset behavior;
- `callback34`;
- every recovered session Variable;
- every recovered global Variable.

Unresolved scenarios additionally require:

- no Transition result in KTRF;
- C++ `kind=unresolved` and `transition=-1`;
- empty destination and callback list;
- no new Ending;
- retained Node/SceneKey and `ROUTE`/`SCENE`;
- identical session/global state.

The C++-internal `feelingApplied` bookkeeping flag is not a KTRF Variable and is intentionally outside this comparison. Choice/Feeling semantics are already covered independently by the exhaustive Choice differential.

## 6. Acceptance criteria

A PASS requires at minimum:

```text
source Nodes covered             1857 / 1857
selected Transitions             2458 / 2458
pending Choice default checks     287 / 287
divergences                         0
```

The report also records total unique scenarios, generator-category membership, Advanced/Terminal/Unresolved outcome counts, fixture-solver work and the exact selected Transition ID set.

## 7. Run

From repository root:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_sdhq_state_matrix.ps1
```

The default pairwise cap can be increased later without changing the semantic acceptance contract:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_sdhq_state_matrix.ps1 `
  -MaxPairwisePerNode 64
```

Success ends with:

```text
BROADER STATE MATRIX DIFFERENTIAL PASS

KTRF / SCHOOL DAYS BROADER STATE MATRIX: PASS
```

## 8. What a PASS means

A PASS is strong finite-state evidence that KTRF and the compiled School Days oracle agree not only on one fixture per Transition, but also around the recovered predicate boundaries, on pending Choices, and on unresolved routing states.

It is **not** a proof over all possible 32-bit Variable valuations, and injected states are **not** claims about causal/historical gameplay reachability.

Those distinctions remain part of the conformance record.

## 9. Next gate

After this matrix passes, the next step is the **KTRF IR semantic freeze gate** for the School Days profile.

That gate should rerun/verify all structural and differential layers, hash the canonical IR/specification inputs and conformance reports, and freeze the semantic contract before physical `.ktnroute` bytes are designed.

Only after that semantic freeze should work begin on header size, section directory layout, offsets, integer widths, integrity algorithms and deterministic binary serialization.
