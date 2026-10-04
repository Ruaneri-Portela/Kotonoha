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

