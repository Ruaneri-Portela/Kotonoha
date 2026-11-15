# Deferred architecture work — Kotonoha v3

**English (US)** | [Português (Brasil)](MAINTENANCE_BACKLOG.pt-BR.md)

The Windows L4 artwork-path repair does **not** authorize any CMake-tree
consolidation, SDL callback/RAII migration or save-path change. Each item below
needs a separate reviewable plan and build/test checkpoint. Never modify the
29 original GPKs or `LxH.GPK`, run the retail executable, or use real saves.

## Future step 9 — consolidate byte-identical CMake modules

The filenames below occur in both `cmake/` and `buildsystems/cmake/` and have
identical SHA-256 content in the audited checkout. No file is to be deleted.
Consumers are static filename references, including inactive platform branches.

| Module | Current text consumers |
| --- | --- |
| `CheckBundleContents.cmake` | `cmake/KotonohaApp.cmake`, `buildsystems/cmake/KotonohaApp.cmake`, `lib/tests/CMakeLists.txt` |
| `CheckBundledAssets.cmake` | `cmake/KotonohaApp.cmake`, `buildsystems/cmake/KotonohaApp.cmake` |
| `CopyAssetDirectory.cmake` | `lib/CMakeLists.txt`, `buildsystems/cmake/KotonohaApp.cmake` |
| `Info.plist.in` | `cmake/KotonohaApp.cmake`, `buildsystems/cmake/KotonohaApp.cmake` |
| `MSVC-UTF8.cmd` | No direct tracked CMake filename reference found; verify generated command-line use before moving |
| `MSVC.cmake` | `CMakeLists.txt`, `lib/CMakeLists.txt` |
| `PackageAssets.cmake` | `lib/CMakeLists.txt`, `lib/tests/CMakeLists.txt`, `cmake/KotonohaApp.cmake`, `buildsystems/cmake/KotonohaApp.cmake` |

**Plan:** inventory generated command lines; redirect one consumer at a time
to `buildsystems/cmake/`; validate Windows L4 and CTest after each change;
retain the old files as history (move with `git mv` only after review). Other
platforms remain untested until an authorized platform-specific session.

## Future step 10 — reconcile divergent CMake modules

| Module | Current text consumers / dependencies |
| --- | --- |
| `BundleApp.cmake` (different) | `cmake/KotonohaApp.cmake`, `buildsystems/cmake/KotonohaApp.cmake`, `lib/tests/CMakeLists.txt`, `games/doki-doki-literature-club/tests/CMakeLists.txt` |
| `KotonohaApp.cmake` (different) | `lib/CMakeLists.txt` explicitly includes the `cmake/` version; `buildsystems/cmake/KotonohaApp.cmake` is an alternative implementation |
| `CopyRuntimeDependencies.cmake` (only `cmake/`) | `cmake/KotonohaApp.cmake` |
| `PackageWindowsRuntime.cmake` (only `cmake/`) | `cmake/KotonohaApp.cmake` |
| `CopyRuntimeLibraries.cmake` (only `buildsystems/cmake/`) | `buildsystems/cmake/KotonohaApp.cmake` |
| `KotonohaOutputs.cmake`, `index.html.in` (only `buildsystems/cmake/`) | `buildsystems/cmake/KotonohaApp.cmake` and platform-specific output configuration; re-audit indirect includes |

**Plan:** diff behavior for Windows two-DLL/staging and other platform hooks;
design an opt-in compatibility layer; update one consumer per commit; keep
the original implementation until parity tests pass. No automatic replacement.

## Future step 12 — SDL shared callbacks (separate plan)

Inventory `games/school-days/src/main.cpp`'s four `SDL_App*` functions and
state lifetime; adapt to `KOTONOHA_SDL_CALLBACK_APP` while preserving launcher,
DEV/DIST flags and Android Activity handoff. Compile Windows L4 and run
launcher/runtime regression tests before any wider platform work.

## Future step 13 — SDL RAII (separate plan)

Inventory SDL ownership in each class, starting with
`SchoolDaysBackLogPresenter.cpp`'s manual textures and fonts. Replace one owner
at a time with established `Kotonoha/core/SDL.hpp` handles, avoiding double
destruction; verify shutdown/reopen tests and Windows L4 builds after each.

## Future step 14 — StoragePaths (separate plan)

Document Windows L4's existing `settings.json` next to `SchoolDays.exe`, its
`Config.DAT` contract, and isolated `--documents`/`--config` QA first. Introduce
`StoragePaths` compatibility only with explicit migration behavior and tests
proving that existing player profiles never move or overwrite unexpectedly.

## Deferred platform and documentation follow-ups

`Android/app/build.gradle` had only two launcher-art source paths updated
statically; Android was not built or run. No Linux, macOS, iOS or Web tests
were conducted. `Android/`, `Xcode/` and `buildsystems/` consolidation is not
authorized here. Several older platform guides still describe the obsolete
Xcode workspace or old Gradle locations; update those guides in bilingual
pairs only during the appropriate platform review, without rewriting dated
historical notes or `games/school-days/PORT_STATUS.md`.
