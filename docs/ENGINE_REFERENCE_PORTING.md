# Engine compatibility reference implementation (2026-11)

> **Historical snapshot, not a current compatibility claim.** The notes below precede PRs #71/#76. See [current N2System status](../games/n2system/STATUS.md), [parity matrix](NITROSHARP_PORT_PARITY.md) and [license audit](LICENSING.md).

This document tracks **incremental** work based on four independent interpreters/tools. A format parser passing a synthetic fixture does not establish 1:1 game compatibility.

## Scope and upstream licensing

| Reference | Kotonoha component | Upstream license | Integration rule |
| --- | --- | --- | --- |
| [rlvm](https://github.com/eglaysher/rlvm) | RealLive VM in `modules/vms/reallive` | GPL-3.0 per public repository metadata (verify individual source notices) | Compare behavior, **do not import GPL code** into MIT engine modules. The vendored **libreallive** subset already includes its own MIT headers; preserve those notices and inspect each file. |
| [NitroSharp](https://github.com/CommitteeOfZero/nitrosharp) | New `modules/vms/n2system` | MIT for NitroSharp; third-party terms separate | NsScript script-indexing implementation written for Kotonoha C++17, not copied C#/.NET runtime. |
| [LuckSystem](https://github.com/wetor/LuckSystem) | LucaSystem bytecode reader under `games/luca-system` | MIT | Cross-check binary instruction length/alignment and AIR text conventions. |
| [Kirikiroid2Yuri](https://github.com/YuriSizuku/Kirikiroid2Yuri) | Kirikiri KAG under `modules/vms/kag` | Unclassified/mixed repository license; inspect terms file by file | Cross-check KAG and XP3 behavior. Do not vendor the Kirikiroid engine or dependencies without an asset-by-asset license audit. |

## Changes in this iteration

- **RealLive:** Validate jump destinations before pushing a return frame. Malformed jump targets can no longer corrupt the VM call stack.
- **LucaSystem:** Keep all payload bytes in odd-length instructions, parse the alignment byte *after* the unpadded instruction, reject truncated instructions and fixed parameters, and decode AIR English dialogue when the Japanese field is empty. Add lossless instruction serialization, preserving unknown opcodes and verifying synthetic/retail byte-for-byte round trips.
- **Kirikiri:** Parse `@command arg=value` lines using the normal tag grammar; ignore leading-semicolon comment lines; decode `[[` as a literal opening bracket in text. Implement local `[call target=*label]` / `[return]` with a return stack, starred label targets, and host-owned save/restore of nested call state.
- **N2System:** Add a standalone C++17 NsScript chapter/label/function-call source index with UTF-8 BOM handling, comments, nested blocks, quoted literals, source locations, malformed-input errors and automated tests. **This is not yet an executable N2System VM** or Nitroplus game port; the separate archive reader now covers AFS and limited NPA.

## Follow-up integration on dev

- **N2System AFS/NPA:** added an independent C++17 archive reader under `modules/providers/n2`, plus an `AssetProvider` adapter supporting archive mounting, case-normalized names, filename decoding for the supported NPA variant, read/list and input bounds checks. NPA compression and content encryption are **not** implemented. The module has synthetic archive tests (`KotonohaN2ArchivesTests`). This is an archive provider, **not** a playable N2System port.
- **Kirikiri:** the VM now owns its callbacks (fixes a dangling reference after scenario initialization), tracks local KAG variables, executes a bounded subset of `[if]` / `[elsif]` / `[else]` / `[endif]`, and writes V3 saves with variables and local return frames while retaining V1/V2 loading. Timed waits resume on the runtime's update thread instead of on SDL timer threads. Full TJS2 expression semantics and cross-file calls are still missing.
- **RealLive:** reports unsupported commands with scenario, PC, module type, module number, opcode and overload; the frontend logs this information so compatibility gaps are observable.
- **LucaSystem:** WAIT/DRAW/FADE instructions now advance only once, wait clocks use 64-bit ticks, and the VM yields after a per-frame instruction budget to avoid accidental endless loops. The parser can replace same-length raw instruction payloads without relocating any branch offsets; variable-length script editing remains unsupported.

Tests were **added but not executed** in this session. CI workflows remain dispatch-only.

## Next compatibility milestones

1. **RealLive:** Test conditional branches, local/global memory, save compatibility, KOE/OVK/NWK audio and rlBabel against legally obtained original games.
2. **N2System:** Add compressed/encrypted archive variants, integrate archive discovery with a playable launcher, typed NsScript lexer/AST and expressions, opcode execution, transitions, save states and media callbacks. Keep it C++17 and independent of the .NET runtime.
3. **LucaSystem:** Introduce per-title opcode metadata, branch relocation and editable script import/export (the current serializer is lossless but does not rewrite operands or offsets); extend CZ decoder and voice archives using documented samples.
4. **Kirikiri:** Add cross-script KAG call stacks, full TJS2 expression/runtime support, animations, additional XP3 variants and filter/plugin APIs.

## Validation

Configure the repository with the relevant ports enabled and CTest:

```sh
cmake -S . -B build -DKOTONOHA_BUILD_REALLIVE=ON -DKOTONOHA_BUILD_LUCA_SYSTEM=ON -DKOTONOHA_BUILD_KIRIKIRI=ON -DBUILD_TESTING=ON
cmake --build build --target KotonohaN2SystemScriptTests KotonohaN2ArchivesTests LucaSystemFormatTests KirikiriXp3Tests RealLiveScriptTests
ctest --test-dir build --output-on-failure -R 'N2System|LucaSystem|KirikiriXp3|RealLiveScript'
```

RealLive's deeper Session test requires legally obtained SEEN.TXT data and may skip in the public CI. Runtime parity is not asserted unless tests are repeated against original assets and binaries.

The build must not include third-party proprietary game assets in releases. Distributing MIT source does not override the upstream GPL conditions on unrelated rlvm files or third-party copyright in commercial games.

## Latest dev improvements (conditional flow, archive aliases and jumps)

- **KAG branching:** Correctly select the first true `elsif` branch, including nested conditionals, rather than skipping its body. Blank indentation no longer blocks dialogue; jump-only loops yield after a finite instruction budget. Active branch states and their depths during subroutine calls are preserved in **KIRIKIRI_SAVE_4**, with V1/V2/V3 still readable. These features cover only a subset of TJS2/KAG conditions.
- **N2System archives:** AFS/NPA archive entries now support seekable streaming with no extra complete asset copy. AFS name-index files can be mounted as aliases through `N2ArchiveAssets::addAfsNames`, using a transactional parser that rejects traversal paths, duplicate conflicts and invalid indexes. The standalone C++17 AFS/NPA tests no longer rely on `assert`, so Release builds exercise checks.
- **LucaSystem:** Jump targets can resolve verified byte offsets as described by LuckSystem's jump parameters. A symbolic lookup checks actual `JUMPPOINT` instructions instead of arbitrary opcode data. Subroutine return frames are only pushed after successful target resolution.
- **RealLive:** Clear pending unsupported-opcode diagnostics after a successful retry, a seek or restoration, so old reports do not masquerade as new failures.

### Test N2 archive formats without the complete game engine

```sh
cmake -S modules/providers/n2/tests -B build/n2-archives -DCMAKE_BUILD_TYPE=Release
cmake --build build/n2-archives
ctest --test-dir build/n2-archives --output-on-failure
```

Compilation and CTest results were **not available in this session**; tests are committed and require execution on a host with CMake and a C++17 compiler. The six GitHub Actions workflows remain **manual**.
