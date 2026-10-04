# ORS-only Classification

The scene inventory audit proved that two physical ORS scripts are absent from the recovered normal-New-Game router:

- `01/01-00-OP2`
- `05/05-9O-B00`

This does **not** by itself prove whether they are special-mode scripts, direct-load scripts, legacy/dead assets, or otherwise outside normal RouteProc control.

## Classification procedure

`tools/schooldays-routing-verifier/classify_ors_only.py` performs a structural, non-redistributive audit on the local script corpus.

It records only:

- SHA-256 and file size;
- event/command counts;
- presence/count of `Next`, `SetSELECT`, `PrintText`, `PlayVoice`, `PlayMovie`, and `EndRoll`;
- unknown command names, without command payloads;
- literal references to each ORS-only SceneKey/basename across all physical ORS scripts;
- literal references in repository source files;
- neighboring ORS filenames.

It intentionally does not print dialogue or media payload strings.

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\schooldays-routing-verifier\run_ors_only_classification.ps1
```

Output:

`build/routing-verifier/ors-only-classification.json`

## Interpretation rule

A missing normal-New-Game router node plus no literal references is evidence that a script is outside ordinary RouteProc reachability, but it is **not** by itself proof that the asset is globally unused. Non-literal/direct indexed loading or another game mode must still be ruled out from engine/binary evidence before calling an asset dead.


## 2026-10-03 local classification result

The local structural classifier was run against the full working script corpus.

### `01/01-00-OP2`

Observed structure:

- 145 bytes, 7 lines, 4 parsed events;
- commands: `PlayMovie`, `PlaySe`, `SkipFRAME`, `Next`;
- no `SetSELECT`, `PrintText`, `PlayVoice`, or `EndRoll`;
- no literal references from any other ORS;
- repository references found by the classifier are research/index/documentation artifacts and are not runtime evidence.

Status: **unresolved**. It is outside normal-New-Game RouteProc reachability, but direct/special loading versus legacy/orphan status is not yet proven.

### `05/05-9O-B00`

Observed structure:

- 3,643 bytes, 100 lines, 49 parsed events;
- commands: 46 `PlayVoice`, 1 `PrintText`, 1 `SkipFRAME`, 1 `Next`;
- literal reference from physical script `05/05-9O-A00`, line 221.

Status: **not orphan**. The direct reference from another physical ORS proves that it participates in some script-level relationship outside the recovered normal-New-Game RouteProc node set. The exact command at `05-9O-A00:221` must be identified and its runtime semantics proven before Android validation resumes.



## Correction: media-path false positive

The first classifier reported a literal ORS reference from `05/05-9O-A00` to `05/05-9O-B00`.

Manual inspection proved that this was **not** script control flow. The matching line is a `PlayVoice` event whose voice asset path contains the directory name `05-9O-B00`.

Therefore:

- `05/05-9O-B00` is **not proven referenced as a script** by that line;
- the first classifier's generic "literal-reference-found" label was too broad;
- media-path mentions must be separated from script/control-flow references.

A corrected classifier, `classify_ors_only_v2.py`, now separates media-path mentions from non-media/control-like references.

### Opening-script evidence

Manual comparison also established:

`01/01-00-OP1`

- `BGM/Vocal/SDV01`
- `System/OP/SDHQ_SEKAI`

`01/01-00-OP2`

- `BGM/Vocal/SDV03`
- `System/OP/SDHQ_SETSUNA`

This is strong direct evidence that `OP2` is a second opening variant (Setsuna), parallel to the RouteProc-visible `OP1` Sekai opening. Its absence from the 55 recovered normal-New-Game route tables therefore points to opening-selection logic outside the ordinary RouteProc scene-node graph, rather than an arbitrary orphan-script hypothesis.

## 2026-10-03 corrected classifier v2 result

The corrected classifier was run locally.

```text
01/01-00-OP2
  media-path mentions: 0
  non-media/control-like mentions: 0
  literal control-flow reference: NO

05/05-9O-B00
  media-path mentions: 1
    05/05-9O-A00:221 [PlayVoice]
  non-media/control-like mentions: 0
  literal control-flow reference: NO
```

This formally removes the earlier false implication that `05/05-9O-A00` script-calls `05/05-9O-B00`.

### Additional voice-overlap test

A direct comparison of the voice asset paths used by `05/05-9O-A00` and `05/05-9O-B00` produced:

```text
A00 unique voice paths: 37
B00 unique voice paths: 46
B00 paths also used by A00: 1
B00 paths not used by A00: 45
```

Therefore `05/05-9O-B00` is not a simple duplicate whose voice content was folded into `05/05-9O-A00`. It contains a largely distinct 46-voice sequence and remains a real semantic-mapping problem.

Current status:

- `01/01-00-OP2`: opening variant identified, selector still unresolved;
- `05/05-9O-B00`: distinct physical scene content, no literal script-control reference found, loader/reachability still unresolved.



## Normal-New-Game reachability closure

Additional executable-router inspection closes the two ORS-only cases for the current normal-New-Game scope.

### `01/01-00-OP2`

The DLL contains a special Route-0 PV selector for `PV/SEKAI-OP`, `PV/KOTONOHA-OP`, and `PV/SETUNA-OP`, but that path is guarded by the Route-0 special callback/mode condition and is excluded by the normal-New-Game `callback34 = 0` assumptions.

No evidence connects that special `PV/SETUNA-OP` identifier to the physical `01/01-00-OP2.ENG.ORS` during normal New Game.

Classification for this project scope: **physical alternate opening asset, unreachable from the recovered normal-New-Game RouteProc graph**.

### `05/05-9O-B00`

The recovered graph gives a stronger proof:

- Route 36 contains exactly one node: `05/05-9O-A00`;
- the only incoming transition to Route 36 / Scene 0 is `t1850` from `05/05-5O-D10`;
- `t1850` registers Ending 4 and sets `ROUTE=36, SCENE=0`;
- `05/05-9O-A00` has one outgoing transition, `t1936`, which is terminal and invokes `callback_38`;
- `05/05-9O-B00` has no RouteProc node, no incoming generated transition, no literal ORS control-flow reference, and no matching RouteProc DLL scene string in the inspected evidence.

Its active/backup ORS copies and historical JRS prove that the content was retained, not that it is reachable.

Classification for this project scope: **retained physical script, unreachable from the recovered normal-New-Game RouteProc graph**.

This does not claim global deadness across every replay/trial/debug/edition-specific loader path.
