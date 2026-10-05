# KTRF Binary v0.1 — Final Full Core Gate

Status: **production-candidate core gate**

The seven semantic binary phases are now composed into one canonical core
artifact:

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
```

`EXTN` and `DBUG` remain optional container sections and are not emitted by the
v0.1 full-core compiler.

This is no longer a new semantic table phase. It is the acceptance gate for the
complete routable core before implementing a native runtime reader.

## 1. Production artifact names

For the frozen School Days HQ oracle the gate emits:

```text
build/ktrf/school-days-hq.ktnroute
build/ktrf/school-days-hq.ktnroute.manifest.json
```

The `.ktnroute` name deliberately drops the intermediate `phaseN` suffix.

## 2. Final compiler

```text
tools/ktrf/compile_full_v0_1.py
```

Compilation requires all of the following:

1. two independent builds from the same Routing IR are byte-identical;
2. decoded semantics equal the canonical Phase-7 projection;
3. the complete artifact passes the independent full validator;
4. decoding and rebuilding the artifact produces the exact original bytes;
5. semantic item counts agree with section-directory item counts;
6. generic container validation accepts SHA-256, CRC32, bounds, flags,
   alignment and canonical section order.

If any condition fails, no successful full-core result is reported.

## 3. Independent validator

```text
tools/ktrf/validate_full_v0_1.py
```

The validator consumes an already-produced `.ktnroute`; it does not need the
source Routing IR.

It validates:

- the generic KTRF v0.1 envelope;
- the exact required core section set/order;
- every Phase-1..7 semantic decoder;
- all physical table cross-references;
- canonical catalog ordering;
- contiguous ordered pools;
- terminal/destination constraints;
- canonical decode -> encode byte identity;
- optional expected section counts;
- the external build manifest when supplied.

Example:

```powershell
python .\tools\ktrf\validate_full_v0_1.py `
  --input .\build\ktrf\school-days-hq.ktnroute `
  --manifest .\build\ktrf\school-days-hq.ktnroute.manifest.json `
  --expect TRAN=2458
```

## 4. Manifest

The manifest is JSON and records:

- full-file byte length;
- full-file SHA-256;
- KTRF content SHA-256 from the binary header;
- exact section order;
- item counts;
- per-section flags;
- offsets;
- stored/decoded sizes;
- alignment;
- CRC32;
- source Routing IR filename and SHA-256.

This gives native readers and release tooling an external reproducibility target
without making the manifest part of the binary semantics.

## 5. Frozen School Days HQ acceptance counts

The final gate requires:

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
CHOI = 287
NODE = 1857
TRAN = 2458
```

`STRS` is intentionally not frozen to a handwritten constant. Its count is
derived deterministically from the represented semantics and is still recorded
in the manifest and validated by the container/string decoder.

## 6. One-command final gate

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_binary_full_v0_1.ps1
```

The gate runs:

```text
[1/5] Python syntax
[2/5] Container + Phase-1..7 + final-core regression tests
[3/5] Fresh frozen School Days oracle -> canonical IR
[4/5] Compile production-candidate .ktnroute + manifest
[5/5] Independent artifact validation + frozen oracle counts
```

Success banner:

```text
=========================================================
 KTRF BINARY v0.1 FULL CORE: PASS
=========================================================
```

## 7. Boundary after this gate

A PASS closes the Python reference serialization contract for the required v0.1
routing core.

The next engineering step is not another semantic table. It is a native
read-only runtime implementation that follows this already-frozen layout.

For the Android port, that reader should initially:

1. parse and authenticate the KTRF header and section directory;
2. expose `STRS`, `VARS`, `RSRC`, `HOOK`, `ENDG`;
3. expose indexed `EXPR`, `EFFT`, `CHOI`, `NODE`, `TRAN` views without copying
   semantic identity into ad-hoc runtime formats;
4. enforce the same `NULL_INDEX`, bounds and flag rules as the reference
   decoder;
5. resolve `ENTR` to the first `NODE`;
6. only after binary parsing is proven equivalent, execute routing semantics.

The Python reference remains the oracle for binary conformance while the native
reader is developed.
