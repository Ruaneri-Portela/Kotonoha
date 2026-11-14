# Filesystem V2 — implementation status

**English (US)** | [Português (Brasil)](README.pt-BR.md)

The authoritative design is the [original architecture specification](SPECIFICATION.pt-BR.md). This module is an **experimental read-only virtual filesystem**; it does not yet replace all legacy readers.

## Implemented in this branch

- `Kotonoha::Filesystem::FileHandle` (read/seek/tell/size), `IMount`, `FileInfo` and typed errors.
- `SDLFileSource`: local file bytes through SDL3 IOStream; no `std::ifstream`.
- `DirectoryMount`: indexed case-insensitive directory, safe relative lookup and symlink containment.
- `LayeredMount`: source-file overlays with explicit priority, merged listings and separate logical-override lookup. A replaced package is never merged by entries with the original.
- `ISO9660Mount`: reuses the existing `DiscImage` parser and range reads, without temporary full-disc extraction.
- `Filesystem::mount(source)`: local directories, recognized ISO/BIN layouts and USTAR/PAX/GNU-longname TAR files. Unsupported sources fail explicitly.

Saves, preferences and Documents **remain outside** this read-only API. The old experimental `UnifiedFileSystem` write API was removed, not promoted.

## Checks

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build --target KotonohaFilesystemV2Tests
ctest --test-dir build -R '^Kotonoha\.FilesystemV2$' --output-on-failure
```

Synthetic tests cover directory/ISO reads, seek/EOF, overlays, collisions, symlink traversal, and independent handles. Actual native, Android, WebAssembly and Apple build results are **not yet verified**.

## Pending migration

The physical-path compatibility adapter remains for older providers. Next phases: migrate Summer CMP/indexing to mounted input; adapt other providers; connect FFmpeg via custom AVIO; implement safe Fetch/Blob for browsers and BIN/CUE modes as required. An Emscripten Fetch provider now exists with asynchronous preloading and bounded Range requests, but requires browser validation. The HTTP provider does not block the browser main thread and raises WouldBlock for missing cached regions. HTML Blob and a worker/Asyncify suspension bridge are not yet implemented.

The legacy StoragePaths implementation was moved to `lib/src/Filesystem/StoragePaths.cpp`; the old `core/FileSystem.hpp` remains a forwarding shim. The save directory policy and public symbols are unchanged.

Mount detection accepts `.tar`, `.iso`, `.bin` and `.img` (disk types verified by the existing `DiscImage` parser); unknown ordinary files raise `UnsupportedMount`. Compressed `.tar.gz`, UDF and CUE multi-track mounting are not supported by this phase.

The virtual `MediaPlayer::open(FileHandle, logicalName, ...)` overload can now prepare a browser-supported format as an owned `blob:` URL through `BrowserBlobAdapter` (64 MiB limit). Native playback keeps custom FFmpeg AVIO. The older path/AssetProvider web playback routes still require migration; browser behavior has not been tested.
