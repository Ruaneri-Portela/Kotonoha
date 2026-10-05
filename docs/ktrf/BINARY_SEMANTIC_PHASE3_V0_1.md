# KTRF Binary v0.1 — Semantic Tables Phase 3 / EXPR

Status: **implementation draft / round-trip gate**

Phase 3 extends the accepted phase-2 binary mapping with:

```text
EXPR  canonical expression DAG catalog
```

The represented sections are now:

```text
META
STRS
NSPC
FEAT
ENTR
VARS
RSRC
HOOK
ENDG
EXPR
```

`EFFT`, `CHOI`, `NODE` and `TRAN` remain deferred. A phase-3 artifact is still
not a complete routable `.ktnroute`.

The frozen Canonical Routing IR remains authoritative. This phase only lowers
that model into a deterministic physical representation.

## 1. Canonical EXPR indices

Expressions are sorted by raw UTF-8 bytes of stable semantic `id`.

The zero-based record position after sorting is the canonical `EXPR` table
index.

Therefore:

```text
source JSON order
    does not matter

stable semantic expression ID
        |
        v
raw UTF-8 ID sort
        |
        v
canonical EXPR[index]
```

Each expression record still stores its stable ID through `STRS`, so decoding
always reconstructs semantic identity.

Forward references are allowed. Cycles are not.

## 2. Semantic order versus canonical order

Expression **records** are canonically sorted by ID.

Expression **arguments are never sorted**.

`args[0]`, `args[1]`, ... retain their exact semantic order because operators
may be non-commutative.

The binary argument pool is emitted by walking expressions in canonical EXPR
order and then appending each expression's arguments in source semantic order.

## 3. EXPR payload header

`EXPR` begins with a 24-byte header:

```text
u32 expression_count
u32 expression_record_size
u32 argument_count
u32 argument_record_size
u32 expressions_offset
u32 arguments_offset
```

For phase 3:

```text
expression_record_size = 32
argument_record_size   = 24
expressions_offset     = 24
arguments_offset       = 24 + expression_count * 32
```

The section-directory `item_count` MUST equal `expression_count`.

The payload MUST end exactly after:

```text
arguments_offset + argument_count * 24
```

No hidden trailer or implementation-specific data is allowed.

## 4. Expression record

Each expression record is 32 bytes:

```text
u32 id_str
u32 op_str
u32 result_type_str
u32 arg_start
u32 arg_count
u32 flags
u32 reserved0
u32 reserved1
```

Rules:

- `id_str` references the stable semantic expression ID in `STRS`;
- `op_str` references the qualified operator name;
- `result_type_str` is a `STRS` index or `0xFFFFFFFF` when absent;
- `arg_start` and `arg_count` select a contiguous slice of the argument pool;
- all argument slices MUST be canonical, contiguous and non-overlapping;
- all argument records MUST be owned by exactly one expression;
- `flags`, `reserved0`, and `reserved1` MUST be zero in phase 3.

`metadata` and `extensions` are not represented by this semantic phase.

## 5. Argument record

Each argument record is 24 bytes:

```text
u8  kind
u8  literal_kind
u8  entity_kind
u8  flags
u32 payload_a
u32 payload_aux
u64 payload_b
u32 reserved
```

`flags`, `payload_aux`, and `reserved` MUST be zero in phase 3.

Argument `kind` values:

```text
1  LITERAL
2  VARIABLE
3  EXPRESSION
4  ENTITY
```

## 6. Literal arguments

Phase 3 standardizes scalar literals only:

```text
1  NULL
2  BOOL
3  INT64
4  FLOAT64
5  STRING
```

`literal_kind = 0` means "not a literal" and is valid only for non-literal
argument records.

### NULL

```text
payload_a = 0
payload_b = 0
```

### BOOL

```text
payload_a = 0
payload_b = 0 or 1
```

### INT64

```text
payload_a = 0
payload_b = signed int64 two's-complement bits
```

Integers outside signed 64-bit range are rejected.

### FLOAT64

```text
payload_a = 0
payload_b = IEEE-754 binary64 bits
```

Only finite values are accepted.

### STRING

```text
payload_a = STRS index
payload_b = 0
```

String literals are interned in the same canonical `STRS` pool.

The generic Routing IR permits broader JSON literal values. Arrays, objects, or
other literal forms whose physical encoding has not been standardized MUST be
rejected rather than stringified or serialized using an invented contract.

## 7. Variable references

A semantic argument:

```json
{
  "kind": "variable",
  "ref": "sdhq:var:session:001"
}
```

is encoded as:

```text
kind         = VARIABLE
literal_kind = 0
entity_kind  = 0
payload_a    = canonical VARS table index
payload_b    = 0
```

The variable index is derived from the same raw-UTF-8 stable-ID ordering already
used by `VARS`.

The encoder MUST reject an unknown variable ID.

The decoder resolves the index back to the stable Variable ID.

## 8. Expression references

A semantic argument:

```json
{
  "kind": "expression",
  "ref": "sdhq:expr:condition:10"
}
```

is encoded as:

```text
kind         = EXPRESSION
literal_kind = 0
entity_kind  = 0
payload_a    = canonical EXPR table index
payload_b    = 0
```

Because the complete EXPR index map is established before argument emission,
forward references are valid.

Unknown references are rejected.

Expression-reference cycles are rejected. The binary representation therefore
remains a DAG just like the frozen semantic contract requires.

## 9. Entity references

Phase 3 allows generic `valueRef.kind = "entity"` only for entity catalogs whose
physical identity was already frozen by phase 2.

Supported semantic entity names are:

```text
resource  -> RSRC
hook      -> HOOK
ending    -> ENDG
```

Entity-kind codes:

```text
1  RESOURCE
2  HOOK
3  ENDING
```

Encoding:

```text
kind         = ENTITY
literal_kind = 0
entity_kind  = catalog kind
payload_a    = canonical catalog table index
payload_b    = 0
```

The encoder validates that the referenced stable ID exists in the selected
catalog.

The decoder restores:

```json
{
  "kind": "entity",
  "entity": "ending",
  "ref": "stable-ending-id"
}
```

Entity kinds whose table layout has not yet been standardized, such as `node`,
are rejected in phase 3 rather than assigned a provisional encoding.

## 10. String collection

Phase 3 adds to `STRS`:

- every Expression stable ID;
- every operator qualified name;
- every present `result_type`;
- every string literal.

Variable/expression/entity reference IDs do not need duplicate storage in
argument records because they lower to canonical table indices. Their stable IDs
remain present in the target tables.

## 11. Cross-reference validation

The phase-3 compiler rejects:

- duplicate Expression IDs;
- unknown Variable references;
- unknown Expression references;
- unknown supported-entity IDs;
- unsupported entity kinds;
- expression cycles;
- unsupported literal forms;
- signed-int64 overflow;
- non-finite float literals.

This validation is in addition to the generic Routing IR schema and semantic
validator.

The binary compiler still does not redefine operator arity or type rules; those
remain governed by the frozen semantic contract and semantic validator.

## 12. School Days HQ acceptance target

For the frozen School Days HQ oracle the phase-3 artifact must contain:

```text
META = 1
NSPC = 1
FEAT = 8
ENTR = 1
VARS = 497
RSRC = 1855
HOOK = 3
ENDG = 22
EXPR = 1892
```

The frozen exporter currently represents its routing predicates with:

- Variable references;
- Expression references used by lowering helpers;
- integer literals;
- boolean literals;
- qualified core operators such as comparison and boolean operations.

No School Days-specific operator or ID is hard-coded into the EXPR physical
layout.

## 13. Round-trip acceptance rule

Phase 3 is accepted only when:

```text
Canonical Routing IR
        |
        v
phase-3 encoder
        |
        v
META/STRS/NSPC/FEAT/ENTR/VARS/RSRC/HOOK/ENDG/EXPR
        |
        v
phase-3 decoder
        |
        v
canonical phase-3 semantic projection
```

and:

```text
decoded projection == canonical phase-3 projection
```

Compilation must also remain byte-identical when only declaration-array order is
changed.

Argument order is excluded from that reordering rule because argument position
is semantic.

## 14. What this phase enables

After phase 3 the binary format has canonical index spaces for:

```text
VARS
RSRC
HOOK
ENDG
EXPR
```

This is sufficient to define the next table:

```text
EFFT
```

without storing repeated stable-ID strings in every effect argument.

The intended progression remains:

```text
EXPR -> EFFT -> CHOI -> NODE -> TRAN
```

`ENTR.node` deliberately remains a stable semantic Node ID through `STRS` until
the `NODE` table itself is standardized.
