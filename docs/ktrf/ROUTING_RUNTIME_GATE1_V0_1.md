# KTRF Routing Runtime v0.1 — Gate 1

Gate 1 starts executable routing semantics on top of the validated native typed reader.

## Scope

- native variable store initialized from `VARS` defaults;
- runtime value representation for bool/integer/float/string/bytes/entity values;
- recursive `EXPR` evaluation with cycle guard;
- frozen School Days operators: `ktrf:eq`, `ktrf:ne`, `ktrf:gt`, `ktrf:le`, `ktrf:and`, `ktrf:or`;
- `EFFT` execution for `set`, `add`, `copy`, `register-ending`, and `call-hook`;
- target-type coercion and integer/range validation;
- callbacks for ending registration and external hooks.

## Gate

`tools/ktrf/run_routing_runtime_gate1_v0_1.ps1` regenerates the frozen School Days IR and production `.ktnroute`, compiles a standalone native harness, evaluates all 1,892 expressions from default state, then executes all 11,356 effects independently from reset defaults.

The test also requires all five frozen effect shapes to be exercised and both external side-effect callback classes to be invoked.

## Out of scope

Gate 1 does not yet choose transitions or resolve choices. Those are the next runtime layer, built on this variable/expression/effect engine.
