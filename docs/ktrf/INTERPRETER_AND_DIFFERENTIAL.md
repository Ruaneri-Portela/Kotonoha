# KTRF IR Interpreter and Differential Conformance

Status: **Implementation draft v0.1**

This stage verifies that Canonical Routing IR is not merely structurally complete: it must execute with the same observable routing semantics as the frozen School Days C++ oracle.

Oracle:

- tag: `school-days-routing-oracle-v1`
- commit: `614461c2b14951ba117b9d2dedb4983cfa8ae8e6`

## 1. Components

Reference IR interpreter:

- `tools/ktrf/interpreter.py`

Compiled C++ oracle trace adapter:

- `tests/SchoolDaysOracleTrace.cpp`

Differential verifier:

- `tools/ktrf/diff_sdhq_witnesses.py`

Runner:

- `tools/ktrf/run_sdhq_differential.ps1`

Generic interpreter tests:

- `tests/ktrf/test_interpreter.py`

## 2. Interpreter boundary

The reference interpreter executes Canonical Routing IR v0.1.

It is intentionally not a `.ktnroute` binary loader and is not yet Kotonoha's production router.

Its purpose is to define and test executable semantics before binary serialization exists.

The interpreter currently implements core v0.1:

### Expressions

- `ktrf:const`
- `ktrf:var`
- `ktrf:eq`
- `ktrf:ne`
- `ktrf:lt`
- `ktrf:le`
- `ktrf:gt`
- `ktrf:ge`
- `ktrf:and`
- `ktrf:or`
- `ktrf:not`

### Effects

- `ktrf:set`
- `ktrf:copy`
- `ktrf:add`
- `ktrf:register-ending`
- `ktrf:call-hook`

### Routing

Transitions are filtered by source Node and trigger, sorted by explicit priority and evaluated first-match.

Omitted `triggers` means `ktrf:next`.

A terminal Transition applies its Effects and marks runtime terminal without moving to another Node.

A non-terminal Transition applies its Effects in declared order and then makes its destination Node current.

## 3. Deferred Choice execution

Choice commit semantics are:

```text
write result Variable
-> execute selected option/timeout Effects in order
-> remain on current Node
-> do not evaluate routing
```

A later routing trigger evaluates Transitions.

The reference interpreter treats a Choice as committed once per current Node activation. Repeating the same result is idempotent; attempting a different result after commit is rejected.

This matches the School Days oracle behavior required to prevent feeling deltas from being applied twice.

## 4. Why a compiled trace adapter exists

The differential verifier must compare against the actual frozen `SchoolDaysRouter` implementation rather than a second Python reimplementation of the generated tables.

`SchoolDaysOracleTrace` therefore links directly with:

```text
src/SchoolDaysRouter.cpp
```

and drives only the public API:

```text
Reset
State
CurrentScene
AcceptChoice
ResolveNext
```

The adapter does not contain route logic.

Its stdin protocol is intentionally minimal:

```text
RESET
STEP <expected-route> <expected-scene> <choice-or--99> <expected-transition-id>
```

Each successful `STEP` emits one JSON object containing the post-step oracle state.

## 5. Witness source

The first direct differential corpus reuses the same 22 deterministic ending witnesses already certified by:

```text
tests/SchoolDaysFullRouterTest.cpp
```

The Python verifier parses the `kEndingXX[]` arrays directly from that source file.

This avoids copying the witness corpus into a second file that could drift independently.

## 6. Step-by-step comparison

For every witness step, the differential verifier compares:

- pre-step Route/Scene coordinate;
- accepted Choice input when present;
- selected source Transition ID;
- Advanced versus Terminal result;
- post-step Route/Scene coordinate;
- current SceneKey;
- destination SceneKey;
- callback sequence versus KTRF ExternalHook sequence;
- full accumulated Ending registration list;
- internal `choice_result`;
- internal `callback34`;
- every recovered session Variable, treating an absent C++ map key as the oracle's zero-read default;
- every recovered global Variable, with explicit reset defaults preserved by both runtimes.

This is substantially stronger than comparing only the final Ending.

## 7. What this differential proves

A PASS proves that, for all steps in the 22 certified causal ending witnesses:

```text
same initial reset state
same Choice inputs
same Next triggers

C++ SchoolDaysRouter
        ==
KTRF Canonical IR Interpreter
```

for the compared observable routing state.

The witness corpus spans all 22 native Endings and many cross-route/cross-episode branches.

## 8. What it does not yet prove

The 22-witness differential is not the final exhaustive proof over every possible reachable routing state.

It does not yet prove equivalence for:

- every projected reachable state in the historical exhaustive exploration;
- deliberately injected arbitrary internal states;
- Back/Replay/Load persistence behavior;
- frontend handoff UI;
- Android platform integration.

Those remain separate layers.

The generated C++ oracle therefore remains authoritative until a later exhaustive differential stage is complete.

## 9. Run

On the configured Windows desktop build with `KOTONOHA_ROUTER_TESTS=ON`:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\ktrf\run_sdhq_differential.ps1
```

The runner performs:

```text
1. interpreter/differential Python syntax
2. generic interpreter regression tests
3. fresh School Days oracle -> IR export and validation
4. build SchoolDaysOracleTrace
5. 22-witness step-by-step differential
```

Success ends with:

```text
KTRF / SCHOOL DAYS DIFFERENTIAL: PASS
```

and the Python differential reports:

```text
witnesses=22
steps=<total compared witness steps>
divergences=0
DIFFERENTIAL PASS
```

## 10. Next acceptance layer

After witness-level zero divergence, the next goal is broader/exhaustive differential execution.

Only after the executable IR model is proven sufficiently equivalent should KTRF binary layout design begin.

This ordering is intentional:

```text
semantics
-> structural validation
-> profile validation
-> executable IR
-> differential equivalence
-> binary serialization
```

The binary container must serialize already-proven semantics rather than becoming the place where semantics are invented.
