# Route Map: Open/Close hover and imported-save node state (2026-11-19)

## Reproduced visual defect and narrow fix

The user reported both toolbar buttons permanently blue in their Windows
`SchoolDaysHQ-Windows-DIST-Integrated-Final-20261119` screenshots:

- **LOAD → Open Route Map**: the original `System.gpk` resource
  `SAVELOAD/LOAD.PNG` already draws a neutral gray button and legible label.
  The separate `SAVELOAD/SAVELOAD_CHIP.PNG` region `(1,1,194,38)` is the blue
  overlay. `SchoolDaysSaveLoadRenderPlan::Build` previously drew that overlay
  whenever LOAD was active, regardless of pointer selection.
- **Route Map → Close Route Map**: each of the 16 original
  `ROUTEMAP/*/*.PNG` bases already draws a neutral button; common record 6
  of the corresponding `*_CHIP.PNG` is its blue overlay.
  `SchoolDaysRouteMapRenderPlan::Build` previously drew record 6 in every
  frame independent of selection.

The fix emits the blue `Role::RouteMap` only when the actual LOAD CMAP
control **21** is selected, and blue RouteMap common record 6 only when
RouteMap control **6** is selected. A returning RouteMap used to restore
LOAD selection 21 blindly after CLOSE or Escape even if the pointer had
moved elsewhere. `SchoolDaysSaveLoadModel::RestoreSelection` now clears
that stale selection. `SchoolDaysSystemUi::HandleEvent` also clears the
selection when the pointer leaves the game window, which might not produce
a later mouse-motion event inside the window.

No underlying art, colors, GPKs or `.ktnroute` were edited. Other Route Map
node and navigation records were not changed, as their original runtime
visibility semantics are separate from the proved toolbar defect.

## Observed data from the user's imported save — READ ONLY

Files examined under the explicitly identified Windows distribution and an
older private distribution. Only original `GlobalFlag.DAT` and save-slot
`SaveFile*.DAT` bytes were **read**, using the exact FlgH and SLog varint,
UTF-16 XOR, tag and length rules in `SchoolDaysPersistenceCodec.cpp`.
No save was written, copied, repaired or injected into the game.

| Observation | Prior clean distribution | Current integrated distribution |
| --- | ---: | ---: |
| `GlobalFlag.DAT` parsed entries | 250 | 252 |
| SceneKeys marked read | 213 | 215 |
| Native progress `floor(read×100/1825)` | 11% | 11% |
| Nonzero checkpoint unlock markers | 8 | 8 |
| Episode 1 markers | `SP100`–`SP103` | Same |
| Episode 2 markers | `SP200`, `SP201`, `SP204`, `SP205` | Same |
| Episode 3–6 SP markers | **0** | **0** |
| SP tag-3 records in all 11 save slots | Only those eight early SP IDs | Same |

The latest `GlobalFlag.DAT` had SHA-256
`0D22EA15144C55FD9B822D5D706A3D728A91A33E474202508A507E37C6E1C705`
at inspection time. The parser verified bounded input and complete EOF.
The older version's flag file began with SHA-256 `9EA2B5FF`.

Several later slots demonstrably have Route/Scene coordinates already in
Episodes 3–6, including an ending, whose normal BackRules identities would
correspond to SP408, SP501, SP603 and SP616. **Those SP markers and tag-3
checkpoints are absent from the imported files.** Thus the current visual
state (early nodes marked, later nodes black despite 11%) is a faithful
projection of the available saved flags, *not* a Route Map renderer defect.
The original retail executable will also show black nodes with those same
flags, independently of what the player remembers reaching.

This proves **incomplete checkpoint history in the imported save**, not its
historical cause. Plausible but unproved explanations are incomplete hook
recording in an older custom build, a jump/replay path bypassing callbacks,
or an incomplete import/restore. The current KTRF session records checkpoints
on `overflow.sdhq:callback_00`, and the renderer projects only the persisted
nonzero SP entries via `SchoolDaysPersistence::GlobalIntegerIsSet`.

**Do not automatically unlock nodes** by counting read scenes or by scanning
the furthest SaveFile route. A valid tag-3 checkpoint includes a specific
saved state, and guessing it would risk incorrect choices, Feel, and routes.
For a future root-cause test, start a **fresh isolated `--documents` profile**
and play through one episode-3 callback, then compare the newly generated
GlobalFlag SP key and SLog tag-3 record before/after; never overwrite or
experiment on the user's imported files.

## Targeted tests and manual QA

`SchoolDays.RouteMapMatrix` now checks both button highlights at rest, on
pointer enter, on exit, and stale LOAD selection after RouteMap return across
all four render variants, in addition to the established 16-page / 161-SP
matrix. The DEV `SchoolDaysSaveTest` additionally exercises actual mouse
motion and SDL window mouse-leave with retail artwork while writing only a
new temporary test document profile. DIST build retains the negative contract
`SchoolDays.DistDebugAbsence`.

Real Windows/OpenGL screenshot confirmation is still needed for both buttons
and for all episode page backgrounds. The imported-save black-node behavior
is already grounded in the actual read-only save data; a fresh controlled
checkpoint recording experiment is needed only to explain why the old build
did not produce later SP records.
