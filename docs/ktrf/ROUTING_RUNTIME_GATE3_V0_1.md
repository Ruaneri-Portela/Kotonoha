# KTRF Routing Runtime v0.1 — Gate 3 Native Parity

Status: **native differential gate**

Gate 3 compares the frozen `SchoolDaysRouter` C++ oracle directly against the production native KTRF path:

```text
school-days-hq.ktnroute
  -> Ktrf.c
  -> KtrfTyped.c
  -> KtrfRuntime.c
  -> KtrfRouter.c
```

No Python IR interpreter participates in the native side of this gate.

## Corpus

The gate reuses the 22 deterministic ending witness arrays from `tests/SchoolDaysFullRouterTest.cpp` and the same `RESET` / `STEP` protocol used by `SchoolDaysOracleTrace`.

For each witness step, both routers receive the same pre-state path and Choice input. The expected source transition ID embedded in the witness must also be selected by both implementations.

## Compared state

After every routing step Gate 3 compares:

- selected source transition ID;
- advanced versus terminal result;
- destination SceneKey;
- current SceneKey;
- mirrored `ROUTE` / `SCENE` values;
- internal `choice_result`;
- internal `callback34`;
- callback / ExternalHook sequence;
- accumulated Ending registrations and newly registered Ending;
- every recovered School Days session variable;
- every recovered School Days global variable.

The native trace emits canonical physical KTRF indices plus the complete physical `VARS` vector. The differential uses the freshly exported canonical IR only to map those indices back to stable semantic IDs and School Days source symbols. Runtime decisions still come exclusively from the native `.ktnroute` stack.

## Acceptance

A PASS requires all 22 witnesses and every contained step to produce zero divergences:

```text
witnesses=22
steps=<all witness steps>
divergences=0
NATIVE DIFFERENTIAL PASS
```

The runner then emits:

```text
KTRF ROUTING RUNTIME v0.1 / GATE 3 NATIVE PARITY: PASS
```

A PASS demonstrates witness-level executable parity between the frozen School Days oracle and the native `.ktnroute` reader/runtime/router stack. Broader reachable-state differential coverage and Android integration remain subsequent gates.
