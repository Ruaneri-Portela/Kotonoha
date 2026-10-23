# KTRF Binary v0.1 — Semantic Tables Phase 1

Status: **implementation draft / round-trip gate**

This phase is the first mapping from the frozen KTRF Routing IR semantics into physical `.ktnroute` records. It builds on `BINARY_FORMAT_V0_1.md` and standardizes only:

```text
META  document/profile identity
STRS  canonical UTF-8 string pool
NSPC  namespace declarations
FEAT  required/optional features
ENTR  entry points
VARS  typed variables and defaults
```

It does **not** yet encode the routing graph (`RSRC`, `HOOK`, `ENDG`, `EXPR`, `EFFT`, `CHOI`, `NODE`, `TRAN`). A file produced by the phase-1 compiler is therefore a conformance artifact, not yet a complete routable KTRF document.

The semantic source remains the freeze tag:

```text
ktrf-semantic-v0.1-sdhq
```

## 1. Canonical record-order rule

Binary record indices must not depend on JSON traversal order.

Phase-1 canonical writers sort:

- `NSPC` by raw UTF-8 bytes of `prefix`;
- `FEAT` by requirement class (`required` before `optional`), then raw UTF-8 `id`, then raw UTF-8 `version`;
- `ENTR` by raw UTF-8 bytes of `id`;
- `VARS` by raw UTF-8 bytes of `id`.

All textual fields are references into `STRS` using `u32` string indices.

## 2. META

`META` contains exactly one 32-byte record.

```text
u32 format_str
u32 ir_version_str
u32 document_id_str
u32 profile_id_str
u32 profile_version_str
u32 reserved0
u32 reserved1
u32 reserved2
```

Rules:

- the section directory `item_count` MUST be `1`;
- all reserved fields MUST be zero;
- profile metadata is not encoded in phase 1;
- binary format version in the file header and semantic IR version in `META` are independent version domains.

## 3. NSPC

Each namespace record is 8 bytes:

```text
u32 prefix_str
u32 uri_str
```

The section directory `item_count` is the number of namespace records.

## 4. FEAT

Each feature record is 12 bytes:

```text
u32 id_str
u32 version_str
u32 flags
```

Feature flags:

```text
0x00000001 REQUIRED
0x00000002 OPTIONAL
```

Exactly one of these bits MUST be present in every v0.1 record.

The binary representation preserves the semantic distinction that an unsupported required feature is a load failure while an optional feature may only be ignored according to its contract.

## 5. ENTR

Each EntryPoint record is 12 bytes:

```text
u32 id_str
u32 node_id_str
u32 trigger_str
```

`node_id_str` remains a stable semantic Node ID string during phase 1. It is **not** prematurely converted to a `NODE` table index because the `NODE` layout has not been frozen yet.

When the graph-table phase is standardized, the compiler may lower this reference to a table index only if round-trip decoding preserves the same stable semantic identity.

## 6. VARS

`VARS` begins with a 16-byte payload header:

```text
u32 variable_count
u32 record_size
u32 blob_offset
u32 blob_size
```

For v0.1 phase 1:

```text
record_size = 32
blob_offset = 16 + variable_count * 32
```

The section directory `item_count` MUST equal `variable_count`.

Each Variable record is 32 bytes:

```text
u32 id_str
u32 type_str
u32 scope_str
u8  default_kind
u8  flags
u16 reserved
u32 payload_a
u32 payload_b
u64 payload_c
```

`flags` and `reserved` MUST be zero in phase 1.

### 6.1 Core default kinds

```text
1  bool
2  int32
3  uint32
4  float32
5  float64
6  string
7  bytes
```

The `default_kind` MUST agree with the semantic `type_str` for core KTRF types.

A phase-1 compiler MUST reject a profile-defined/custom Variable type whose binary default contract has not been standardized. It must not guess an encoding.

### 6.2 Scalar encodings

`ktrf:bool`:

```text
payload_a = 0
payload_b = 0
payload_c = 0 or 1
```

`ktrf:int32`:

```text
payload_a = 0
payload_b = 0
payload_c low 32 bits = two's-complement int32 bits
payload_c high 32 bits = 0
```

`ktrf:uint32`:

```text
payload_a = 0
payload_b = 0
payload_c low 32 bits = value
payload_c high 32 bits = 0
```

`ktrf:float32`:

```text
payload_a = 0
payload_b = 0
payload_c low 32 bits = IEEE-754 binary32 bits
payload_c high 32 bits = 0
```

`ktrf:float64`:

```text
payload_a = 0
payload_b = 0
payload_c = IEEE-754 binary64 bits
```

### 6.3 String defaults

`ktrf:string`:

```text
payload_a = STRS index
payload_b = 0
payload_c = 0
```

The default string is interned into the same canonical `STRS` pool used by all other phase-1 textual values.

### 6.4 Bytes defaults

`ktrf:bytes` values are base64 strings in JSON IR but raw bytes in the binary.

```text
payload_a = offset relative to start of VARS blob
payload_b = byte length
payload_c = 0
```

The Variable records are followed immediately by the `VARS` raw-byte blob. Canonical input requires canonical RFC 4648 base64 text before decoding.

The bytes blob is a storage detail; decoding reconstructs the equivalent base64 semantic value.

## 7. Phase-1 string collection

Before `STRS` is emitted, the compiler collects all strings referenced by these tables:

- IR format/version/document ID;
- profile ID/version;
- namespace prefix/URI;
- feature ID/version;
- EntryPoint ID/Node ID/trigger;
- Variable ID/type/scope;
- `ktrf:string` default values.

`STRS` then applies its already-standardized deterministic interning/sorting rule.

## 8. Round-trip acceptance rule

Phase 1 is accepted only if:

```text
Canonical Routing IR
        |
        v
phase-1 encoder
        |
        v
META/STRS/NSPC/FEAT/ENTR/VARS
        |
        v
phase-1 decoder
        |
        v
canonical phase-1 semantic projection
```

matches the same projection taken directly from the source IR.

Metadata and graph collections that phase 1 does not encode are excluded from this comparison rather than silently dropped from a claim of full semantic equivalence.

## 9. Reference implementation

```text
tools/ktrf/binary_semantic_phase1_v0_1.py
tools/ktrf/compile_phase1_v0_1.py
tests/ktrf/test_binary_semantic_phase1_v0_1.py
tools/ktrf/run_binary_semantic_phase1_v0_1.ps1
```

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\ktrf\run_binary_semantic_phase1_v0_1.ps1
```

The runner regenerates the frozen School Days canonical IR, compiles the phase-1 artifact, decodes it again, and fails on any phase-1 semantic mismatch.

## 10. Next binary phase

After this gate passes, the next record-layout group is:

```text
RSRC
HOOK
ENDG
```

These are deliberately scheduled before `EXPR/EFFT/CHOI/NODE/TRAN` because they are stable referenced entity catalogs and establish the binary reference/index policy needed by the graph tables.
