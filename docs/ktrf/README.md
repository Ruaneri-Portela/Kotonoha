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
- `../../schemas/ktrf-routing-ir.schema.json` — JSON Schema for Routing IR v0.1.
- `../../examples/ktrf/minimal-routing-ir.json` — minimal valid example.

## Reference tooling

Semantic validation:

```powershell
python .\tools\ktrf\validate_ir.py .\examples\ktrf\minimal-routing-ir.json
```

The validator performs whole-document checks that JSON Schema alone cannot express, including references, expression cycles, type compatibility, namespace/feature rules, terminal invariants and transition priority collisions.

Reference tests:

```powershell
python .\tests\ktrf\test_validate_ir.py
```

## Core rule

A `.ktnroute` implementation is conformant only if its decoded semantic model can be compared against the same canonical Routing IR used to compile it.

The binary format is therefore a serialization of KTRF semantics, not the definition of those semantics.
