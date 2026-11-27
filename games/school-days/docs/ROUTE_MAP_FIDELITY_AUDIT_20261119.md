# School Days HQ v1.02 — Route Map fidelity audit (2026-11-19)

## Verdict

**The original Route Map's static assets and clickable geometry are mapped;
100% functional parity with the retail executable is NOT yet certified.**

This was a **read-only** examination of the user's original `System.gpk`,
`Script.GPK`, KTRF and the Kotonoha Route Map model/oracle/renderer. No retail
executable or installer was run, no commercial archive was modified, and no
real save was read or changed as part of the audit.

## Proven static correspondence

| Check | Evidence | Result |
| --- | --- | --- |
| Route Map pages | 6 episodes: pages `[1,1,2,4,4,4]` | **16/16** |
| Retail Route Map assets | 16 PNG bases + 16 PNG chips + 64 CMAP maps | **96/96**, source `System.gpk` |
| Screen variants | Standard 800×600, Wide 800×450, Wide Full 1280×720, Wide Note 1024×576 | **64/64 CMAP** dimensions verified; 32 PNG dimensions agree |
| Node hotspots | Per episode `[4,9,11,35,57,45]` | **161/161**, matches Kotonoha model, all CMAP IDs contiguous |
| Selector destinations | 162 BackRules SP mapping records cover 161 distinct selector IDs: `100..103`, `200..208`, `300..310`, `400..434`, `500..556`, `600..644` | All point to physical KTRF nodes; 161 distinct selectors accounted for |
| Oracle | ROUTE IDs `0..54`, six SP fallback groups, 12 `GetRouteMapPage` test examples | Implemented; no exhaustive independent click-by-click retail comparison |

`SP428` intentionally/possibly has two BackRules records at `ROUTE15`,
`SCENE85` and `SCENE108`. This requires a saved-history/re-entry test: two
entries for a single selector do not establish which checkpoint the original
would prefer in each gameplay state.

## Story corpus and coverage gaps

- `Script.GPK` from the user's original game: **1,859 entries**, of which
  **1,857 English `.ORS`** and 2 incidental `.DS_STORE` entries.
- KTRF: 14 sections verified by their content SHA and per-section CRC;
  **1,857 NODE**, **2,458 TRAN**, **287 CHOI/485 options**, **22 ENDG**.
- Of KTRF's 1,857 nodes, **1,855** have physical scene resources. Two nodes,
  `03/03-B2-A00` and `03/03-KB-E00`, are synthetic dispatchers without an ORS
  resource of their own. This appears structurally intentional.
- Two retail ORS have no same-named KTRF scene:
  `01/01-00-OP2` and `05/05-9O-B00`. It is unproven whether either is used by
  the original game's active narrative routes. Thus direct physical resource
  correspondence is **1,855/1,857 = ~99.892%**, not 100%.
- Ignoring predicates, reachability by the declared KTRF transitions from
  `00/00-00-A00` covers **1,856/1,857** KTRF NODE. The one unreachable physical
  scene, `04/04-C0-A05`, has no transition in or out; it might be an unused
  legacy asset, but that requires independent proof. All 161 SP hotspots have
  physically present, statically reachable destinations.

## Meaning of the 11% screen

The implementation excludes 32 `-OP1` entries from the 1,857-node progress
denominator, leaving **1,825** countable scenes. It applies
`floor(readScenes × 100 / 1825)`. Consequently, the screenshot showing
**11% Complete** corresponds to roughly **201–218 read scenes** under this
algorithm, not 11% of the 161 Route Map hotspots. This arithmetic has not
been independently compared with the retail screen for the same save.

## Behavior not yet independently proven

1. **Activating every Route Map destination:** no exhaustive 161-selector
   retail-vs-Kotonoha click/transition comparison exists. In particular,
   distinguish a SaveLog tag-3 checkpoint, a GlobalFlag-only blue unlock,
   restored saves and current gameplay. Kotonoha does not jump to a blue node
   that has no saved tag-3 checkpoint; a subsequent read-only disassembly of
   the retail EXE confirmed the same **no-jump** rule through its tag-3 lookup,
   while exact resulting frame/animation parity remains untested.
2. **Visual state and hover equivalence:** known reference captures cover
   0%/100%, Episodes 1/2 and a subset of tab hover states. The behavior of
   band0 hover priority, band1 global unlock, band2 gameplay marker, band3
   click action, and the `alternateHost=false` runtime path lacks a complete
   original-reference matrix.
3. **Graph anomalies:** classify the two retail-only ORS and the isolated
   `04/04-C0-A05` as deliberately unused, virtual aliases, or missing graph
   links by an independent original trace, rather than modifying KTRF based
   on an assumption.
4. **Coverage tests:** exercise 55 ROUTE values, 161 selectors, SP428's two
   records, all four CMAP variants and 0%/11%/100% percentage boundaries
   including read counts 200, 201, 218 and 219. Synthetic tests can prove
   internal invariants, but cannot alone prove retail fidelity.

The earlier repository audits `MENU_ROUTE_MAP_AUDIT` and
`MENU_SFX_AND_ROUTE_MAP_HOVER` document the narrower original-reference
coverage. **Do not label the Route Map '100% faithful' until these behavior
checks are witnessed or independently verified.**

## Follow-up implementation and verification (same date)

**New original-source evidence:** the 55-byte `_GetRouteMapPage@8` route case
table at `RouteProcSDHQ.dll` VA `0x1000C350` matched the program's
`kPageCaseByRoute` **55/55**. The six episode groups and 12 jump-table arms
were also located in original code by read-only analysis. The retail EXE's
selector >=100 handler performs a tag-3 `SP` checkpoint lookup and aborts the
handoff when that checkpoint is absent: Kotonoha's GlobalFlag-only blue node
does not jump either. This independently resolves the missing-tag3 question,
but not the exact original graphical handoff frames.

**Further native oracle verification:** the separate `_CheckScript@8` jump
table contains **55 distinct ROUTE helpers**; all 55 call the SP fallback
group selected in `kRouteRules`. The six native fallback functions reference
all **161 SP strings** in exactly the descending order implemented by the
Kotonoha fallback search. The native `_GetStory@4` route groups have lengths
`[1,3,2,13,13,23]`, matching the 55-route partition in `NativeStoryFor`.
Individual `SCENE` override conditions have not been exhaustively checked
against original executable traces.

**Original completion set independently authenticated:** a subsequent
read-only extraction of `_GetReadScriptCount@4` at RouteProcSDHQ.dll VA
`0x1000A350` enumerated **1,825 distinct UTF-16 SceneKeys** from 55 arms.
Their set matches **exactly 1,825/1,825** KTRF NODE IDs after excluding 32
`-OP1` keys. Crucially, the original counter **does include** the two
synthetic dispatchers `03/03-B2-A00`, `03/03-KB-E00` and **excludes** the
retail-only physical ORS `01/01-00-OP2`, `05/05-9O-B00`. A proposed change to
replace dispatcher flags with original-only ORS flags was thus **rejected and
reverted before commit**, preserving the retail-faithful `ComputeCompletion`
implementation and the original **1,825** denominator. The mismatch between
physical playable scenes and read-flag counter keys still requires gameplay
validation of how/when the original sets those two dispatcher flags. Nothing
was changed in the KTRF or the user's GlobalFlag files.

**Added regression checks:** `SchoolDays.RouteMapMatrix` generates synthetic
CMAPs for all **16×4 = 64** page/variant combinations and exercises every
node selector in each variant (**161×4 = 644** cases), lock/unlock gating,
SP key derivation, control bounds and navigation. The extended
`SchoolDaysSessionTest` validates that progress **counts** both native
dispatcher flags but **does not count** the two original-only ORS flags;
validates the 200/201/218/219/1825
percent boundaries; and confirms `SP428`'s two authenticated coordinates
produce one first-visit tag-3 checkpoint in **both visit orders**, not
duplicate records. These are
**internal regression proofs** grounded in an independently inventoried
retail mapping, not independent retail click-by-click playback.

| Area | Implemented | Independently verified | Still unproven |
| --- | --- | --- | --- |
| Pages, 4 skins, hotspots | 16 pages, 96 assets, 161 nodes | All original CMAP/PNG counts and dimensions, 161/161 IDs | Per-pixel animated state/hover on a real GPU |
| ROUTE dispatch and marker | 55 route cases, six descending SP fallback groups, 12 page-case arms | 55/55 page bytes, 55/55 CheckScript fallback dispatches, 161/161 descending original SP strings and six GetStory groups | Every SCENE override branch in live gameplay |
| Selector / checkpoint | 161 SPs; tag-3 first-visit snapshot, SP428 duplicate coordinates | 162 BackRules mappings to physical nodes; 161×4 synthetic activations, SP428 internal test | Full 161 original click/handoff sequences, imported save combinations |
| GlobalFlag-only unlock | Blue/available state, no jump without tag3 | Original EXE static tag3 failure branch matches no-jump | Every visual branch after a successful tag3 handoff |
| Script graph | 1855 physical nodes + 2 dispatchers, 2458 transitions | Corpus sets, 1855/1857 physical overlap; SP targets statically reachable | 2 original-only ORS usage, orphan `04/04-C0-A05`, all predicate-dependent paths |
| Choices and endings | 287 choices / 485 options / 22 ending codes | KTRF structural counts, previous directed session tests | Original path traversal of all choices/endings and seven L×H extensions |
| Completion | Exact 1825 native keys; dispatchers counted, two original-only ORS excluded | 1825/1825 exact original DLL UTF-16 string-set match, plus synthetic boundary tests | Source and timing of dispatcher read flags, 100% on an actual retail-vs-Kotonoha playthrough |
