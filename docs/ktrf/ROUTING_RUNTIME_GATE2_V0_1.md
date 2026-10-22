# KTRF Routing Runtime v0.1 — Gate 2

Status: **native routing decision gate**

Gate 1 established native variable state plus EXPR/EFFT execution. Gate 2 adds
the graph decision layer over the accepted typed binary reader.

## Native router

Files:

```text
include/Kotonoha/routing/KtrfRouter.h
src/routing/KtrfRouter.c
```

The router wraps `Kotonoha_KtrfRuntime` and owns only routing activation state:

- current `NODE[index]`;
- terminal state;
- per-activation deferred Choice commit state.

No School Days-specific symbol is hard-coded into this layer.

## Deferred Choice semantics

The native implementation follows the already-certified reference interpreter:

```text
commit result
-> write Choice.result_variable
-> apply outcome EFFT refs in declared order
-> remain on current Node
-> do not route
```

A Choice is committed once per Node activation. Repeating the same semantic
result is idempotent; a different result is rejected without reapplying effects.

Every uncommitted `ktrf:deferred` Choice at the current Node blocks routing.

## Transition semantics

`Kotonoha_KtrfRouterTrigger()`:

1. rejects routing after terminal state;
2. blocks on an unresolved deferred Choice;
3. filters `TRAN` by current source Node;
4. filters by trigger;
5. evaluates optional predicate `EXPR`;
6. selects first match by `(priority, canonical TRAN index)`;
7. executes ordered transition `EFFT` references;
8. marks terminal or activates destination Node.

Because `TRAN` physical indices are canonical stable-ID order, the tie-breaker is
equivalent to the reference interpreter's `(priority, id)` ordering.

Absent `triggers` means `ktrf:next`.

## School Days gate

The runner regenerates the frozen School Days IR and production `.ktnroute`,
then validates:

- 287 deferred Choices;
- every Choice option commit;
- every timeout commit;
- choice idempotence;
- no routing during Choice commit;
- unresolved Choice routing block;
- all 1,857 Nodes under default state;
- trigger filtering;
- predicate evaluation;
- explicit priority selection across 2,458 Transitions;
- ordered transition Effect execution;
- terminal versus destination behavior.

Runner:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\ktrf\run_routing_runtime_gate2_v0_1.ps1
```

Success ends with:

```text
KTRF ROUTING RUNTIME GATE 2 PASS
choice_deferred_semantics=PASS
transition_priority_selection=PASS
ordered_transition_effects=PASS

KTRF ROUTING RUNTIME v0.1 / GATE 2: PASS
```

After this gate, the remaining routing work is profile parity: drive this native
router with the same School Days witness states/choices as the frozen C++ oracle
and require zero divergence.
