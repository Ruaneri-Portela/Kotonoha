# Scene Inventory Audit — 2026-10-03

## Result

`PASS`

## Counts

| Inventory | Count |
|---|---:|
| Router SceneKeys | 1,857 |
| Physical ORS SceneKeys | 1,857 |

## Router-only SceneKeys

| Route | Scene | SceneKey | Outgoing transitions |
|---:|---:|---|---:|
| 12 | 0 | `03/03-B2-A00` | 2 |
| 15 | 31 | `03/03-KB-E00` | 2 |

These are the complete no-ORS RouteProc nodes for the audited script inventory.

## ORS-only scripts

- `01/01-00-OP2`
- `05/05-9O-B00`

These are physical ORS scripts that are not nodes in the recovered normal-New-Game router. They are not classified as routing-only and need separate semantic classification.

## Passed checks

- router_count
- ors_count
- router_unique
- ors_unique
- ors_parse_clean
- router_only_exactly_known_set
- ors_only_count
- router_only_all_have_outgoing

## Generated local report

`build/routing-verifier/scene-inventory-audit.json`

That JSON report is generated from the user's working checkout and should be treated as the machine-readable evidence for the audit run.
