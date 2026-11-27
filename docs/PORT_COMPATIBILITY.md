# Game port compatibility audit — 2026-11-16

[Português (Brasil)](PORT_COMPATIBILITY.pt-BR.md)

This matrix covers **every game application** in `buildsystems/xcode/ProjectManifest.json`.
It distinguishes repository-backed support from unverified retail behavior.
A valid archive, successful startup or green synthetic test is **not** proof
that every script command, route, transition or ending works.

| Port | Existing evidence and limits | Improvement in this change |
| --- | --- | --- |
| Example | Shared engine, scene and UI demonstration; no commercial data | Aligned the demo runtime/package ID with its bundle, retaining legacy documents through a guarded migration; included in CLI smoke tests |
| Amagami | Original prologue and initial event map; calendar, later routes and endings unverified | Included in the same native startup and asset-slug validation |
| Zero no Tsukaima | Nocturne playable; Concerto and Symphony have distinct coverage gaps | Included in native startup and metadata validation; multi-disc parity still needs disc fixtures |
| Snow Radish Vacation!! | ADV3 presentation playable; original effects and timing remain incomplete | Included in native startup and metadata validation |
| Summer Radish Vacation!! | CMP/script/media/input tests exist; all routes and endings unverified | Included in native startup and metadata validation |
| Doki Doki Literature Club! | Ren'Py/RPA/CPython port; patch and game-data differences matter | Shared VFS check for paired `scripts.rpa` and `images.rpa`, including `game/` and archive sources |
| School Days HQ | GPK/media/menu/save tests and reference comparisons; route parity not certified | Included in native startup checks; gameplay input remains intentionally gated to authenticated event types |
| RealLive | ISO, scenario, graphics/audio and many opcodes covered; full route/presentation parity unverified | VFS validator now insists `Seen.txt` and `Gameexe.ini` are regular files, not directories |
| Kirikiri2/KirikiriZ | XP3/TLG/KAG, bounded experimental TJS2; general TJS/plugins and title parity absent | Injects its XP3 asset provider into both runtime and engine, rather than leaving the engine on a fallback source |
| LucaSystem | AIR PAK and VM reference coverage; other LucaSystem games not certified | Fixes provider injection, packaged asset slug, case-insensitive VFS/ISO validation of the three PAKs, and metadata ID with guarded migration from legacy documents |
| Pure Mail | DVD script/graphics, early story, audio and animation; computer UI/route parity incomplete | Removes machine-specific paths; supports remembered selection, portable launcher, ISO/VFS directory probing and matching script/graphics archives |
| PONscripter / ONScripter | New independent CMake/Android port with VM, SAR/NSA assets, SDL3 runtime and synthetic tests; complete retail parity unverified | Registered as an explicit game target with macOS/iOS Xcode schemes |
| N2System | NsScript/NPA/AFS reference VM, scene graph and synthetic save tests; original title coverage remains unverified | Added to CMake/Android, startup audit and macOS/iOS Xcode schemes |

## Launcher and platform parity (2026-11-18)

- All 12 game launchers (plus the Example demo) follow one CLI policy: interactive startup uses the launcher by default; `--no-launcher`, `--frames N`, translation export and MCP launch without the launcher. Source discovery never overrides an explicit command-line choice.
- `Kotonoha.CommandLine` exercises the shared headless behavior; `check_port_contracts.py` rejects per-port launcher overrides and missing documentation entries.
- N2System and PONscripter are selectable in CMake, Android and the checked-in Xcode project. `ProjectManifest.json` reports `xcode_pending: []`; scheme presence alone still requires a real macOS/iOS build to validate.
- Synthetic checks cover startup contracts only; complete retail gameplay parity and device builds still require separate verification.

## Shared Kotonoha UI integration (2026-11-18)

Every port connects its native in-game menu and history to the engine
components, either directly or through game-specific subclasses:

- Example, RealLive, Kirikiri2/KirikiriZ, LucaSystem, Summer Radish and N2System use `InGameMenu`, `VNBacklog` and `VNSaveMenu`; N2System subclasses the menu so History opens the backlog it populates.
- Amagami, Zero no Tsukaima, Snow Radish, Doki Doki Literature Club, Pure Mail and PONscripter keep their native labels, drawing and input by subclassing `InGameMenu` and/or `VNBacklog`.
- Saya no Uta/PONscripter maps native `rmenu` to a four-item `InGameMenu`, native `lookback` to `VNBacklog`, and native `save`/`load` to `VNSaveMenu`; the VM remains in `WaitSystemCall` until each modal closes.
- School Days HQ keeps its authenticated native MenuBar/SysMenu and history skin; `VNSaveMenu` and `SchoolDaysBackLogEngine` (a `VNBacklog` subclass) remain registered fallbacks.
- Verified in this change with `AmagamiEngineTest` (retail disc), `PonscripterRuntimeTests` (synthetic and retail Saya no Uta), and the `N2System` suite (7 tests).

## Contract and synthetic tests

- `tools/check_port_contracts.py` audits the manifest, CMake game slugs,
  SDL callback entrypoints, bundle IDs, path references and the presence of C++
  tests for every port.
- `lib/tests/GameSourceProbeTests.cpp` tests case-insensitive regular-file
  groups in synthetic nested sources, without reading retail archives.
- `kotonoha_configure_app` registers `Kotonoha.PortHelp.<target>` for every
  native game app selected in a CMake build. This covers CLI initialization
  and dynamic dependencies without requiring assets, graphics or a controller.
- The `port-contracts.yml` workflow runs cheap source contracts on relevant
  pull requests and pushes to `dev`.

A suggested native validation sequence:

```sh
python3 tools/check_port_contracts.py
python3 tools/check_game_platform_isolation.py
cmake -S . -B build -DKOTONOHA_BUILD_ALL=ON -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build -R 'Kotonoha\\.(PortHelp|GameSourceProbe)' --output-on-failure
```

For each port, follow up with an asset-provided boot, scene/choice/save/load
round-trip, gamepad/touch interaction and video/audio timing comparison on its
supported platforms. Assets must be legally supplied locally, not committed.

## Important safety and compatibility boundaries

- The School Days gameplay input gate is deliberate: synthetic SDL input
  support does not authorize game-specific control mappings lacking comparison
  to original behavior.
- A launcher accepting an ISO only confirms expected filenames and file
  types; it does not decode or execute the original title.
- LucaSystem's and the reference demo's legacy document directories are moved to the canonical bundle ID
  only if it exists, is not a symlink and the destination does not already
  exist. A failed move is logged; newer data is never overwritten.
- Full platform builds (macOS, iOS, Android, Linux, Windows, WebAssembly) and
  testing against proprietary media were **not** executed as part of this
  source review. Performance, compatibility and story parity require tests
  and must not be represented as universal guarantees.
