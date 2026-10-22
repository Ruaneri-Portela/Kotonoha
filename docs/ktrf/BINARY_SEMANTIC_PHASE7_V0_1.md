# KTRF Binary v0.1 — Semantic Tables Phase 7 / TRAN

Status: **implementation draft / full core graph round-trip gate**

Phase 7 extends the accepted phase-6 mapping with the final core routing table:

```text
TRAN  canonical Transition catalog
```

Represented sections are now:

```text
META STRS NSPC FEAT ENTR VARS RSRC HOOK ENDG EXPR EFFT CHOI NODE TRAN
```

`EXTN` and `DBUG` remain optional/deferred. Phase 7 is the first artifact that
physically represents the complete core routing graph.

## Canonical Transition indices

Transition records are sorted by raw UTF-8 bytes of stable semantic `id`.
Their zero-based positions are canonical `TRAN[index]` values. Stable IDs remain
stored through `STRS` so decoding reconstructs semantic identity.

Source JSON transition order is not routing precedence. Precedence is represented
explicitly by each Transition's `priority` field.

## TRAN payload

The section begins with a 32-byte header:

```text
u32 transition_count
u32 transition_record_size
u32 effect_ref_count
u32 effect_ref_record_size
u32 trigger_ref_count
u32 trigger_ref_record_size
u32 effects_offset
u32 triggers_offset
```

Phase-7 constants:

```text
transition_record_size = 48
effect_ref_record_size = 4
trigger_ref_record_size = 4
transitions_offset      = 32
effects_offset          = 32 + transition_count * 48
triggers_offset         = effects_offset + effect_ref_count * 4
```

The section-directory `item_count` MUST equal `transition_count`.

## Transition record

Each 48-byte record is:

```text
u32 id_str
u32 source_node_index
u32 destination_node_index
u32 priority
u32 predicate_expr_index
u32 effect_start
u32 effect_count
u32 trigger_start
u32 trigger_count
u32 flags
u32 reserved0
u32 reserved1
```

`destination_node_index` is `0xFFFFFFFF` for terminal transitions.
`predicate_expr_index` is `0xFFFFFFFF` when no predicate is present.

Flags:

```text
0x00000001 TERMINAL
0x00000002 HAS_TRIGGERS
```

Unknown flag bits and nonzero reserved fields are invalid.

## Physical references

Phase 7 lowers all remaining graph references:

```text
source      -> NODE[index]
destination -> NODE[index] or NULL_INDEX
predicate   -> EXPR[index] or NULL_INDEX
effects[]   -> ordered EFFT[index] pool
```

Triggers are qualified-name strings and therefore use ordered `STRS[index]`
references in a dedicated trigger pool.

## Semantic order

Transition records are canonically sorted by stable ID. The following are never
sorted by the binary compiler:

```text
Transition.effects[]
Transition.triggers[]
```

Their source semantic order is preserved exactly.

## Terminal contract

The binary compiler enforces the Routing IR invariant directly:

```text
terminal == true  -> destination MUST be absent
terminal == false -> destination MUST be present
```

A decoded terminal record with a non-null destination is invalid. A decoded
nonterminal record with a null destination is invalid.

## Priority

`priority` is represented as an unsigned 32-bit integer. Values outside
`0..4294967295` are rejected rather than truncated.

No extra ordering rule is invented by the binary layer. In particular, the
compiler does not reorder transitions by priority and does not infer priorities
from physical record order.

## Triggers

`triggers` remains optional. Absence is preserved using `HAS_TRIGGERS`.
When present it MUST contain at least one unique qualified name, matching the
Routing IR schema. Trigger order is preserved.

## Cross-reference validation

Phase 7 rejects:

- duplicate Transition stable IDs;
- unknown source or destination Nodes;
- unknown predicate Expressions;
- unknown Effects;
- terminal/destination contract violations;
- non-boolean terminal values;
- priorities outside `u32`;
- present-but-empty trigger arrays;
- duplicate triggers;
- malformed or out-of-range decoded physical indices;
- noncanonical pool ownership or offsets.

## School Days HQ gate

The frozen School Days HQ oracle is expected to compile with:

```text
VARS = 497
RSRC = 1855
HOOK = 3
ENDG = 22
EXPR = 1892
EFFT = 11356
CHOI = 287
NODE = 1857
TRAN = 2458
```

The gate regenerates the canonical IR from the frozen oracle before compilation,
then requires exact canonical semantic projection round-trip.

Expected artifact:

```text
build/ktrf/school-days-hq.phase7-tran.ktnroute
```

A successful run ends with:

```text
PHASE7 TRAN SEMANTIC BINARY PASS
KTRF BINARY SEMANTIC PHASE 7 / TRAN v0.1: PASS
```

At this point the v0.1 core routing graph is physically represented end-to-end.
`EXTN` and `DBUG` are still deferred because they are not required to execute the
frozen School Days HQ core routing semantics.
