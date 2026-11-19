# School Days HQ v1.02 / Kotonoha v3 — Windows final integration review

**Local private QA only. No remote publication and no retail executable run.**

## Recovery and preserved baseline

- Starting clean worktree: `D:\Dev\_worktrees\school-days-menubar-cursor-20261119`, branch `fix/school-days-dist-backlog-20261119`, HEAD `2e02f9b9d392e7492e5e042982b218baf8e8e672`.
- Its direct ancestors already implement JSON/GPK language presets (`9181cc5d`), a mouse cursor that remains visible while stationary over the MenuBar (`32af5e4e`), and the compiled-out generic dialogue-history fallback (`2e02f9b9`).
- No staged/unstaged changes were present on resumption; prior DEV 9/9 and DIST 7/7 targeted test results were re-used rather than rerun wholesale.
- DEV build configuration: `build/cursor-qa`, `KOTONOHA_DISTRIBUTION_BUILD=OFF`. DIST: `build/json-dist-qa`, `KOTONOHA_DISTRIBUTION_BUILD=ON`, `KOTONOHA_DEV_CONTROL=OFF`.
- Kept prior DIST releases, original 29 retail GPKs, translation files, seven L×H assets, original commercial software and all real saves/backup files unchanged. `game-assets/school-days-hq.ktnroute` SHA-256 `557830833AA3BB4A8EFBA82FE75964211491E9F02D60D6090DDA1D5174D4E745` is unchanged.

## Fixed production defects

**DIST contract:** The School Days `Runtime` has no generic `VNBacklog` fallback member, registration, update, rendering, or event entry point when `KOTONOHA_DISTRIBUTION_BUILD` is set. A failed native BackLog explicitly reports an error instead of showing `Dialogue history`. This is a compile-time rule, not a cosmetic toggle; `SchoolDays.DistDebugAbsence` asserts it. The DEV fallback remains available. No changes to generic `lib/` were necessary.

**Native BackLog:** The previous work already fitted TTF runs proportionally (`FitTextRaster`), wrapped translated lines by actual FONTDATA advance, measured height at 57px line steps, and clipped text away from x>=1180 source pixels (right controls) and y>=640 (bottom blue band) in a 1280×720 source surface. This iteration addressed two additional regressions:

1. An entry with more than nine visible rows was clipped and could never reveal its beginning/end. The model now stores an **intra-entry line offset**, moves through hidden rows using the existing Up/Down controls, wheel, buttons and Android gesture bridge, and changes the current history entry only at the edge. Ordinary short entries keep their original one-control-to-one-entry navigation.
2. The literal Chinese punctuation `｜` (U+FF5C) and `《` (U+300A) invalidated the native text plan and made the DIST close the BackLog. Localized BackLog content now renders those characters as ordinary SDL_ttf glyphs. The strict original-English `WrapRetailEnglish` helper remains unchanged.

**Route Map completion:** The KTRF contains 1855 physical resource scenes and two synthetic dispatchers, while the original retail Script.GPK has 1857 physical English ORS. A deeper disassembly of the original RouteProcSDHQ.dll enumerated all **1825 distinct SceneKeys** used by the authentic read-count oracle. The prior `ComputeCompletion` was **already 1825/1825 correct**: the original counts the two dispatchers and does not count the two retail-only ORS. An experimental replacement of those keys was **reverted before commit**, avoiding a regression. No transition, read flag, denominator, KTRF or real save was modified; when and how dispatcher read flags are set in live retail gameplay remains an open coverage question.

## Route Map evidence matrix

| Subject | Implemented | Original-source verification | Remaining validation |
| --- | --- | --- | --- |
| Assets, pages, CMAP | Six episodes, 16 pages, four variants, 161 nodes | Original `System.gpk`: 96/96 assets, 64/64 CMAP, 161/161 hotspots, all dimensions match | Pixel/hover comparison on real GPU, all visual states |
| ROUTE dispatch | 55 ROUTE, six groups, 12 page-case arms | Original DLL tables: GetRouteMapPage 55/55, CheckScript SP fallback dispatch 55/55, six GetStory groups and 161 descending SP strings | Exhaustive independent proof of each SCENE override and runtime conditions |
| Selector mapping | 161 unique SP IDs over 162 original mapping rows | 161 selectors to physical KTRF resources; all statically reachable; new synthetic 161×4 activation matrix PASS | Every original player-facing click/handoff sequence |
| SP428 | Two ROUTE15 records (SCENE85 / SCENE108), first SP checkpoint retained on revisit | New synthetic session tests confirm one first-visit tag3 in both orders, no duplicate; native CheckScript=39 for both | Original saved-history preference on both route visits |
| Unlocking | GlobalFlag unlock for blue node, tag3 SaveLog required for activation | Read-only retail EXE disassembly confirms missing tag3 prevents jump, matching current implementation | Intermediate visual transitions and hover priority |
| Graph, decisions, endings | 1857 KTRF NODE, 2458 TRAN, 287 CHOI/485 options, 22 ENDG | 1855/1857 physical ORS mapping; every SP has reachable destination ignoring predicates | Two retail-only ORS, orphan `04/04-C0-A05`, all conditional branch and ending playthroughs |
| Completion | 1825 original SceneKey read flags; original formula preserved | Native DLL 1825/1825 exact key-set match to KTRF excluding 32 `-OP1`; synthetic tests count dispatcher flags and exclude ORS-only flags, plus 200/201/218/219/1825 percent thresholds PASS | How dispatcher read flags are earned; full 100% gameplay and 11% screenshot comparison with matching retail save |

**Verdict:** geometry and static routing coverage are strongly verified, but
**100% retail parity is not proven**. Do not claim all decisions, ending states,
transitions or checkpoint handoffs have been compared one by one. The same
Route Map corpus remains available for future Shiny Days / Cross Days work.

See `ROUTE_MAP_FIDELITY_AUDIT_20261119.md` for source inventories and original
functions/addresses.

## Targeted validation

- Previously validated on unchanged baseline: DEV Release **9/9**, DIST Release **7/7** focused tests, mouse/MenuBar and launcher among them.
- Newly built DEV targeted tests: `SchoolDays.BackLogGeometry`, `SchoolDays.RouteMapMatrix`, and `SchoolDaysSessionTest` (with prerequisite synthetic PortTest) PASS.
- Newly built DIST Release: `SchoolDays.BackLogGeometry`, `SchoolDays.RouteMapMatrix`, `SchoolDaysSessionTest`, `SchoolDays.DistDebugAbsence`, with synthetic PortTest prerequisite, **5/5 PASS**.
- The new Route Map matrix creates all 64 synthetic CMAP files in the build output; no retail GPK is copied or modified. It exercises all 161 node selectors under every variant, locking, unlocking, SP naming, bounds and navigation.
- The expanded BackLog geometry test covers text clips, normal and huge rows, proportional raster fit, Chinese full-width punctuation, and scrolling to the first and last part of a 600-character message on four variants.
- DEV and DIST player executables are built in the existing Windows Release trees. Windows OpenGL is the default for manual QA; SDL software/dummy is only for synthetic one-frame probes.

## Reproducible manual OpenGL QA (still required)

Use the **isolated private DEV/DIST QA launchers**, never run the retail EXE or
installer. The launcher wrappers set `--documents` to an isolated directory
and pass `--renderer opengl` by default; retail GPKs stay at the player's
external installation. Avoid real saves or profiles.

1. Enter an ordinary PT-BR scene, hover MenuBar for >3 seconds without moving
   the mouse, confirm native cursor stays visible. On empty narrative area it
   should auto-hide after ~3 seconds and return on physical motion.
2. Open native **BackLog** from gameplay. Confirm the generic `Dialogue
   history` panel is impossible to reach in the DIST, including when the
   native font/skin is unavailable; a clear failure should be shown instead.
3. Inspect the original screenshot's long Kotonoha statement and other
   dialogue: no flattened glyphs, no overlap with the right-side scroll
   arrows, no text across the bottom blue stripe. Verify UTF-8 accents,
   speaker labels/order, scroll, Up/Down, arrows, wheel, Escape/Back.
4. Repeat in Standard 4:3, Wide, Wide Full and Wide Note, and at windowed and
   borderless desktop sizes; look for clipping at every edge. Synthetic
   600-character tests exercise long within-entry scrolling even if no retail
   sentence has so many rows.
5. Try JSON and GPK PT-BR, Russian, Chinese and Spanish; look at literal
   `｜` and `《` in Chinese BackLog. Confirm the launcher shows language names
   without filename extensions and no "Custom JSON" option; switching
   does not silently fall back to English.
6. Verify Route Map Episodes 1–6, all 16 pages, arrows, percent indicator,
   locked/blue/current nodes. A GlobalFlag-only blue node without tag3 should
   not jump; a saved tag3 checkpoint should. Pay particular attention to
   SP428 and a restored save, while never altering personal saves.
7. Spot-check SetSELECT/Feel/AUTO, skip/back, Replay, Save/Load and L×H
   epilogue handoffs under the isolated profile before approving final Windows
   distribution. These systems were not edited by this iteration.

**Limitation:** No original executable or GPU-backed retail-vs-Kotonoha visual
playthrough was run in the assistant's test process. Geometry/renderer tests
and synthetic software initialization do not certify actual visual parity.
No distribution is authorized for public redistribution of commercial assets.
