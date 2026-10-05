# KTRF — Kotonoha Routing Format

Status: **Semantic v0.1 frozen / Binary v0.1 in progress**

KTRF is the generic routing format used by Kotonoha. Its binary extension is `.ktnroute`.

The semantic contract was frozen before the binary layout was started:

- semantic freeze tag: `ktrf-semantic-v0.1-sdhq`
- semantic implementation commit: `88bd2ad0de8b68bac242839d771b381e22e65f22`
- School Days HQ Routing Model v1 oracle tag: `school-days-routing-oracle-v1`
- oracle commit: `614461c2b14951ba117b9d2dedb4983cfa8ae8e6`
- semantic freeze root SHA-256: `c06717b5bc57c398cff4dca5f5f5f8185ee750ce168b0a0700b66cedb70e870d`

Binary work continues on `research/ktrf-binary-v0.1-20261005` and MUST preserve the frozen semantic behavior.

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
- keep the human-readable Routing IR separate from the binary container.

## Non-goals

KTRF v0.1 does not define:

- save-file serialization;
- title/menu behavior;
- media container formats;
- ORS syntax;
- a universal game script language.

## Source design influence

The Hokkaido recipe-file documentation supplied for this work demonstrates useful container principles: fixed identity/version data, flags/capabilities, typed records, explicit lengths/counts, UTF-8 names, configurable integrity mechanisms and reserved compatibility space.

KTRF adopts those principles conceptually. It does **not** copy that format.

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
- `BROADER_STATE_DIFFERENTIAL.md` — finite boundary/state matrix covering every Node, every executable Transition and unresolved/pending-Choice cases.
- `SEMANTIC_FREEZE_GATE.md` — semantic freeze acceptance gate and evidence policy.
- `BINARY_FORMAT_V0_1.md` — physical `.ktnroute` container v0.1 draft: header, directory, integrity, ordering and STRS envelope.
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

Broader finite state-matrix differential:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\ktrf\run_sdhq_state_matrix.ps1
```

Full semantic freeze gate:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\ktrf\run_sdhq_semantic_freeze.ps1
```

Binary container v0.1 envelope + STRS tests:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\ktrf\run_binary_container_v0_1.ps1
```

## Semantic conformance status

```text
22 ending witnesses                    PASS / zero divergence
witness transition coverage            1081 / 2458
targeted transition fixtures           1377 / 1377
combined transition coverage           2458 / 2458 (100%)
combined transition divergences        0

Choice nodes                            287 / 287
Choice outcomes / FeelingResolution     772 / 772
FeelingDelta source rows                302 / 302
Choice seed modes                       2
Choice scenarios                        1544
Choice divergences                      0

source Effects                          6183 / 6183
transitions with source Effects         2458
Effect stress seed modes                2
Effect stress scenarios                 4916
Effect stress divergences               0

broader state-matrix Nodes              1857 / 1857
broader state-matrix Transitions        2458 / 2458
pending Choice gate checks              287 / 287
state-matrix scenarios                  3371
state-matrix divergences                0

semantic freeze gate                    PASS
semantic freeze root                    c06717b5bc57c398cff4dca5f5f5f8185ee750ce168b0a0700b66cedb70e870d
```

Recovered source-Effect inventory observed by the stress gate:

```text
SetSessionConst          5911
SetSessionFromSession      40
SetGlobalConst             67
RegisterEnding             25
Callback                  140
```

All 40 `SetSessionFromSession` source operands are branch/coordinate constrained in the recovered executable model, so they cannot be independently perturbed without potentially changing branch selection. They are still executed and compared under the stress scenarios; this limitation is kept explicit rather than overstating coverage.

## Binary v0.1 phase 1

The first physical-format phase now defines and tests only the generic container envelope and STRS string pool.

Frozen draft constants for this phase:

```text
magic                      KTRF
binary version              0.1
byte order                  little-endian
fixed header                96 bytes
section directory entry     48 bytes
default alignment           8 bytes
whole-content integrity     SHA-256
header integrity            CRC32
section integrity           CRC32
```

The reference implementation is:

```text
tools/ktrf/binary_container_v0_1.py
```

Regression tests are:

```text
tests/ktrf/test_binary_container_v0_1.py
```

The first smoke artifact generated by the runner is:

```text
build/ktrf/binary-v0.1-smoke.ktnroute
```

It is a container/STRS smoke artifact, **not yet a compiled School Days routing file**.

## Current pipeline

```text
School Days frozen C++ executable oracle
        |
        v
Canonical Routing IR JSON
        |
        v
semantic validators + differential gates
        |
        v
semantic freeze tag: ktrf-semantic-v0.1-sdhq
        |
        v
KTRF binary v0.1 container
        |
        +--> 96-byte HEADER
        +--> 48-byte section directory entries
        +--> SHA-256 / CRC32 integrity
        +--> canonical placement/alignment
        +--> STRS canonical UTF-8 pool
        |
        v
next: semantic section record layouts
        |
        v
IR -> .ktnroute compiler
        |
        v
.ktnroute -> decoded semantic model
        |
        v
binary round-trip + oracle differential
```

## Core rule

A `.ktnroute` implementation is conformant only if its decoded semantic model can be compared against the same canonical Routing IR used to compile it.

The binary format is therefore a serialization of KTRF semantics, not the definition of those semantics.

The frozen C++ School Days router remains the external conformance oracle while the binary encoder/decoder is developed. The binary phase may change physical layout during v0.1 research, but it may not silently change the frozen semantic contract.
