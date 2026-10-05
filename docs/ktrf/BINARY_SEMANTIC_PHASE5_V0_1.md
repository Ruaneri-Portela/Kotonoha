# KTRF Binary v0.1 — Semantic Tables Phase 5 / CHOI

Status: **implementation draft / round-trip gate**

Phase 5 extends the accepted phase-4 mapping with the canonical Choice catalog:

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
CHOI
```

`NODE` and `TRAN` remain deferred. A phase-5 artifact is therefore not yet a
complete routable `.ktnroute`.

The frozen Canonical Routing IR remains authoritative. This phase only lowers
Choice semantics into a deterministic binary representation.

## 1. Canonical Choice indices

Choice records are sorted by raw UTF-8 bytes of stable semantic `id`.

The zero-based position after sorting is the canonical `CHOI` table index.

Source JSON Choice-array order is therefore non-semantic.

Choice Option order is semantic and MUST be preserved exactly. Effect reference
order inside each Option and timeout is also semantic and MUST be preserved.

## 2. References frozen in this phase

Phase 5 lowers:

```text
result_variable -> VARS[index]
option.effects   -> EFFT[index] list
timeout.effects  -> EFFT[index] list
```

`Choice.node` deliberately remains the stable Node ID through `STRS` because the
`NODE` table layout is not frozen yet. The decoder reconstructs the exact stable
Node ID.

`routing_policy` is stored through `STRS`; no profile-specific numeric policy is
invented.

## 3. CHOI header

`CHOI` begins with a 48-byte header:

```text
u32 choice_count
u32 choice_record_size
u32 option_count
u32 option_record_size
u32 timeout_count
u32 timeout_record_size
u32 effect_ref_count
u32 effect_ref_record_size
u32 choices_offset
u32 options_offset
u32 timeouts_offset
u32 effects_offset
```

Phase-5 constants:

```text
choice_record_size     = 32
option_record_size     = 36
timeout_record_size    = 28
effect_ref_record_size = 4
choices_offset         = 48
```

All pools are contiguous and canonical. There is no trailer.

## 4. Choice record

Each Choice record is 32 bytes:

```text
u32 id_str
u32 node_id_str
u32 result_variable_index
u32 routing_policy_str
u32 option_start
u32 option_count
u32 timeout_index
u32 reserved
```

`timeout_index` is `0xFFFFFFFF` when the Choice has no timeout.

Choice option slices are contiguous and non-overlapping. Every Option record is
owned by exactly one Choice.

## 5. Option record

Each Option record is 36 bytes:

```text
u32 id_str
u32 label_str
u8  value_kind
u8  flags
u16 reserved0
u32 payload_a
u64 payload_b
u32 effect_start
u32 effect_count
u32 reserved1
```

`label_str = 0xFFFFFFFF` when `label` is absent.

`flags`, `reserved0`, and `reserved1` MUST be zero in phase 5.

Option record order is exactly the semantic order from `Choice.options`.

Local Option IDs MUST be unique within their Choice. They are not globally
sorted or globally indexed.

## 6. Timeout record

Each present timeout has one 28-byte record:

```text
u8  value_kind
u8  flags
u16 reserved0
u32 payload_a
u64 payload_b
u32 effect_start
u32 effect_count
u32 reserved1
```

Timeout records are emitted in canonical Choice order, only for Choices that
actually contain a timeout.

Absence of timeout and a present timeout with a scalar `null` value remain
distinct states.

## 7. Choice scalar values

Choice Option values and timeout values use the same scalar kind family:

```text
1  NULL
2  BOOL
3  INT64
4  FLOAT64
5  STRING
```

Encoding:

```text
NULL:    payload_a = 0, payload_b = 0
BOOL:    payload_a = 0, payload_b = 0 or 1
INT64:   payload_a = 0, payload_b = signed int64 two's-complement bits
FLOAT64: payload_a = 0, payload_b = IEEE-754 binary64 bits
STRING:  payload_a = STRS index, payload_b = 0
```

Only finite floats are accepted. Integers outside signed 64-bit range are
rejected. Arrays/objects are rejected rather than serialized with an invented
contract.

This fully covers the frozen School Days HQ Choice domain, whose option values
are integers and whose timeout value is `-1`.

## 8. Effect-reference pool

Each effect reference is exactly one `u32` canonical EFFT index.

References are emitted in this semantic traversal order:

```text
Choice 0 option 0 effects
Choice 0 option 1 effects
...
Choice 0 timeout effects (if present)
Choice 1 option 0 effects
...
```

The decoder requires all slices to be contiguous and fully owned. It restores
stable Effect IDs from the EFFT catalog.

## 9. String collection

Phase 5 adds to `STRS`:

- every Choice stable ID;
- every Choice Node stable ID;
- every routing-policy string;
- every Option local ID;
- optional Option labels;
- string-valued Option values;
- string-valued timeout values.

`result_variable` and Effect references do not need duplicate stable-ID strings
in CHOI because they lower to already-frozen table indices.

## 10. Validation

The phase-5 compiler rejects:

- duplicate Choice IDs;
- duplicate local Option IDs within one Choice;
- empty Option lists;
- unknown Node references in the source IR;
- unknown result Variables;
- unknown Effect references;
- non-scalar Option/timeout values;
- signed-int64 overflow;
- non-finite float values.

The decoder additionally rejects malformed pool offsets, non-canonical slices,
out-of-range indices, unknown scalar kinds, non-zero reserved fields, and
unowned Option/timeout/Effect-reference records.

`metadata` and `extensions` remain outside this phase's semantic projection.

## 11. School Days HQ acceptance target

The frozen School Days HQ oracle must compile with at least:

```text
VARS = 497
RSRC = 1855
HOOK = 3
ENDG = 22
EXPR = 1892
EFFT = 11356
CHOI = 287
```

The 287 Choices preserve the exported Option order, the `ktrf:deferred` routing
policy, the internal choice-result Variable, all feeling-delta Effect ordering,
and the `-1` timeout behavior.

## 12. Round-trip gate

Phase 5 is accepted only if:

```text
Canonical Routing IR
        |
        v
phase-5 encoder
        |
        v
... / EXPR / EFFT / CHOI
        |
        v
phase-5 decoder
        |
        v
canonical phase-5 semantic projection
```

The decoded projection MUST equal the canonical phase-5 projection.

Repeated compilation after reordering only non-semantic Choice declaration arrays
MUST remain byte-identical. Option order and Effect-reference order are not
eligible for canonical reordering because they carry semantics.

After this phase the remaining core binary work is:

```text
NODE
TRAN
```
