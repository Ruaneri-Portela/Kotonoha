# Deferred architecture work — Kotonoha v3

**English (US)** | [Português (Brasil)](MAINTENANCE_BACKLOG.pt-BR.md)

The earlier Windows L4 artwork-path repair did **not** authorize CMake-tree
consolidation, SDL callback/RAII migration or save-path changes. The separately
authorized local upstream synchronization has since integrated some core
features (status below); remaining changes still need independent reviews
and build/test checkpoints. Never modify the 29 original GPKs or `LxH.GPK`,
run the retail executable, or use real saves.

## Upstream/dev synchronization audit — 2026-11-15

The local-only branch `sync/upstream-dev-20261115-021254` imports the upstream
architecture. An integrated core feature is not evidence that every game port,
variant or platform is verified. The original audit remains below for history.

| Step | Status in synchronized source | Evidence and remaining check |
| --- | --- | --- |
| 9 — CMake duplicates | **PARTIAL** — upstream preloading/host/platform helpers integrated (`cb05a02`, `43657a0`, `4c83f57`) | Root game matrix is upstream; legacy `cmake/` consumers remain. No deletion or equivalence claim. |
| 10 — divergent CMake | **PARTIAL** — upstream output, packaging and game selection integrated (`cb05a02`, `43657a0`) | Two-DLL manifest, DEV/DIST gates and Windows staging retained. Platform-specific parity and obsolete-module consolidation remain unverified. |
| 11 — FileResolver / AssetProvider | **CORE INTEGRATED; GAME VALIDATION PENDING** — `4214a85`, `c27e32b`, `bc1d473`, `7e7d65f`, `2d3a371`, `0efdc35` | Filesystem V2/Fetch/overlays are in shared code. The 29 retail GPKs, seven real LxH handoffs and strict locale priority have **not** been exercised with licensed content; do not enable network Fetch implicitly. |
| 12 — SDLCallbackApp | **INTEGRATED** — `151766b`, `e2b0e64`, `1185f77`, `428016b`, `50098c5` | School Days defines `init/event/iterate/quit` and one `KOTONOHA_SDL_CALLBACK_APP` entrypoint. The single launcher and L4 settings exception remain; `SchoolDaysLauncherTest` still requires investigation. |
| 13 — SDL RAII | **CORE INTEGRATED; GAME OWNERSHIP PENDING** — `8642f19` | `SDL.hpp` handles are available; do not claim School Days' manually owned presenter textures/fonts migrated. Audit each owner and shutdown/reopen independently. |
| 14 — StoragePaths | **CORE INTEGRATED; L4 EXCEPTION RETAINED** — `4214a85`, `c27e32b` | Namespaced shared storage is in place, but `games/school-days/src/main.cpp` explicitly uses `resourceDirectory()/settings.json` on Windows L4. Maintain separate `Config.DAT`; verify headless behavior only in disposable copies. |

Integration decisions and build results are recorded in the ignored
`build/_sync-decisoes.md` and `build/_sync-levantamento.md`.

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

## Step 11 — shared FileResolver / AssetProvider: core complete, parity open

The synchronized shared APIs replace the older storage shim and add bounded
VFS/Fetch support. School Days still owns its retail GPK and LxH assembly
policy. Check cache/seek/decoder behavior with synthetic streams, then obtain
an explicitly authorized, isolated licensed-media QA pass before publication.
Never infer strict locale fallback behavior from a successful compile alone.

## Step 12 — SDL shared callbacks: integration complete

`games/school-days/src/main.cpp` now exposes four static methods via
`KOTONOHA_SDL_CALLBACK_APP`, not a second `main()` or GUI. Keep the existing
launcher and state lifetime. The bridge itself needs no repeat migration;
the remaining `SchoolDaysLauncherTest` timeout must be isolated headlessly.

## Step 13 — SDL RAII: shared handles complete, game migration deferred

Inventory SDL ownership in each class, starting with
`SchoolDaysBackLogPresenter.cpp`'s manual textures and fonts. Replace one owner
at a time with established `Kotonoha/core/SDL.hpp` handles, avoiding double
destruction; verify shutdown/reopen tests and Windows L4 builds after each.

## Step 14 — StoragePaths: core complete, Windows L4 exception retained

Shared `StoragePaths` supplies per-app directories, while School Days Windows
L4 deliberately keeps `settings.json` next to `SchoolDays.exe` and preserves
its separate `Config.DAT` contract. The core migration is already integrated;
do **not** move L4 player profiles. Add an isolated `--documents` smoke test
only when the test can avoid commercial assets and GUI interaction.

## Deferred platform and documentation follow-ups

`Android/app/build.gradle` had only two launcher-art source paths updated
statically; Android was not built or run. No Linux, macOS, iOS or Web tests
were conducted. `Android/`, `Xcode/` and `buildsystems/` consolidation is not
authorized here. Several older platform guides still describe the obsolete
Xcode workspace or old Gradle locations; update those guides in bilingual
pairs only during the appropriate platform review, without rewriting dated
historical notes or `games/school-days/PORT_STATUS.md`.
