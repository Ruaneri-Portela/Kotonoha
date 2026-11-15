# Documentation index — Kotonoha v3

**English (US)** | [Português (Brasil)](README.pt-BR.md)

The repository's root README files describe the project, supported games, and
basic build workflows. These guides cover the shared engine and tooling; they
remain at their published locations so existing links do not break.

| Area | Document | Purpose |
| --- | --- | --- |
| Getting started | [README (English)](../README.md) / [Português](../README.pt-BR.md) | Overview and project layout |
| Building and validation | [BUILD_VALIDATION.md](BUILD_VALIDATION.md) | Prerequisites, build, and test instructions |
| Configuration | [SETTINGS.md](SETTINGS.md) | Shared engine settings |
| Assets and overlays | [ASSET_OVERLAYS.md](ASSET_OVERLAYS.md) | Asset ownership, overlays and lookup |
| Platform file paths | [Directories.pt-BR.md](Directories.pt-BR.md) | Directories by platform |
| Android | [Android README](../Android/README.md) / [PT-BR](../Android/README.pt-BR.md) | Deferred mobile target |
| School Days HQ | [School Days docs index](../games/school-days/docs/README.md) | Windows L4 implementation, parity, localization and QA |
| Architecture follow-ups | [Maintenance backlog](MAINTENANCE_BACKLOG.md) | Deferred CMake consolidation, SDL callbacks, RAII and StoragePaths |
| Historical upstream sync | [RUANERI_SYNC_FOLLOWUP_2026-11-04.md](RUANERI_SYNC_FOLLOWUP_2026-11-04.md) | Dated integration notes, not a current change log |
| Repository hygiene | [REPOSITORY_HYGIENE_2026-11-09.md](REPOSITORY_HYGIENE_2026-11-09.md) | Safe cleanup, protected data, and local rollback |

## Folder conventions

- `lib/`, `cmake/`, `games/` and the tracked contents of `assets/` are source
  or build inputs; do **not** remove them as generated clutter.
- Both `cmake/` and `buildsystems/cmake/` are presently used; they have not been
  consolidated. The active Windows L4 project still includes `cmake/` modules.
- `build/`, `vcpkg_installed/` and Android build outputs are generated, but
  **local `build/` also contains commercial GPKs, preserved saves, reports,
  junctions to a retail install and a handoff ZIP**. Never delete it wholesale.
- Dated School Days research notes are separated by topic under the game's
  docs folder. Root-level published documents stay in place for link stability.
- Screenshots previously loose in the repository root were archived locally
  under `build/repo-cleanup-backup-20261109/root-qa-captures/` along with a
  separately checked ZIP backup, also copied outside the repository to
  `D:/Dev/Kotonoha-v3-reorg-backups/`. They are not committed assets.

## Safety

No cleanup script should follow filesystem junctions/reparse points or delete
the original retail executable, GPKs, saves, private translations or the
Windows L4 Release. Historical notes and QA captures are records, not proof
that the corresponding bugs or stages are still current.
