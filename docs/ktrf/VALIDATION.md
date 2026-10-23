# KTRF Validation Model v0.1

KTRF validation is layered. A document is not semantically valid merely because it matches the JSON Schema.

## 1. Validation layers

### Layer A — JSON syntax

The file must decode as UTF-8 JSON.

### Layer B — JSON Schema

`schemas/ktrf-routing-ir.schema.json` validates structural shape:

- required properties;
- field types;
- qualified-name syntax;
- allowed object members;
- basic enums/constants;
- structural constraints.

Schema success is necessary but not sufficient.

### Layer C — semantic validation

`tools/ktrf/validate_ir.py` validates rules that require whole-document context:

- duplicate IDs;
- cross-reference existence;
- namespace declarations;
- core vocabulary recognition;
- required/optional feature consistency;
- optional runtime capability enforcement;
- Variable default compatibility;
- Expression argument references;
- Expression DAG acyclicity;
- core Expression arity/result contracts;
- predicate boolean result type;
- core Effect target/reference contracts;
- assignment/copy/add type checks;
- Ending references;
- ExternalHook references;
- Choice node/result/effect references;
- Choice result-value compatibility;
- ResourceLocator references;
- terminal/destination invariants;
- transition trigger validation;
- transition priority collisions;
- extension namespace/required-feature consistency.

### Layer D — profile validation

Future profile validators may add source-engine invariants that must not be placed in KTRF core.

Examples:

- School Days `(ROUTE, SCENE)` coordinate uniqueness;
- exact SceneKey mapping conventions;
- profile-specific history scopes;
- profile hook contracts.

### Layer E — conformance/differential validation

A serialized or imported model is conformant only after its behavior is compared with a trusted oracle.

For School Days HQ v1 the oracle is:

- tag: `school-days-routing-oracle-v1`;
- commit: `614461c2b14951ba117b9d2dedb4983cfa8ae8e6`.

This is the layer that proves semantic equivalence, rather than merely document consistency.

## 2. Command line

From repository root:

```powershell
python .\tools\ktrf\validate_ir.py .\examples\ktrf\minimal-routing-ir.json
```

Expected result:

```text
KTRF IR PASS: 0 errors, 0 warning(s) — ...minimal-routing-ir.json
```

Machine-readable output:

```powershell
python .\tools\ktrf\validate_ir.py `
  .\examples\ktrf\minimal-routing-ir.json `
  --json
```

The report format is:

```json
{
  "format": "ktrf-validation-report-v0.1",
  "valid": true,
  "errors": 0,
  "warnings": 0,
  "diagnostics": []
}
```

## 3. Structural dependency

The reference CLI uses the Python `jsonschema` package for Layer B.

If unavailable, the CLI returns a tooling error unless explicitly invoked with:

```text
--skip-schema
```

`--skip-schema` is intended for developer/unit-test scenarios only. Release validation should run both Schema and semantic validation.

## 4. Capability enforcement

Authoring validation proves that required features are declared consistently, but a runtime additionally needs to prove it implements those features.

Use:

```powershell
python .\tools\ktrf\validate_ir.py game.routing.json `
  --enforce-capabilities `
  --supported-feature overflow.shiny:predicate-x@1.0.0
```

Built-in v0.1 core capabilities are known by the validator.

A required namespaced feature not supplied to capability enforcement produces an error.

Compatibility policy for feature versions in v0.1:

- MAJOR must match;
- available version must be greater than or equal to the required version.

This policy is provisional until KTRF reaches 1.0.

## 5. Namespaced semantics

A profile Expression or Effect operator must:

1. use a declared namespace prefix;
2. have at least one `features.required` entry owned by that namespace.

This prevents a document from silently introducing routing-significant semantics that no feature negotiation mechanism exposes.

The generic semantic validator does not attempt to execute or reinterpret those operators.

## 6. Core Expression checks

The v0.1 reference validator recognizes:

- `ktrf:const`;
- `ktrf:var`;
- `ktrf:eq`;
- `ktrf:ne`;
- `ktrf:lt`;
- `ktrf:le`;
- `ktrf:gt`;
- `ktrf:ge`;
- `ktrf:and`;
- `ktrf:or`;
- `ktrf:not`.

It checks arity and boolean/numeric requirements where currently specified.

All transition predicates must resolve to an Expression declaring `result_type: "ktrf:bool"`.

Expression-to-Expression references must form a DAG. Cycles are invalid.

## 7. Core Effect checks

The v0.1 reference validator recognizes:

- `ktrf:set`;
- `ktrf:copy`;
- `ktrf:add`;
- `ktrf:register-ending`;
- `ktrf:call-hook`.

Effects remain ordered by their reference order in Choices and Transitions.

The validator must never sort an Effect list as part of validation or canonicalization.

## 8. Transition priority

Transitions use explicit integer priority and first-match semantics.

Within the same `(source Node, trigger)` pair, two transitions cannot have the same priority.

If `triggers` is omitted, semantic validation treats it as:

```json
["ktrf:next"]
```

This makes priority deterministic independently of JSON array order.

## 9. Terminal rule

A terminal Transition:

- must not have a destination;
- may register zero, one or multiple Endings through ordered Effects;
- must not be interpreted as implicitly registering an Ending.

A non-terminal Transition must have a valid destination Node.

This rule is required by the School Days oracle, where some terminal transitions do not directly contain `RegisterEnding` because the outcome was registered earlier.

## 10. Diagnostics

Diagnostic codes are stable within the v0.1 tooling series and grouped by family:

```text
KTRF000x  parser/schema/tooling
KTRF100x  identity/indexes
KTRF110x  namespaces/qualified names
KTRF120x  features/capabilities
KTRF130x  generic cross references
KTRF140x  variables
KTRF150x  expressions
KTRF160x  effects
KTRF170x  choices
KTRF180x  transitions
KTRF190x  extensions
```

Diagnostic-code stability is a tooling promise, not part of the future binary KTRF format.

## 11. Test suite

Reference semantic tests live under:

`tests/ktrf/test_validate_ir.py`

Run:

```powershell
python -m unittest tests.ktrf.test_validate_ir -v
```

or directly:

```powershell
python .\tests\ktrf\test_validate_ir.py
```

The suite currently covers:

- minimal valid IR;
- duplicate IDs;
- missing references;
- Expression cycles;
- terminal/non-terminal destination rules;
- transition priority collisions;
- undeclared namespaces;
- Choice type mismatch;
- namespaced operator feature requirements;
- runtime capability enforcement.

## 12. What this validator does not prove

A semantic PASS does not prove that the routing model matches a source game.

It proves only that the IR is self-consistent under the KTRF v0.1 semantic rules.

Source-game fidelity requires profile validation plus oracle/differential testing.
