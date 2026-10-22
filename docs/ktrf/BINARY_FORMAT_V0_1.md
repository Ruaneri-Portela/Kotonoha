# KTRF Binary Format v0.1

Status: **Binary container draft v0.1**

This document starts the physical `.ktnroute` format after the semantic contract was frozen by tag `ktrf-semantic-v0.1-sdhq`.

The binary format is a serialization of the frozen KTRF semantic model. It MUST NOT redefine routing semantics.

## 1. Scope of this phase

This first binary phase freezes only the generic container envelope:

- file identity and version;
- byte order;
- fixed header layout;
- section directory layout;
- section alignment and padding;
- whole-content SHA-256;
- header and per-section CRC32;
- required/optional section handling;
- deterministic section ordering;
- UTF-8 string-pool envelope.

Entity record layouts for `NODE`, `TRAN`, `EXPR`, `EFFT`, `CHOI`, and related sections are deliberately specified in later phases. The container can therefore be tested independently of semantic record packing.

## 2. Primitive encoding rules

KTRF binary v0.1 uses:

- byte order: **little-endian only**;
- unsigned integers: fixed width (`u8`, `u16`, `u32`, `u64`);
- signed integers: two's-complement fixed width when a record explicitly declares them;
- strings: UTF-8, never locale-dependent;
- default section alignment: 8 bytes;
- all alignment padding bytes: zero;
- null index sentinel: `0xFFFFFFFF` for future `u32` table references.

A v0.1 reader MUST reject a file that declares an unsupported byte-order code rather than guessing.

## 3. File header

The fixed v0.1 header is exactly **96 bytes**.

| Offset | Size | Field | v0.1 meaning |
|---:|---:|---|---|
| `0x00` | 4 | `magic` | ASCII `KTRF` |
| `0x04` | 2 | `format_major` | `0` |
| `0x06` | 2 | `format_minor` | `1` |
| `0x08` | 2 | `header_size` | `96` |
| `0x0A` | 2 | `section_entry_size` | `48` |
| `0x0C` | 4 | `flags` | `0` in v0.1 |
| `0x10` | 4 | `section_count` | number of directory entries |
| `0x14` | 4 | `reserved0` | zero |
| `0x18` | 8 | `section_directory_offset` | `96` in canonical v0.1 |
| `0x20` | 8 | `file_size` | exact total file size |
| `0x28` | 1 | `byte_order` | `1` = little-endian |
| `0x29` | 1 | `hash_algorithm` | `1` = SHA-256 |
| `0x2A` | 2 | `reserved1` | zero |
| `0x2C` | 4 | `header_crc32` | CRC32 of the 96-byte header with this field zeroed |
| `0x30` | 32 | `content_sha256` | SHA-256 of bytes `[header_size, file_size)` |
| `0x50` | 16 | `reserved2` | all zero |

### 3.1 Header CRC32

To calculate `header_crc32`:

1. construct the final 96-byte header, including `content_sha256`;
2. write zero to bytes `0x2C..0x2F`;
3. compute IEEE CRC-32 over all 96 bytes;
4. store the result little-endian at `0x2C`.

### 3.2 Content SHA-256

`content_sha256` covers every byte after the fixed header through `file_size`, including:

- the section directory;
- zero alignment padding;
- every stored section payload;
- zero padding between sections.

The header is excluded so the hash does not recursively include itself. The header CRC32 protects the header, including the stored SHA-256 value.

## 4. Section directory

The section directory starts at offset `96` and contains `section_count` entries. Each v0.1 entry is exactly **48 bytes**.

| Relative offset | Size | Field | Meaning |
|---:|---:|---|---|
| `0x00` | 4 | `type` | ASCII FourCC |
| `0x04` | 4 | `flags` | section flags |
| `0x08` | 8 | `offset` | absolute payload offset |
| `0x10` | 8 | `stored_size` | bytes physically stored |
| `0x18` | 8 | `decoded_size` | logical bytes after decoding |
| `0x20` | 4 | `item_count` | record count, or `0` if not applicable |
| `0x24` | 4 | `alignment` | required power-of-two alignment |
| `0x28` | 4 | `crc32` | IEEE CRC-32 of the stored payload bytes |
| `0x2C` | 4 | `reserved` | zero |

A section payload MUST lie completely inside the file and MUST NOT overlap another section payload or the section directory.

## 5. Section flags

v0.1 defines:

```text
0x00000001  REQUIRED
0x00000002  COMPRESSED
0x00000004  NON_SEMANTIC
```

Rules:

- an unknown section with `REQUIRED` set MUST cause rejection;
- an unknown optional section MAY be skipped only if no required feature depends on it;
- `COMPRESSED` is reserved by the v0.1 container but no compression codec is standardized yet, therefore v0.1 readers MUST reject a section with this bit set;
- `NON_SEMANTIC` identifies diagnostic/debug payload that cannot affect decoded routing semantics;
- unknown section-flag bits MUST cause rejection in v0.1.

## 6. Canonical section types and order

The canonical v0.1 ordering is:

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
TRAN
EXTN
DBUG
```

Not every section is necessarily present in every document. Present known sections MUST follow this relative ordering.

Unknown extension FourCCs, when optional and semantically safe, sort after known sections by raw four-byte value.

A canonical writer MUST NOT emit duplicate FourCC entries in v0.1.

## 7. Placement and alignment

Canonical placement is deterministic:

1. fixed 96-byte header;
2. contiguous section directory (`section_count * 48` bytes);
3. zero padding to the first section's alignment;
4. section payloads in canonical order;
5. zero padding before each following payload as needed;
6. no bytes after the final section other than its own stored payload.

The default alignment is 8. An entry MAY request another power-of-two alignment, but v0.1 canonical tooling limits it to at most 4096.

## 8. STRS — canonical UTF-8 string pool envelope

The first concrete payload format standardized by v0.1 is `STRS`.

`STRS` payload:

```text
u32 string_count
u32 byte_count
u32 offsets[string_count + 1]
u8  utf8_data[byte_count]
```

Rules:

- `offsets[0]` MUST be `0`;
- `offsets[string_count]` MUST equal `byte_count`;
- offsets MUST be nondecreasing;
- string `i` occupies `utf8_data[offsets[i]:offsets[i+1]]`;
- strings are not NUL-terminated;
- each string MUST be valid UTF-8;
- embedded `U+0000` is forbidden in v0.1;
- duplicate byte-identical strings MUST be interned into one entry by canonical writers;
- index `0` is reserved for the empty string;
- `0xFFFFFFFF` is the null/no-string sentinel in record fields;
- after the reserved empty string, canonical writers sort strings by raw UTF-8 byte sequence.

This makes string indices deterministic and independent of traversal order in the source JSON.

## 9. Integrity validation order

A strict v0.1 reader SHOULD validate in this order:

1. minimum size;
2. magic/version/header constants;
3. reserved header fields and supported flags;
4. `file_size`;
5. header CRC32;
6. whole-content SHA-256;
7. directory bounds and ordering;
8. section offsets/alignment/non-overlap;
9. zero padding;
10. section CRC32;
11. known-section payload structure;
12. semantic feature/profile compatibility.

No recovery-by-guessing is allowed for malformed required data.

## 10. Determinism requirements

For the same canonical Routing IR and compiler version/policy, repeated compilation MUST produce byte-identical `.ktnroute` output.

At minimum this requires:

- canonical section order;
- deterministic string interning/sorting;
- deterministic record ordering;
- zero padding;
- reserved fields zeroed;
- no timestamps, host paths, random IDs, or build-machine data in semantic sections;
- deterministic integrity fields.

## 11. Semantic compatibility rule

The semantic freeze remains authoritative:

```text
canonical Routing IR
        |
        v
binary encoder
        |
        v
*.ktnroute
        |
        v
binary decoder
        |
        v
decoded semantic model
```

The decoded model MUST be semantically equivalent to the canonical Routing IR that produced it. Future binary differential tests will execute the decoded model against the same School Days C++ oracle used by the semantic freeze.

## 12. Deferred record-layout work

The following are intentionally not frozen by this first container draft:

- `META` record body;
- namespace/feature records;
- variable/default-value encoding;
- expression DAG bytecode/table encoding;
- effect records;
- Choice/Feeling structures;
- Node and Transition records;
- extension payload contracts;
- optional compression codec identifiers.

Those layouts will be added incrementally, each with encode/decode round-trip and differential evidence before the final binary v0.1 freeze.
