# KTRF Routing Runtime v0.1 — Gate 4 Native Broader State Matrix

Status: **native broad-parity gate**

Gate 3 proved step-by-step native parity for the 22 certified causal ending
witnesses. Gate 4 broadens that proof around the recovered routing predicate
space without changing the routing implementation.

## Backends

The same deterministic injected states are executed by:

```text
frozen SchoolDaysRouter C++ oracle
                vs
school-days-hq.ktnroute
 -> Ktrf native reader
 -> typed decoder
 -> KtrfRuntime
 -> KtrfRouter
```

The Python KTRF interpreter is not an execution backend in this gate. Existing
Python selector/state-matrix code is reused only to generate the finite scenario
set and to identify expected source Transition IDs.

## Scenario families

Gate 4 reuses the accepted broader state matrix:

1. coherent default state for every logical Node;
2. at least one satisfying fixture for every executable Transition;
3. single-variable predicate-boundary perturbations;
4. deterministic limited pairwise boundary combinations.

The default pairwise cap is 32 generated combinations per eligible source Node.

## Native state injection

`KtrfNativeStateMatrixTrace` exposes a test-only protocol:

```text
RESET
ACTIVATE <NODE index>
SET <VARS index> <integer>
MARK_CHOICES_COMMITTED
RESOLVE
```

`MARK_CHOICES_COMMITTED` does not execute Choice Effects. It models a
pre-existing committed-choice checkpoint after the scenario generator has
already injected the complete variable state. Pending `choice_result=-2`
scenarios deliberately omit it and therefore exercise the native deferred-choice
routing gate.

## Compared behavior

Each scenario compares:

- selected source Transition ID;
- Advanced / Terminal / Unresolved result;
- deferred Choice blocked status for pending default Choice Nodes;
- source, destination and current Node identity;
- destination/current SceneKey;
- callback / ExternalHook sequence;
- Ending registrations;
- `choice_result` and `callback34`;
- every recovered School Days session Variable;
- every recovered School Days global Variable.

## Acceptance

PASS requires:

```text
source Nodes covered             1857 / 1857
selected Transitions             2458 / 2458
pending Choice default checks     287 / 287
divergences                         0
```

The scenario total and Advanced/Terminal/Unresolved counts are data-dependent
outputs of the deterministic matrix and are recorded in:

```text
build/ktrf/school-days-hq.native-state-matrix-diff.json
```

## Run

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_routing_runtime_gate4_v0_1.ps1
```

Optional wider pairwise sampling:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_routing_runtime_gate4_v0_1.ps1 `
  -MaxPairwisePerNode 64
```

Successful execution ends with:

```text
NATIVE BROADER STATE MATRIX PASS
KTRF ROUTING RUNTIME v0.1 / GATE 4 BROAD PARITY: PASS
```

A PASS closes the native routing-runtime semantic parity stage for the frozen
School Days HQ routing contract. Platform/frontend integration remains a
separate layer.
