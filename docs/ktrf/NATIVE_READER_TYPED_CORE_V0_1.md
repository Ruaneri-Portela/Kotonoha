# KTRF Native Reader v0.1 — Gate 2 / Typed Core

Gate 2 layers a typed, zero-copy semantic reader over the Gate 1 container reader.

The binary envelope remains owned and validated by `Ktrf.c`. `KtrfTyped.c` decodes
the physical v0.1 records without JSON, Python, SDL, FFmpeg, or platform-specific
dependencies.

## Typed sections

All full-core semantic sections are decoded:

- `META`
- `NSPC`
- `FEAT`
- `ENTR`
- `VARS`
- `RSRC`
- `HOOK`
- `ENDG`
- `EXPR`
- `EFFT`
- `CHOI`
- `NODE`
- `TRAN`

`STRS` remains exposed by the Gate 1 string-pool API.

## Runtime-facing API

`include/Kotonoha/parsers/KtrfTyped.h` exposes typed records and indexed accessors.

Nested pools are accessible without copying:

- expression arguments
- effect valueRefs
- choice options
- choice timeout
- choice option/timeout Effect references
- Node Resource references
- Transition Effect references
- Transition trigger strings

Physical references remain physical table indices, which is the representation
the routing runtime will consume directly.

## Validation

`Kotonoha_KtrfValidateTypedCore()` validates the complete typed graph before use:

- canonical record order
- fixed record/header sizes
- pool offsets and exact payload sizes
- contiguous/full ownership of pool slices
- reserved bits/fields
- variable default type/kind agreement
- scalar/literal encodings
- Expression valueRef kinds and cross-references
- Expression acyclicity
- Effect shape/op agreement
- Effect payload shape and target catalog ranges
- Choice Node/Variable/Effect references
- Choice option uniqueness and timeout pool ownership
- Node Resource references
- Transition source/destination/predicate/effect references
- terminal/destination rules
- trigger flag/count agreement and duplicate-trigger rejection

## School Days gate

Run:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_native_ktrf_reader_gate2_v0_1.ps1
```

The gate regenerates the frozen School Days IR and production `.ktnroute`, builds
the standalone C/C++ reader harness, validates the full typed graph, and
enumerates every nested pool.

Expected frozen top-level counts:

- VARS: 497
- RSRC: 1855
- HOOK: 3
- ENDG: 22
- EXPR: 1892
- EFFT: 11356
- CHOI: 287
- NODE: 1857
- TRAN: 2458

Success banner:

```text
KTRF NATIVE READER v0.1 / GATE 2 TYPED CORE: PASS
```

A Gate 2 PASS means the native reader can consume the complete physical routing
graph. The next milestone is the routing runtime: evaluate `EXPR`, execute
`EFFT`, resolve `CHOI`, and select `TRAN`.
