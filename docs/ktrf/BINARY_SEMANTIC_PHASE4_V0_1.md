# KTRF Binary v0.1 — Semantic Tables Phase 4 / EFFT

Status: **implementation draft / round-trip gate**

Phase 4 extends the accepted phase-3 mapping with:

```text
EFFT  canonical Effect catalog
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
EFFT
```

`CHOI`, `NODE` and `TRAN` remain deferred. A phase-4 artifact is therefore still
not a complete routable `.ktnroute`.

The frozen Canonical Routing IR remains authoritative. Binary Effect records are
only a deterministic physical lowering of that semantic model.

## 1. Canonical EFFT indices

Effects are sorted by raw UTF-8 bytes of stable semantic `id`.

The zero-based record position after sorting is the canonical `EFFT` table
index. Source JSON declaration order does not affect the index.

Each record still stores its stable semantic ID through `STRS`, so later tables
may reference compact `EFFT[index]` values while decoding restores stable IDs.

This is distinct from **Effect execution order**. Execution order is carried by
ordered Effect-reference lists in `CHOI` and `TRAN`, which are standardized in
later phases. The EFFT catalog itself is identity storage, not execution order.

## 2. Core Effect contracts frozen by phase 4

Phase 4 standardizes these core operators:

```text
ktrf:set
ktrf:add
ktrf:copy
ktrf:register-ending
ktrf:call-hook
```

Unknown operators are rejected rather than assigned a guessed layout.

For every supported operator, the `args` object must contain exactly the fields
defined below. Extra keys are rejected so binary compilation never silently
drops semantic data.

### ktrf:set

```json
{
  "target": "stable-variable-id",
  "value": { "kind": "literal", "value": 1 }
}
```

### ktrf:add

Same physical contract as `ktrf:set`:

```json
{
  "target": "stable-variable-id",
  "value": { "kind": "variable", "ref": "stable-variable-id" }
}
```

### ktrf:copy

```json
{
  "target": "stable-variable-id",
  "source": "stable-variable-id"
}
```

### ktrf:register-ending

```json
{
  "ending": "stable-ending-id"
}
```

### ktrf:call-hook

```json
{
  "hook": "stable-hook-id",
  "arguments": []
}
```

Hook argument order is semantic and MUST be preserved exactly.

## 3. EFFT payload header

`EFFT` starts with a 24-byte header:

```text
u32 effect_count
u32 effect_record_size
u32 value_count
u32 value_record_size
u32 effects_offset
u32 values_offset
```

For phase 4:

```text
effect_record_size = 32
value_record_size  = 24
effects_offset     = 24
values_offset      = 24 + effect_count * 32
```

The section-directory `item_count` MUST equal `effect_count`.

The payload ends exactly after:

```text
values_offset + value_count * 24
```

## 4. Effect record

Each Effect record is exactly 32 bytes:

```text
u32 id_str
u32 op_str
u8  shape
u8  flags
u16 reserved
u32 payload_a
u32 payload_b
u32 value_start
u32 value_count
u32 reserved2
```

`flags`, `reserved`, and `reserved2` MUST be zero in phase 4.

Shape values:

```text
1  SET
2  ADD
3  COPY
4  REGISTER_ENDING
5  CALL_HOOK
```

The textual `op_str` and numeric `shape` must agree. A mismatch is malformed.

Value slices are contiguous, non-overlapping, canonical, and together own every
record in the EFFT value pool.

## 5. Value pool

The EFFT value pool deliberately reuses the exact 24-byte valueRef record format
standardized by `EXPR` phase 3.

Therefore Effect values and hook arguments can use the already-frozen scalar and
reference forms:

```text
literal
variable
expression
entity(resource|hook|ending)
```

No second incompatible value encoding is introduced.

References lower to the already-frozen canonical indices:

```text
VARIABLE    -> VARS[index]
EXPRESSION  -> EXPR[index]
RESOURCE    -> RSRC[index]
HOOK        -> HOOK[index]
ENDING      -> ENDG[index]
```

Decoding resolves every index back to the stable semantic ID.

## 6. Operator-specific physical lowering

### SET / ADD

```text
payload_a   = target VARS index
payload_b   = 0
value_count = 1
```

The one value-pool record is the semantic `value`.

### COPY

```text
payload_a   = target VARS index
payload_b   = source VARS index
value_count = 0
```

### REGISTER_ENDING

```text
payload_a   = ENDG index
payload_b   = 0
value_count = 0
```

### CALL_HOOK

```text
payload_a   = HOOK index
payload_b   = 0
value_count = len(arguments)
```

The value-pool slice preserves hook argument order exactly.

## 7. Validation

The phase-4 compiler rejects at least:

- duplicate Effect IDs;
- unsupported Effect operators;
- missing or extra operator argument fields;
- unknown target/source variables;
- unknown hooks;
- unknown endings;
- invalid valueRef forms;
- unknown Variable/Expression/entity references inside valueRefs;
- unsupported literal forms inherited from phase 3;
- malformed EFFT shape/op pairs;
- non-canonical or overlapping value slices;
- unowned value records.

`metadata` and `extensions` remain outside the phase-4 semantic projection.

## 8. School Days HQ expectation

For the frozen School Days HQ oracle, phase 4 must preserve:

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
EFFT = 11356
```

The School Days exporter currently lowers its effects into the five supported
core operators above, including source effects, feeling deltas, the explicit
choice-result reset, and synthetic ROUTE/SCENE mirror effects.

No School Days-specific Effect IDs or counts are encoded in the binary layout.

## 9. Round-trip acceptance rule

Phase 4 is accepted only if:

```text
Canonical Routing IR
        |
        v
phase-4 encoder
        |
        v
.../EXPR/EFFT
        |
        v
phase-4 decoder
        |
        v
canonical phase-4 semantic projection
```

The decoded projection MUST equal the canonical phase-4 projection.

Repeated compilation of semantically identical input whose declaration arrays
have different source order MUST produce byte-identical output.

## 10. What this unlocks

Once EFFT is accepted, later tables may use canonical `u32` Effect indices.

The next phase is naturally:

```text
CHOI
```

`CHOI` can then preserve ordered option/timeout Effect lists using `EFFT[index]`
without duplicating Effect bodies or making semantic identity depend on source
array order.
