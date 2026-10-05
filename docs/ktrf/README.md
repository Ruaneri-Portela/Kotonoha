# KTRF — Kotonoha Routing Format

Status: **Design Draft v0.1**

KTRF is the generic routing format used by Kotonoha. Its intended binary extension is `.ktnroute`.

This directory defines the semantics first. The binary byte layout is intentionally **not** defined yet.

The first conformance oracle is:

- School Days HQ Routing Model v1
- tag: `school-days-routing-oracle-v1`
- commit: `614461c2b14951ba117b9d2dedb4983cfa8ae8e6`

The oracle exists so KTRF can be tested against a known routing system without making School Days-specific concepts part of the KTRF core.

## Design goals

KTRF must:

- represent routing independently from a specific Days-series game or routing DLL;
- preserve logical nodes that do not have physical scene files;
- preserve ordered transition priority;
- preserve delayed choice routing;
- preserve state variables, predicates, ordered effects, endings and external hooks;
- separate static routing from save files and frontend UI;
- be extensible for unknown future semantics discovered in Shiny Days, Cross Days or other titles;
- reject unsupported required semantics instead of guessing;
- support deterministic compilation and differential validation;
- keep the human-readable Routing IR separate from the future binary container.

## Non-goals

KTRF v0.1 does not define:

- save-file serialization;
- title/menu behavior;
- media container formats;
- ORS syntax;
- the future `.ktnroute` byte layout;
- a universal game script language.

## Source design influence

The Hokkaido recipe-file documentation supplied for this work demonstrates useful container principles: fixed identity/version data, flags/capabilities, typed records, explicit lengths/counts, UTF-8 names, configurable integrity mechanisms and reserved compatibility space.

KTRF adopts those principles conceptually. It does **not** copy that format, and this v0.1 phase deliberately avoids choosing byte sizes, offsets or physical section layouts.

## Documents

- `SPECIFICATION.md` — normative conceptual specification.
- `DATA_MODEL.md` — exact definitions of Node, Transition, Expression, Effect, Variable, Choice, Ending, ExternalHook and ResourceLocator.
- `ROUTING_IR.md` — canonical JSON Routing IR.
- `VERSIONING_EXTENSIONS.md` — versioning, feature negotiation, namespaces and extensions.
- `VALIDATION.md` — structural, semantic, profile and oracle-validation layers.
- `profiles/school-days-hq.md` — mapping from the School Days oracle into the generic model.
- `SCHOOL_DAYS_EXPORT.md` — deterministic School Days executable oracle → Routing IR lowering.
- `ORACLE_BOUNDARY_NOTE.md` — clarification separating broader structural-research branches from the frozen executable oracle.
- `INTERPRETER_AND_DIFFERENTIAL.md` — executable IR semantics and direct C++ witness differential validation.
- `DIFFERENTIAL_COVERAGE.md` — coverage accounting for the 22-ending differential corpus and the gap to broader branch/state validation.
- `TARGETED_TRANSITION_DIFFERENTIAL.md` — state-injection differential strategy for closing uncovered executable transitions without claiming causal reachability.
- `../../schemas/ktrf-routing-ir.schema.json` — JSON Schema for Routing IR v0.1.
- `../../examples/ktrf/minimal-routing-ir.json` — minimal valid example.

## Reference tooling

Generic semantic validation:

```powershell
python .\tools\ktrf\validate_ir.py .\examples\ktrf\minimal-routing-ir.json
```

Full generic validation runner:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\ktrf\run_validation.ps1
```

School Days oracle export + all validation layers:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\ktrf\run_sdhq_export.ps1
```

Reference IR interpreter tests:

```powershell
python .\tests\ktrf\test_interpreter.py
```

School Days C++ oracle versus KTRF IR witness differential + coverage audit:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\ktrf\run_sdhq_differential.ps1
```

Target all witness-uncovered executable transitions:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\ktrf\run_sdhq_targeted_transition_diff.ps1
```

Default generated School Days IR:

```text
build/ktrf/school-days-hq.routing.json
```

Default differential reports:

```text
build/ktrf/school-days-hq.differential-coverage.json
build/ktrf/school-days-hq.targeted-transition-diff.json
```

The generic validator performs whole-document checks that JSON Schema alone cannot express, including references, expression cycles, type compatibility, namespace/feature rules, terminal invariants and transition priority collisions.

The School Days profile validator additionally checks the frozen executable-oracle inventory, source-index coverage, routing-only nodes, ending/hook catalogs, feeling-resolution coverage, transition lowering order, callback_38 edge counts and the structural-dead exclusion boundary.

The reference interpreter executes the canonical IR directly. `SchoolDaysOracleTrace` drives the compiled frozen `SchoolDaysRouter` routing implementation and emits machine-readable state, allowing both causal-witness and targeted state-injection differential comparison.

The coverage auditor measures which of the 2,458 executable Transitions, 1,857 Nodes, 287 Choice Nodes, terminal edges and callback_38 handoffs were exercised by the causal corpus. Coverage accounting is diagnostic: zero divergence and percentage coverage are separate facts.

The targeted transition layer then searches for one consistent routing pre-state per uncovered Transition and injects the same state into both runtimes. A targeted PASS proves direct transition-semantic equivalence for those fixtures; it does not convert injected states into claims about historical gameplay reachability.

Reference tests:

```powershell
python .\tests\ktrf\test_validate_ir.py
python .\tests\ktrf\test_export_sdhq_ir.py
python .\tests\ktrf\test_interpreter.py
```

## Current pipeline

```text
School Days frozen C++ executable oracle
        |
        v
export_sdhq_to_ir.py
        |
        v
Canonical Routing IR JSON
        |
        +--> JSON Schema validation
        |
        +--> generic semantic validation
        |
        +--> School Days profile validation
        |
        v
reference IR interpreter
        |
        +<---------------- SchoolDaysOracleTrace / compiled C++ oracle
        |
        v
22-ending causal witness differential: zero divergence
        |
        v
coverage audit
        |
        v
targeted transition state-injection differential
        |
        `--> next: Choice/FeelingResolution + effect-stress + broader state differential

future, only after sufficient zero-divergence coverage:

Canonical Routing IR
        |
        v
KTRF compiler
        |
        v
*.ktnroute
```

## Core rule

A `.ktnroute` implementation is conformant only if its decoded semantic model can be compared against the same canonical Routing IR used to compile it.

The binary format is therefore a serialization of KTRF semantics, not the definition of those semantics.

The generated School Days JSON IR is not the `.ktnroute` binary and is not yet allowed to replace the frozen C++ router. The frozen C++ router remains the executable oracle until differential execution reaches the required zero-divergence coverage.
