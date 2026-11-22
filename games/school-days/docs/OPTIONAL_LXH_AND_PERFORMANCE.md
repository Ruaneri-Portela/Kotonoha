# School Days HQ — Optional L×H & Windows Performance Optimization

**English (US)** | [Português (Brasil)](OPTIONAL_LXH_AND_PERFORMANCE.pt-BR.md)

This document describes the optional **L×H (7 extended epilogues)** architecture shared across Windows and Android, together with the evidence-based Windows x64 performance optimizations and real-data benchmarks completed on November 22, 2026.

---

## 1. Optional L×H Architecture

### Core Contract
- **Base HQ Independence**: School Days HQ runs completely with only the **29 original retail GPKs**. `Packs/LxH.GPK`, translated `*-lxh.GPK`, `*-lxh.json`, and `forced_subtitles/` are all strictly optional.
- **State Separation**:
  1. **User preference** (`EnableLxh=0|1` in `Kotonoha.ini`, stored alongside `ContentAspectPreference` and `AndroidFrameLimit`).
  2. **Content availability** (non-mutating validation of `Packs/LxH.GPK` — 28 raw entries — and matching L×H translation for the selected language).
  3. **Effective runtime state** (`effectiveEnabled` / `assets.lxhMounted()`).
- **Default on Fresh Install**:
  - No explicit `EnableLxh` + valid `Packs/LxH.GPK` present → defaults **ON**.
  - No explicit `EnableLxh` + `Packs/LxH.GPK` absent or invalid → defaults **OFF**.
- **Explicit Preference Preservation**: Turning `Enable LxH endings` ON or OFF is persisted verbatim in `Kotonoha.ini`. If the user selects ON while `Packs/LxH.GPK` or the selected language's L×H translation is missing/incomplete, the preference is preserved, an objective status notice explains why L×H is unavailable, and base School Days HQ launches cleanly without silent cross-language fallback.

### Launcher Controls & CLI Flags
- **Windows DEV & DIST Launcher** (`SchoolDaysLauncher`): `General` tab checkbox `Enable LxH endings` with live availability/status notice, Cancel rollback snapshot, and atomic persistence in `Kotonoha.ini`.
- **Android DEV & DIST Launcher** (`SchoolDaysSetup`, `SchoolDaysFolder`, `SchoolDaysConfigStore`): `Switch` (`Enable L×H endings` / `Ativar finais L×H`) with live status notice, 29-retail-GPK-only acceptance, and explicit `--lxh` / `--no-lxh` handoff to native startup.
  - *Note*: Android implementation and synthetic `PackCaseRegression` (11 cases) + XML validation are complete; **physical Android QA on POCO X7 Pro is pending hardware availability**.
- **CLI Flags** (DEV & DIST): `--lxh` and `--no-lxh` override the startup resolution for one session without mutating saved `Kotonoha.ini` preferences.

### Runtime & Save Provenance Behavior
- **L×H OFF**: `Packs/LxH.GPK` and `*-lxh.GPK` are never mounted. The 7 ending scripts (`05-A2-F00`, `05-A2-Z00`, `05-B0-F00`, `05-G0-L03`, `05-KC-F00`, `05-SB-E01`, `05-SC-L03`) resolve strictly from base retail `Packs/Script.GPK` (or base translated `Script - <Lang>.GPK`), with zero L×H fades or epilogue media.
- **L×H ON**: All 7 epilogues, L×H fade transitions, and strict L×H translation validation remain intact.
- **Save Provenance Enforcement**:
  - Normal HQ saves and legacy saves (without `.kotonoha-lxh.v1` sidecar) load with both **L×H ON** and **L×H OFF**.
  - Attested L×H epilogue saves load with **L×H ON**, and are cleanly rejected **before any runtime state mutation** with a clear player-facing message when **L×H is OFF or unavailable** — leaving the save file and its sidecar untouched.

---

## 2. Windows x64 Performance Optimizations

### Benchmark Environment
- **OS**: Microsoft Windows 10 Pro (`10.0.19045`, x64)
- **CPU**: Intel Xeon E5-2690 v4 @ 2.60 GHz
- **RAM**: 23.9 GB
- **GPU**: NVIDIA GeForce RTX 3060 (OpenGL + MPV Upscaler)
- **Compiler / Build**: MSVC 19.51 (`Visual Studio 18 2026`), C++17, `Release` (`/O2 /MD`), SDL3 + SDL3_ttf + ImGui from submodules, vcpkg `x64-windows-static-md`
- **Dataset**: Read-only 30-GPK retail dataset (29 retail + `LxH.GPK`), `pt-BR.json` (2.19 MiB), `pt-BR-lxh.json`, `Script - Brazilian.GPK`, `pt-BR-lxh.GPK`, isolated temp `--documents`

### Implemented Evidence-Based Optimizations
1. **Lazy Directory Index Cache in `SchoolDaysGpkArchive::ListDirectory()`**: Replaced per-call `O(N)` full-archive path folding and linear scans with a thread-safe lazily built directory index preserving exact case-insensitive collision semantics. Eliminated ~12.4 ms stalls on every gameplay background change (`discoverAbcForBackground()`).
2. **Desktop Cached Archive Stream + Zero-Copy Raw Read (`SchoolDaysGpkArchive::ReadEntry`)**: Extended the mutex-guarded `cachedStream_` reuse (previously Android-only) to desktop with automatic reopen-on-error fallback, read raw prefix-free entries directly into `*out`, and replaced `out->assign(size, 0)` with `out->resize(size)` in `ReadRange()`.
3. **Eliminated Duplicate ORS Read in `ValidateTranslatedScript()`**: Removed the redundant double `ReadEntry` + zlib inflation of all 1,855 ORS scripts when validating translated `Script.GPK` packs.
4. **Optimized `SchoolDaysTranslationGate::CollectDetailed()` & `CheckLayered()`**: Reserved slot storage upfront, cached per-node translation section pointers across consecutive slots, and replaced `std::map<pair<int, string>, set<string>>` collision tracking with zero-copy `std::string_view` hash tracking.
5. **Zero-Allocation Per-Frame Render Hot Paths**: Cached wrapped choice lines (`choiceScratchLines`) in `GameScene` instead of re-converting UTF-16 and re-wrapping every frame, and replaced per-frame string concatenation in `SubtitleScene::render()` with direct field comparison.

### Measured BEFORE vs AFTER Results (Windows x64 Release)

| Workload (Real Retail Data, Median of 5 runs unless noted) | BEFORE | AFTER | Delta / Speedup |
|---|---:|---:|---:|
| `ListDirectory` ×120 `Event*` dirs (ABC transitions) | 1,485.50 ms | **8.38 ms** | **177.3× faster (−99.4%)** |
| Read 500 `Script.GPK` entries (529 KiB) | 30.74 ms | **6.36 ms** | **4.83× faster (−79.3%)** |
| `CollectDetailed` 1,855 ORS (L×H OFF, 30,968 slots) | 836.18 ms | **342.32 ms** | **2.44× faster (−59.1%)** |
| `CollectDetailed` 1,855 ORS (L×H ON, 31,241 slots) | 944.59 ms | **436.62 ms** | **2.16× faster (−53.8%)** |
| Mount `Script - Brazilian.GPK` (L×H OFF, 3 runs) | 1,272.65 ms | **558.83 ms** | **2.28× faster (−56.1%)** |
| Mount `Script - Brazilian.GPK` + `pt-BR-lxh.GPK` (L×H ON, 3 runs) | 1,239.17 ms | **556.20 ms** | **2.23× faster (−55.1%)** |
| `CheckLayered` audit `pt-BR` (L×H OFF, 30,968 slots) | 36.24 ms | **18.14 ms** | **2.00× faster (−49.9%)** |
| `CheckLayered` audit `pt-BR` (L×H ON, 31,241 slots) | 37.99 ms | **15.41 ms** | **2.47× faster (−59.4%)** |
| Mount 29 original retail GPKs (L×H OFF) | 156.04 ms | **125.49 ms** | **1.24× faster (−19.6%)** |
| Mount 30 GPKs (L×H ON) | 152.36 ms | **126.60 ms** | **1.20× faster (−16.9%)** |
| `Runtime::loadGame` (20 runs, isolated temp) | 1.30 ms | **1.07 ms** | **1.21× faster (−17.7%)** |
| `Runtime::saveGame` (20 runs, isolated temp) | 1.02 ms | **1.04 ms** | Parity |
| Real GPU `SchoolDays.exe` (`--no-lxh`, 90 frames, VSync) | 4,050.4 ms | **4,039.0 ms** | Parity (VSync-capped) |
| Real GPU `SchoolDays.exe` (`--lxh`, 90 frames, VSync) | 4,044.8 ms | **4,030.3 ms** | Parity (VSync-capped) |
| Peak Working Set RAM | 57.6 MiB | **56.6 MiB** | **−1.0 MiB** |

No narrative timings, audio/video fidelity, subtitle styles, upscaling quality, or save formats were altered.
