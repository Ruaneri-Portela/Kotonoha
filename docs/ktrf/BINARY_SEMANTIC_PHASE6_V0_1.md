# KTRF Binary v0.1 — Semantic Tables Phase 6 / NODE

Status: **implementation draft / round-trip gate**

Phase 6 extends the accepted phase-5 binary mapping with:

```text
NODE  canonical node catalog
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
CHOI
NODE
```

`TRAN` remains deferred. A phase-6 artifact is therefore still not the final
complete routable `.ktnroute`.

## Canonical NODE indices

Nodes are sorted by raw UTF-8 bytes of stable semantic `id`.

The zero-based position after sorting is the canonical physical `NODE` index.
Every Node record still stores its stable ID through `STRS`.

Source JSON order does not affect the binary.

## NODE payload

The section begins with a 24-byte header:

```text
u32 node_count
u32 node_record_size
u32 resource_ref_count
u32 resource_ref_record_size
u32 nodes_offset
u32 resources_offset
```

For phase 6:

```text
node_record_size         = 32
resource_ref_record_size = 4
nodes_offset             = 24
resources_offset         = 24 + node_count * 32
```

The section-directory `item_count` must equal `node_count`.

## Node record

Each Node record is 32 bytes:

```text
u32 id_str
u32 kind_str
u32 label_str
u32 resource_start
u32 resource_count
u32 flags
u32 reserved0
u32 reserved1
```

`label_str` is `0xFFFFFFFF` when `label` is absent.

`flags`, `reserved0` and `reserved1` are zero in v0.1.

`metadata` and `extensions` remain outside this semantic binary projection.

## Resource references

Each resource reference is one `u32` containing the canonical `RSRC` table
index.

Node `resources` order is semantic and is preserved exactly. The resource-ref
pool is emitted by walking Nodes in canonical NODE order and each Node's
resource list in source semantic order.

Unknown ResourceLocator IDs are rejected.

## ENTR.node lowering

Before phase 6, the second `ENTR` record field referenced the Node stable ID
through `STRS`.

With NODE standardized, phase 6 redefines that field as:

```text
u32 node_index  -> NODE[index]
```

The record remains 12 bytes:

```text
u32 id_str
u32 node_index
u32 trigger_str
```

The decoder restores the stable Node ID.

Unknown entry-point Node references are rejected.

## CHOI.node lowering

Before phase 6, `CHOI` stored `Choice.node` through `STRS`.

Phase 6 keeps the existing CHOI record width and replaces that field with the
canonical `NODE` table index.

The decoder resolves it back to the stable Node ID before producing Canonical
Routing IR.

Option order, timeout semantics, and ordered Effect references are unchanged.

## Determinism rules

```text
Node catalog order        canonical raw-UTF8 stable-ID order
Node resource order       preserved
EntryPoint catalog order  unchanged from phase 1
Choice catalog order      unchanged from phase 5
Choice option order       preserved
Choice effect order       preserved
```

Reordering source `nodes` without changing semantics must produce identical
binary output.

## Phase-6 validation

The compiler rejects:

- duplicate Node IDs;
- unknown Node resource references;
- unknown EntryPoint Node references;
- unknown Choice Node references;
- malformed NODE header/counts/offsets;
- non-canonical NODE record order;
- non-contiguous resource slices;
- invalid physical `RSRC` indices;
- invalid physical `NODE` indices in ENTR or CHOI;
- non-zero phase-6 reserved fields.

## Frozen School Days HQ gate

The frozen oracle must compile and decode with:

```text
VARS = 497
RSRC = 1855
HOOK = 3
ENDG = 22
EXPR = 1892
EFFT = 11356
CHOI = 287
NODE = 1857
```

The two routing-only School Days nodes have empty resource lists. The remaining
1855 nodes reference their corresponding `RSRC` entries.

A successful local gate ends with:

```text
PHASE6 NODE SEMANTIC BINARY PASS

=========================================================
 KTRF BINARY SEMANTIC PHASE 6 / NODE v0.1: PASS
=========================================================
```

After this phase, the only remaining core semantic section is `TRAN`.
