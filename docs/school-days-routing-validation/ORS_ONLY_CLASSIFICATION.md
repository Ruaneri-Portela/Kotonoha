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
