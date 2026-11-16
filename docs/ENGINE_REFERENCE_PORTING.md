# Engine compatibility reference implementation (2026-11)

This document tracks **incremental** work based on four independent interpreters/tools. A format parser passing a synthetic fixture does not establish 1:1 game compatibility.

## Scope and upstream licensing

| Reference | Kotonoha component | Upstream license | Integration rule |
| --- | --- | --- | --- |
| [rlvm](https://github.com/eglaysher/rlvm) | RealLive VM in `modules/vms/reallive` | GPL-3.0-or-later for rlvm overall | Compare behavior, **do not import GPL code** into MIT engine modules. The vendored **libreallive** subset already includes its own MIT headers; preserve those notices and inspect each file. |
| [NitroSharp](https://github.com/CommitteeOfZero/nitrosharp) | New `modules/vms/n2system` | MIT for NitroSharp; third-party terms separate | NsScript script-indexing implementation written for Kotonoha C++17, not copied C#/.NET runtime. |
| [LuckSystem](https://github.com/wetor/LuckSystem) | LucaSystem bytecode reader under `games/luca-system` | MIT | Cross-check binary instruction length/alignment and AIR text conventions. |
| [Kirikiroid2Yuri](https://github.com/YuriSizuku/Kirikiroid2Yuri) | Kirikiri KAG under `modules/vms/kag` | Multi-component BSD-style / permissive terms and bundled third-party notices | Cross-check KAG and XP3 behavior. Do not vendor the Kirikiroid engine or dependencies without an asset-by-asset license audit. |

## Changes in this iteration

- **RealLive:** Validate jump destinations before pushing a return frame. Malformed jump targets can no longer corrupt the VM call stack.
- **LucaSystem:** Keep all payload bytes in odd-length instructions, parse the alignment byte *after* the unpadded instruction, reject truncated instructions and fixed parameters, and decode AIR English dialogue when the Japanese field is empty.
- **Kirikiri:** Parse `@command arg=value` lines using the normal tag grammar; ignore leading-semicolon comment lines; decode `[[` as a literal opening bracket in text.
- **N2System:** Add a standalone C++17 NsScript chapter/label source index with UTF-8 BOM handling, comments, nested blocks, quoted literals, malformed-input errors and automated tests. **This is not yet an executable N2System VM**, NPA archive reader or Nitroplus game port.

## Next compatibility milestones

1. **RealLive:** Test conditional branches, local/global memory, save compatibility, KOE/OVK/NWK audio and rlBabel against legally obtained original games.
2. **N2System:** Add NPA/AFS asset providers, typed NsScript lexer/AST and expressions, opcode execution, transitions, save states and media callbacks. Keep it C++17 and independent of the .NET runtime.
3. **LucaSystem:** Introduce per-title opcode metadata, branch relocation and script import/export round trips; extend CZ decoder and voice archives using documented samples.
4. **Kirikiri:** Add TJS2 expression/runtime support, KAG `call`/`return` stack and conditionals, additional XP3 variants and filter/plugin APIs.

## Validation

Configure the repository with the relevant ports enabled and CTest:

```sh
cmake -S . -B build -DKOTONOHA_BUILD_REALLIVE=ON -DKOTONOHA_BUILD_LUCA_SYSTEM=ON -DKOTONOHA_BUILD_KIRIKIRI=ON -DBUILD_TESTING=ON
cmake --build build --target KotonohaN2SystemScriptTests LucaSystemFormatTests KirikiriXp3Tests RealLiveScriptTests
ctest --test-dir build --output-on-failure -R 'N2System|LucaSystem|KirikiriXp3|RealLiveScript'
```

RealLive's deeper Session test requires legally obtained SEEN.TXT data and may skip in the public CI. Runtime parity is not asserted unless tests are repeated against original assets and binaries.

The build must not include third-party proprietary game assets in releases. Distributing MIT source does not override the upstream GPL conditions on unrelated rlvm files or third-party copyright in commercial games.
