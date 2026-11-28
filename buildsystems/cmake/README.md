# CMake layout

**English (US)** | [Português (Brasil)](README.pt-BR.md)

The project root `CMakeLists.txt` only orchestrates configuration. Keep
platform settings and shared options at the top level; keep responsibilities
in the directories below:

- `dependencies/`: discover and select SDL3 / SDL3_ttf, Dear ImGui (pinned
  submodule or vcpkg), FFmpeg, libass, json-c, and initialize required
  submodules (`KotonohaDepsProvider.cmake`).
- `games/`: canonical game list, target creation and selection, and
  platform-specific application target configuration.
- `packaging/`: assets, shared-runtime deployment, macOS bundles, Python,
  native ZIP archives via CPack, output layout, and per-game WebAssembly PWA
  templates.

`BuildVersion.cmake`, `KotonohaAndroid.cmake`, `KotonohaOptions.cmake`,
`KotonohaReport.cmake`, and `KotonohaTestGroups.cmake` remain here because
they are common project setup or reporting rather than group-specific logic.

When moving helpers, update script-mode `-P` invocations, CI workflow path
filters, and the source-contract tests under `tools/`. Do not add legacy
compatibility includes or compiler-specific launcher hacks.
