# Kotonoha — Retail Parity Verification (2026-11-23)

This document records the objective parity evidence collected on 2026-11-23.

## Methodology

- Dedicated build: `out/build/retail-verify` (macOS arm64, vcpkg, triplet
  `macos-arm64-vcpkg`, `CMAKE_BUILD_TYPE=Release`).
- All retail data gates populated pointing to
  `/Volumes/Nero 2TB/KotonohaAssets` (read-only).
- CTest filter: `-L real` (lowercase; `-L REAL` returns 0 — case error already
  corrected in the reports).
- Tests without data return `77` and are registered with
  `SKIP_RETURN_CODE 77`, showing as `Skipped`, never as false `Passed`.

## Result

```
$ ctest --test-dir out/build/retail-verify -L real --output-on-failure
100% tests passed out of 89
Label Time Summary: real = 241.63 sec*proc (89 tests)
```

Final coverage: **89/89 retail tests** with all gates — the 27 asset sets in
the `KotonohaAssets` folder (including AIR-SE/KANON/PLANETARIAN/Clannad
editions, multi-disc LostM/Imouto, LargePonPon and NEKOPARA VNX tests).
One test is legitimately `Skipped`: `Kirikiri.RetailXp3Startup`
— 1186/1187 entries of `data.xp3` are protected by `PackinOne.dll`
(flag `0x80000000`), with no decryption key available; the test reports the
exact diagnostic instead of a false `Passed`. Multiple stable runs.

Synthetic/engine CTest: **209/209** (`-j4`), zero failures after fixing
invalid display, Documents/save root and duplicate ISO records.

## Coverage per game

| Game | Gate | `real` tests | Status |
|---|---|---:|---|
| AIR (LucaSystem PAKs) | `LUCA_SYSTEM_TEST_DATA` | 5 | PASS |
| AIR_PT-BR (RealLive) | `REALLIVE_TEST_DATA` | 2 + Session | PASS |
| Steins;Gate (N2System) | `KOTONOHA_STEINSGATE_DIR` | 1 | PASS |
| NUKITASHI + NUKITASHI 2 (Artemis) | `KOTONOHA_ARTEMIS_RETAIL_DIR` | 1 (both editions) | PASS |
| DDLC + DDLC PT-BR Patch | `DDLC_DATA_DIR`, `DDLC_OVERLAY_DIR` | 8 | PASS |
| Katawa Shoujo | `KATAWA_DATA_DIR` | 2 | PASS |
| Amagami | `AMAGAMI_DISC_IMAGE` | 4 | PASS |
| Clannad PT-BR (partial) | `CLANNAD_TEST_DATA` | SpriteAtlas | PASS |
| School Days HQ | `SCHOOL_DAYS_DATA_DIR` | 17 | PASS |
| Snow Radish Vacation | `SNOW_DATA_DIR` | 4 | PASS |
| Summer Radish 1/2 | `SUMMER_DATA_DIR` | 5 | PASS |
| Zero no Tsukaima 0/1/2 | `ZERO_*_DISC_IMAGE` | 5 | PASS |
| MEO | `MEO_DATA_DIR` | 6 | PASS |
| Pure Mail | `PMAIL_TEST_ASSET_DIR` | 10 | PASS |
| Saya no Uta PT-BR | `PONSCRIPTER_TEST_DATA` | 1 | PASS |

## Real bugs fixed by the battery

- **Ren'Py VM**: post-transformation RPYC2 slots (`Translate`/`EndTranslate`)
  were ignored → zero dialogue in DDLC/Katawa retail scripts. Fixed in
  `games/doki-doki-literature-club/src/Script.cpp` and `modules/vms/renpy/src/Vm.cpp`.
- **RealLive**: `objDispRect` treated `w,h` as `x2,y2`, drawing the whole Key
  logo atlas; button frames and cursor strip were also wrong. Fixed in
  `games/reallive/src/RealLiveRuntime.cpp`.
- **N2System**: `CreateMovie` read the literal parameter name `"描画優先度"`
  as `asFloat()`→0, leaving the movie behind the black background. Fixed in
  `modules/vms/n2system/src/NsbVm.cpp`.
- **Artemis**: `resolveUiImagePath` did not handle art IDs renamed between
  editions (`background_2a` → `background_trial.png`). Fixed in
  `modules/providers/artemis/src/ArtemisAssets.cpp`.
- **Zero**: race condition in the menu input test (fixed frame wait).
  Fixed in `games/zero-no-tsukaima/tests/ZeroEngineTest.cpp`.

## False positives eliminated

Before the audit, retail tests without data returned `0` and showed as
`Passed`. All now return `77` with `[SKIP]` and `SKIP_RETURN_CODE 77`.
Result without data: `Skipped`, never `Passed`.

## Known limitations

- `NEKOPARA_VOL0_PT-BR` — `data.xp3` SHA-256 `6384cd1a…`, 1186/1187 entries
  with flag `0x80000000`. Exhaustive analysis confirmed the blocker:
  `PackinOne.dll` is a TJS2 utility bundle (only `V2Link`/`V2Unlink`, no
  CryptoAPI imports, no XP3/crypt strings) — the real stream cipher lives in
  the engine EXE. A brute force of all 256 single-byte XOR keys in both orders,
  validated by Adler-32, verified only 1/1182 entries (the plaintext
  `startup.tjs`); the other 1181 remained encrypted. Derivations from the
  public SmileCrypt formula also failed. The loose `VNX+/` translation is
  playable and tested; 1:1 parity of the encrypted original requires the
  decrypted assets or runtime capture of the engine's filter on Windows.
- Windows cross-build from macOS: platform blocker (no MSVC / Windows SDK;
  MinGW links Mach-O libraries from Homebrew into libass).

## Original execution in CrossOver/Wine (2026-11-23)

A sweep ran 29 original EXEs in a CrossOver bottle (`Master`). Result:
**11 ran, 3 started with warnings, 11 failed, 4 blocked**. Full log and
screenshots in the temporary evidence directory (`crossover-logs/summary.txt`).

Notable results:
- ✅ Ran: AIR_PT-BR, Clannad_PT-BR, Imouto `MainSystem.exe`, Katawa Shoujo,
  LargePonPon `BOOT.exe`, MEO, NEKOPARA (menu, `PackinOne.dll` loaded),
  Saya no Uta (`ponscr.exe`), Snow Radish (both EXEs), Summer Radish `DISC1`.
- ❌ Steam-dependent titles (AIR Steam, NUKITASHI 1/2) fail with
  `SteamAPI_Init(): Could not determine Steam client install directory` when
  the Steam client is not logged in inside the bottle.
- 🔒 PureMail and Summer Radish 2 show DVD/CD check message boxes; Steins;Gate
  stops at the Steam login splash.
- Full route/longplay parity was **not** proven: no automated input driver was
  available, so comparisons reached only title/menu/first-scene landmarks.

## Platform builds (state verified in this session)

| Platform | Result |
|---|---|
| macOS CMake arm64 + amd64 | PASS |
| Xcode macOS + iOS (34 schemes) | PASS |
| WASM (14 targets, Asyncify 14/14) | PASS |
| Android (17 flavors, 16 KiB) | PASS |
| Windows cross x86/x64 (MinGW, mingw-dynamic) | PASS — 17 `.exe` each |
