# Unified filesystem prototype

**English (US)** | [Português (Brasil)](UNIFIED_FILESYSTEM.pt-BR.md)

This is an **incremental prototype**, isolated in a feature branch. It does not replace existing `FileResolver`, archive providers, ISO readers, or game save formats.

`UnifiedFileSystem` composes the existing `directoryLayout(StorageLocations, game)` policy and an optional `AssetProvider` to expose:

- `FileDomain::Engine`, `Game`, `Reference`, and `Saves`;
- ordered, read-only resource lookup; game-provider and overlay precedence;
- optional byte reads (`std::nullopt` for absent, empty vector for empty file);
- binary save writes in the per-app save directory, through the existing atomic writer;
- rejection of absolute paths, `..` traversal (including backslash forms), and symlinks escaping read/write roots.

`AssetProvider` remains responsible for original archive formats, codecs, overlays, and streaming. The new facade does not need archive extraction and never writes back to original game installations.

## Example

```cpp
#include <Kotonoha/core/UnifiedFileSystem.hpp>

auto files = Kotonoha::UnifiedFileSystem::forApplication("example", gameProvider);
auto data = files.read(Kotonoha::FileDomain::Game, "story.ks");
if (data) {
    files.writeSave("chapter/slot.dat", *data);
}
```

Here `gameProvider` is an optional `std::shared_ptr<Kotonoha::AssetProvider>`. Do **not** use this sample's save write to copy game data in production; it only illustrates the API.

## Tests and migration

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target KotonohaUnifiedFileSystemTests
ctest --test-dir build -R '^Kotonoha\.UnifiedFileSystem$' --output-on-failure
```

Test fixtures use temporary synthetic data. They check desktop, Android and iOS path ordering, overlay precedence, binary saves, missing/empty files, and traversal/symlink rejection.

**Not yet migrated:** existing ports continue using their current storage/asset APIs. Adopting this service should be incremental and first verify platform builds, save migration, browser IDBFS, and native streaming behavior.

The factory uses the existing platform discovery and preserves WebAssembly's `/save` IDBFS mount. For deterministic unit tests, construct the service directly with synthetic `StorageLocations`.
