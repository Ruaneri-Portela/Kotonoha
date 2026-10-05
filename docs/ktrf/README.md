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
- `CHOICE_FEELING_DIFFERENTIAL.md` — exhaustive Choice outcome / FeelingResolution / FeelingDelta differential validation.
- `SOURCE_EFFECT_STRESS.md` — non-default pre-state stress differential for all recovered source Effects.
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

Exhaustive Choice/Feeling differential:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\ktrf\run_sdhq_choice_diff.ps1
```

Source Effect stress differential:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\ktrf\run_sdhq_effect_stress.ps1
```

Default generated School Days IR:

```text
build/ktrf/school-days-hq.routing.json
```

Default differential reports:

```text
build/ktrf/school-days-hq.differential-coverage.json
build/ktrf/school-days-hq.targeted-transition-diff.json
build/ktrf/school-days-hq.choice-diff.json
build/ktrf/school-days-hq.effect-stress-diff.json
```

The generic validator performs whole-document checks that JSON Schema alone cannot express, including references, expression cycles, type compatibility, namespace/feature rules, terminal invariants and transition priority collisions.

The School Days profile validator additionally checks the frozen executable-oracle inventory, source-index coverage, routing-only nodes, ending/hook catalogs, feeling-resolution coverage, transition lowering order, callback_38 edge counts and the structural-dead exclusion boundary.

The reference interpreter executes the canonical IR directly. `SchoolDaysOracleTrace` drives the compiled frozen `SchoolDaysRouter` routing implementation and emits machine-readable state, allowing causal-witness, targeted-transition, Choice-commit and source-Effect stress differential comparison without copying the C++ routing logic into Python.

Current transition-differential status:

```text
22 ending witnesses                   PASS / zero divergence
witness transition coverage           1081 / 2458
Targeted transition fixtures          1377 / 1377
combined transition coverage          2458 / 2458 (100%)
combined transition divergences       0
```

Current Choice/Feeling status:

```text
Choice nodes                           287 / 287
Choice outcomes / FeelingResolution    772 / 772
FeelingDelta source rows               302 / 302
seed modes                             2
scenarios                              1544
accepted checks                        1544
idempotence checks                     1544
alternate rejection checks             1544
divergences                            0
```

The Choice/Feeling layer is independent because the targeted transition fixtures inject post-choice routing state. It executes every exported Choice outcome through the frozen `AcceptChoice` implementation versus KTRF `commit_choice`, requires complete `FeelingResolution` and `FeelingDelta` source-index coverage, and checks both idempotent repeat and alternate-result rejection.

The source-Effect stress layer is also independent. Transition closure already executes all 6,183 recovered source Effects at least once through their owning transitions; the stress layer reruns every effect-bearing transition under deterministic positive and negative non-default seeds while preserving first-match branch selection, then compares the complete C++/KTRF post-state.

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
coverage audit: 1081 / 2458 transitions
        |
        v
targeted transition differential: 1377 / 1377
        |
        v
combined transition coverage: 2458 / 2458, zero divergence
        |
        v
Choice + Feeling differential: 772 / 772 outcomes, zero divergence
        |
        v
source-Effect stress differential
        |
        `--> next: broader state differential / semantic freeze gate

future, only after semantic conformance freeze:

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

The generated School Days JSON IR is not the `.ktnroute` binary and is not yet allowed to replace the frozen C++ router. The frozen C++ router remains authoritative until the semantic conformance gates are closed and frozen.
