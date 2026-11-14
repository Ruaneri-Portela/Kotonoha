# Filesystem V2 migration matrix

**English (US)** | [Português (Brasil)](MIGRATION.pt-BR.md)

This report separates **SDL3 I/O conversion** from **actual virtual-source support**. A replacement of `std::ifstream` with `Kotonoha::Filesystem::InputFile` still opens a local path unless its caller passes an existing virtual `FileHandle`.

| Game | Direct stream conversion | Virtual source/index integration |
| --- | --- | --- |
| Example | Shared engine SDL3 layer | Still uses the engine's default provider |
| Amagami | Session reads changed to SDL3 adapter | Native disc/source parsers still need virtual mounting |
| Zero no Tsukaima | Session and frontend streams changed | RAW disc/CUE indexing and VFS port bootstrap pending |
| Snow Radish | ADV3 DAT sources read/index through VFS handles | Path or shared mount constructor; ISO extraction removed from boot; fixtures require validation |
| Summer Radish | CMP packs and media use virtual handles | Directory/ISO and source overlay pilot; regression build pending |
| DDLC | RPA read, loose index and launch path support virtual handles | Directory/ISO/TAR initial support; VNX/overlay regression pending |
| School Days | KTRF, GPK and save streams use SDL3-backed adapters | GPK indexing and streaming still require deeper VFS migration |
| RealLive | Voice reads changed; existing ISO decoder retained | Shared ISO provider and integration still require tests |
| Kirikiri | Loose files and XP3 index/read use VFS mounts | Root XP3 enumeration and validator updated; game-specific tests required |
| LucaSystem | PAK indexes and on-demand reads through VFS mounts | Source-level overlay before indexing; synthetic tests run in CI |
| Pure Mail | Loose Pmail provider and shared ADV3 DAT source use VFS | Port runtime still uses SDL3 adapters and needs gameplay tests |

The shared `FileSystemAssetProvider` and `IndexedFileSystemAssetProvider` accept a VFS mount. Providers are **not** automatically migrated simply because they link to the engine.

## Check source migration

```sh
python3 tools/audit_filesystem_v2.py
python3 tools/audit_filesystem_v2.py --strict
```

The default command reports remaining direct standard/stdio runtime calls and checks V2 sources in Xcode/CMake. `--strict` fails if direct calls remain. Neither command compiles or plays a game. Do not treat a clean audit as proof that the virtual source index/overlay model works in all ports.

## Remaining architectural work

- Add portable compile/CTest and per-game regression runs; Xcode/Android/web builds are not verified.
- Migrate each format-specific provider to `Filesystem::open(id)` for its **source packages**, including indexing before overlays are applied.
- Full `FetchAssetProvider` web bootstrap, worker/Asyncify handoff and persistent caching as required, with browser/CORS/Range tests; the current implementation is a non-blocking prefetch backend.
- Migrate all legacy browser media paths to the implemented `BrowserBlobAdapter`, verify FFmpeg lifecycle/seek, and add multi-BIN/CUE modes and remaining ISO behaviors.
- Remove the temporary physical-path adapters only after all eleven ports have equivalent tested virtual-source behavior.

The [original specification](SPECIFICATION.pt-BR.md) defines the final acceptance criteria. The draft PR remains unmerged.

- **Web fallback:** local-first `FallbackMount`, asynchronous HTTP manifest bootstrap and Fetch registration are now implemented, with a bounded 128 MiB startup preload. Web browser tests and complete legacy-provider migrations remain pending.

The obsolete `core/FileSystem.hpp` forwarding header was removed after migrating first-party code and test includes to `Kotonoha/Filesystem/StoragePaths.hpp`. Storage path policies and symbol names remain unchanged.
