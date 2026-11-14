# Filesystem V2 migration matrix

**English (US)** | [Português (Brasil)](MIGRATION.pt-BR.md)

This report separates **SDL3 I/O conversion** from **actual virtual-source support**. A replacement of `std::ifstream` with `Kotonoha::Filesystem::InputFile` still opens a local path unless its caller passes an existing virtual `FileHandle`.

| Game | Direct stream conversion | Virtual source/index integration |
| --- | --- | --- |
| Example | Shared engine SDL3 layer | Still uses the engine's default provider |
| Amagami | Session reads changed to SDL3 adapter | Native disc/source parsers still need virtual mounting |
| Zero no Tsukaima | Session and frontend streams changed | RAW disc/CUE indexing and VFS port bootstrap pending |
| Snow Radish | Pack and session streams changed | Archive and game bootstrap still use physical paths |
| Summer Radish | CMP packs and media use virtual handles | Directory/ISO and source overlay pilot; regression build pending |
| DDLC | RPA read, loose index and launch path support virtual handles | Directory/ISO/TAR initial support; VNX/overlay regression pending |
| School Days | KTRF, GPK and save streams use SDL3-backed adapters | GPK indexing and streaming still require deeper VFS migration |
| RealLive | Voice reads changed; existing ISO decoder retained | Shared ISO provider and integration still require tests |
| Kirikiri | XP3 C stdio replaced with SDL3 | XP3 provider still indexes physical file paths |
| LucaSystem | PAK reads use SDL3-backed adapter | PAK providers still index physical paths |
| Pure Mail | PAK/runtime reads use SDL3-backed adapter | PAK providers still index physical paths |

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
