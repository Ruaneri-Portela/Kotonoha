# Ruaneri upstream synchronization — November 23, 2026

## Target and provenance
Fixed upstream snapshot `Ruaneri-Portela/Kotonoha:dev` **009e5cfa299008ef3db37a4fb35ba4883422df81** is being merged into the `tsubaki-0x/Kotonoha` fork, preserving School Days HQ Windows/Android policies and behavior. Previously recorded local upstream ref was `afcfc64bcbe629765386189bb4f57d37a444160d`.

The isolated candidate branch `sync/ruaneri-dev-20261123-009e` began at fork `dev` commit `d5b13a6429fa4064c7214a8149b40d3c2a992ba8`. No upstream repository branches were modified.

## Integrated areas
- Neutral codecs in `modules/formats`, archive/VFS providers in `modules/providers`, VM runtimes in `modules/vms`.
- Prefetch and subtitle workers, media/presentation/VFS/SDL/upscaling updates, more tests and many game parity improvements.
- Shared brand assets and native icons, modern CMake, Gradle, Apple, Web and Windows build changes.
- Updated upstream `AGENTS.md` and `AGENTS.pt-BR.md`, **plus preserved fork-only section** covering Windows and Android, DEV/DIST, originals, saves and distribution safety; permanent fork distribution branch is `hybrid`.
- Owner-specific `assets/SPLASH.PNG` and School Days icon remain separate from universal engine branding.
- School Days Android retains SAF and never bundles the 29 retail GPK archives; ARMv7 NEON and ARM64 remain supported. No device was accessed.
- Consolidated duplicate RealLive/School Days/ImGui/Encoding test registrations, fixed Windows path handling in the CMake target checker, portable Windows vcpkg presets, and a pinned baseline available in the verified local vcpkg tree.

## Safety and validation
The DEV candidate required **65 merge conflict resolutions**. On case-insensitive Windows filesystems, the upstream casing migration `lib/src/Filesystem` to `lib/src/filesystem` requires a clean build to validate. Do not run the retail School Days EXE or touch original GPKs/saves.

Static build metadata, release matrix, Android NEON, platform isolation, persistence and duplicate-target checks passed. Native Windows QA results follow; **Windows synthetic tests do not establish Android APK parity, real GPU gameplay, or on-device behavior**.

### Windows Release QA — November 23, 2026
- **DEV:** MSVC x64 Release `ALL_BUILD` passed; **98/98 synthetic CTest passed** using `ctest -C Release -LE real`. Evidence: `D:/Dev/_research/Kotonoha-Upstream-Sync-Audit-20261123/ctest-dev-all-synthetic-green-final.log`.
- **HYBRID/DIST:** MSVC x64 Release `ALL_BUILD` passed with `KOTONOHA_DISTRIBUTION_BUILD=ON`, MCP/DEV controls OFF and commercial assets excluded; **93/93 synthetic CTest passed**, including `SchoolDays.DistDebugAbsence`. Evidence: `D:/Dev/_research/Kotonoha-Upstream-Sync-Audit-20261123/ctest-hybrid-dist-all-synthetic-final-green.log`.
- **Real-data tests excluded:** 14 DEV and 12 HYBRID/DIST cases are outside this synthetic test run. No retail School Days executable/installer, original GPKs, or personal saves were used.
- **Regressions fixed:** merged School Days backlog/choice/menu fixtures, Windows WinRes branding, explicit display selection, per-package storage roots, duplicate ISO paths, FFmpeg AVIO close, unavailable MetalFX, touch slider release, DIST-only CLI rules, and portable synthetic provenance fixtures under long TEMP roots.
- **Android is not certified:** HYBRID `Gradle :app:tasks --offline` passed configuration, but the QA environment lacks SDK platform `android-37`; no new APK build or physical-device QA occurred. Private launcher/Display Options imagery was intentionally absent from the integration worktrees.

## Recovery
A verified 91-ref Git bundle was saved to `D:/Dev/_backups/Kotonoha-pre-ruaneri-sync-20261123-0606/kotonoha-all-refs.bundle` (SHA-256 `EC7A583B15D6A39C3E8BF49C96D2C1493A5A2325882FCD8F8EF7EB71282BC1E6`). The 13 untracked Cross Days L1 study files are separately backed up in `cross-days-l1-untracked.zip` (SHA-256 `3607A7F98BF527BECB6745F93817FCC056E883E1F8C0BA7B3C453D31AF84D78F`; remove the spacing before hash use). Cross Days L1 remains frozen and unmodified.

HYBRID must be integrated and validated independently, retaining its own history and DEV/DIST distinctions. Never reset, clean, force-push, or remove a worktree/branch without evidence; upstream PR #95 remains a separate draft.

## Status and repository hygiene
Both candidate Windows build/test gates passed. This is **not** a certification of a player-ready package, visual/GPU gameplay, or Android parity. Packaging licensed/private assets, real hardware QA, and final release checks remain separate gates.

The read-only Git inventory in `D:/Dev/_research/Kotonoha-Upstream-Sync-Audit-20261123/REPOSITORY_INVENTORY_20261123.pt-BR.md` found **30 worktrees and 37 local branches**. Preserve Cross Days L1 with its untracked `games/cross-days/` research and the translation PR worktree with uncommitted `SchoolDaysTranslationGate` files. Do not delete unique branches/PR worktrees or ignored artifacts before verifying ownership, current remote PR status and recoverability.
