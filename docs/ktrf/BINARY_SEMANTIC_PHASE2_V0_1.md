# KTRF Binary v0.1 — Semantic Tables Phase 2

Status: **implementation draft / round-trip gate**

Phase 2 extends the already accepted phase-1 mapping with the first canonical
catalogs of referenciable entities:

```text
META
STRS
NSPC
FEAT
ENTR
VARS
RSRC  ResourceLocator catalog
HOOK  ExternalHook catalog
ENDG  Ending catalog
```

`EXPR`, `EFFT`, `CHOI`, `NODE` and `TRAN` remain deferred. A phase-2 artifact is
therefore still not a complete routable `.ktnroute`.

The semantic source remains the frozen KTRF Routing IR v0.1 / School Days HQ
oracle. Binary layout must preserve that semantic model; it must not redefine it.

## 1. Canonical catalog indices

`RSRC`, `HOOK` and `ENDG` are the first KTRF binary entity catalogs.

For each catalog:

1. records are sorted by raw UTF-8 bytes of stable semantic `id`;
2. duplicate IDs are rejected;
3. the zero-based record position after sorting is the **canonical binary table
   index**;
4. source JSON traversal order never affects the index;
5. every record still stores its stable semantic ID through `STRS`.

Therefore a later binary reference may use a compact `u32` table index without
making semantic identity depend on physical order.

The null table-reference sentinel remains:

```text
0xFFFFFFFF
```

Decoding always reconstructs stable semantic IDs. Table indices are a physical
serialization detail only.

## 2. String collection

Phase 2 adds these strings to the canonical `STRS` pool before any section is
encoded:

- `ResourceLocator.id`;
- `ResourceLocator.scheme`;
- string `ResourceLocator.value`;
- optional `ResourceLocator.media_type`;
- `ExternalHook.id`;
- `ExternalHook.symbol`;
- `ExternalHook.contract`;
- `Ending.id`;
- optional `Ending.label`;
- string-valued `Ending.code`.

All phase-1 strings remain collected exactly as before.

## 3. RSRC — ResourceLocator catalog

Each `RSRC` record is exactly 24 bytes:

```text
u32 id_str
u32 scheme_str
u32 value_str
u32 media_type_str
u32 flags
u32 reserved
```

`media_type_str` is `0xFFFFFFFF` when absent.

`reserved` MUST be zero.

### 3.1 Flags

```text
0x00000001  REQUIRED
0x00000002  HAS_REQUIRED
```

Rules:

- `HAS_REQUIRED=0, REQUIRED=0`: semantic `required` member is absent;
- `HAS_REQUIRED=1, REQUIRED=0`: `"required": false`;
- `HAS_REQUIRED=1, REQUIRED=1`: `"required": true`;
- `REQUIRED=1` with `HAS_REQUIRED=0` is invalid;
- unknown flag bits are rejected.

The explicit presence bit prevents the binary format from silently treating an
absent optional member as equivalent to `false`.

### 3.2 ResourceLocator.value scope

The generic Routing IR schema allows `ResourceLocator.value` to be arbitrary
JSON. Phase 2 deliberately standardizes only **string values**.

A non-string value MUST be rejected by the phase-2 compiler. It MUST NOT be
silently stringified, JSON-packed, or assigned an invented binary contract.

This is sufficient for the frozen School Days HQ profile, whose 1,855 resource
locators use string scene keys.

`metadata` and `extensions` remain outside this phase's semantic projection.

## 4. HOOK — ExternalHook catalog

Each `HOOK` record is exactly 16 bytes:

```text
u32 id_str
u32 symbol_str
u32 contract_str
u32 reserved
```

`reserved` MUST be zero.

The phase preserves:

- stable hook ID;
- qualified hook symbol;
- contract string.

`ExternalHook.arguments_schema` is intentionally deferred. If present, a phase-2
compiler MUST reject the hook rather than dropping or inventing a representation
for that schema.

`metadata` and `extensions` remain outside this phase's semantic projection.

## 5. ENDG — Ending catalog

Each `ENDG` record is exactly 24 bytes:

```text
u32 id_str
u32 label_str
u8  code_kind
u8  flags
u16 reserved
u32 payload_a
u64 payload_b
```

`label_str` is `0xFFFFFFFF` when absent.

`flags` and `reserved` MUST be zero in binary v0.1 phase 2.

### 5.1 Ending.code kinds

The optional presence of `code` is preserved explicitly:

```text
0  ABSENT
1  NULL
2  BOOL
3  INT64
4  FLOAT64
5  STRING
```

Encodings:

`ABSENT`

```text
payload_a = 0
payload_b = 0
```

`NULL`

```text
payload_a = 0
payload_b = 0
```

`BOOL`

```text
payload_a = 0
payload_b = 0 or 1
```

`INT64`

```text
payload_a = 0
payload_b = signed int64 two's-complement bits
```

`FLOAT64`

```text
payload_a = 0
payload_b = IEEE-754 binary64 bits
```

Only finite floating-point values are accepted.

`STRING`

```text
payload_a = STRS index
payload_b = 0
```

Integers outside signed 64-bit range and unsupported scalar/container forms are
rejected rather than coerced.

`metadata` and `extensions` remain outside this phase's semantic projection.

## 6. School Days HQ phase-2 expectations

For the frozen School Days HQ oracle, phase 2 must compile at least:

```text
META = 1
NSPC = 1
FEAT = 8
ENTR = 1
VARS = 497
RSRC = 1855
HOOK = 3
ENDG = 22
```

The School Days exporter currently emits:

- 1,855 string-valued resource locators;
- 3 external callback hooks;
- 22 endings whose codes are integers `0..21`.

No School Days-specific IDs, counts, or meanings are hard-coded into the binary
record layouts.

## 7. Round-trip acceptance rule

Phase 2 is accepted only if:

```text
Canonical Routing IR
        |
        v
phase-2 encoder
        |
        v
META/STRS/NSPC/FEAT/ENTR/VARS/RSRC/HOOK/ENDG
        |
        v
phase-2 decoder
        |
        v
canonical phase-2 semantic projection
```

The decoded projection MUST equal the canonical phase-2 projection.

Repeated compilation of semantically identical input whose declaration arrays
have different source order MUST produce byte-identical output.

## 8. What phase 2 freezes for later graph sections

After this phase, later binary tables may reference:

```text
RSRC[index]
HOOK[index]
ENDG[index]
```

using canonical `u32` table indices.

The invariant is:

```text
stable semantic ID
        |
        v
canonical raw-UTF-8 ID ordering
        |
        v
zero-based physical table index
        |
        v
decoder restores the same stable semantic ID
```

This rule is the bridge needed before lowering dense references in:

```text
EXPR -> EFFT -> CHOI -> NODE -> TRAN
```

Phase 2 does **not** yet authorize converting `ENTR.node` to a `NODE` table index.
That decision remains deferred until the `NODE` layout itself is standardized.
