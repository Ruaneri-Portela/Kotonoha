# School Days HQ — ORS Event Runtime Audit

Read-only G1 audit for the extracted School Days HQ `.ORS` corpus.

The scanner does not change runtime behavior. It inventories every event header actually present in the ORS tree and compares the corpus with the current Kotonoha parser/runtime baseline.

## Run

```powershell
cd D:\Dev\Kotonoha

git pull --ff-only origin research/ktrf-binary-v0.1-20261005

python .\tools\schooldays-ors-audit\audit_schooldays_ors.py `
  --ors-root "D:\Dev\Kotonoha\assets" `
  --out-dir ".\build\schooldays-ors-audit"
```

## Outputs

- `build/schooldays-ors-audit/SUMMARY.md` — compact command/status table.
- `build/schooldays-ors-audit/audit.json` — full reproducible audit, examples, malformed lines and per-file manifest.
- `build/schooldays-ors-audit/event_occurrences.csv` — one row per raw ORS event occurrence.

The report also records an aggregate corpus SHA-256 so repeated audits can prove whether they used the same ORS tree.

## G1 status vocabulary

- `validated`: behavior has an in-game validation gate.
- `partial`: code exists, but payload/lifetime/original-runtime semantics are not closed.
- `boundary-only`: currently used only for scene-boundary timing.
- `suspect`: current code likely does not express the event's intended semantic operation.
- `unsupported`: a real ORS header is not recognized by the parser.

G1 is complete only after the real corpus has been scanned and every real header has an explicit semantic disposition: validated, understood-but-partial with a concrete follow-up, or unsupported/unknown with evidence for research.
