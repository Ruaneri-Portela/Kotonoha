# KTRF Native Reader v0.1 — Gate 1

This gate establishes the dependency-free native C foundation for consuming KTRF binary v0.1 in Kotonoha on desktop and Android.

## Scope

Implemented:

- load from file or owned memory copy;
- KTRF v0.1 header validation;
- header CRC32 validation;
- content SHA-256 validation;
- canonical section-directory validation;
- section CRC32 validation;
- alignment, zero-padding, bounds and overlap validation;
- rejection of unsupported compression and unknown required sections;
- presence validation for all full-core sections through `TRAN`;
- canonical `STRS` validation and indexed string access;
- read-only section lookup without packed-struct casts or host-endian assumptions.

The reader is pure C11 and has no SDL, FFmpeg, platform, or crypto-library dependency. This is intentional so the same translation unit can be compiled by the desktop toolchain and the Android NDK.

## Public API

Header:

`include/Kotonoha/parsers/Ktrf.h`

Implementation:

`src/parsers/Ktrf.c`

Initialize a document with `Kotonoha_KtrfInit()` before first load and clean/reinitialize it before reuse. The loaded document owns its input bytes. Section payload pointers and string views remain valid until `Kotonoha_KtrfClean()`.

## Gate

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\ktrf\run_native_ktrf_reader_v0_1.ps1
```

The gate regenerates the frozen School Days IR and production `.ktnroute`, compiles a standalone native harness, then verifies:

- the full-core file is accepted natively;
- frozen School Days section counts match;
- every STRS entry is accessible;
- payload corruption is rejected by native integrity validation.

Expected final banner:

`KTRF NATIVE READER v0.1 / GATE 1: PASS`

## Not in Gate 1

Gate 1 does not yet decode semantic record layouts for `META`, `VARS`, `RSRC`, `EXPR`, `EFFT`, `CHOI`, `NODE`, or `TRAN`, and it does not execute routing. Those are layered on this validated container/string foundation in the next native-reader gate.
