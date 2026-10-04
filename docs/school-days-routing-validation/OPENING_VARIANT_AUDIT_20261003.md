# Opening Variant Audit — 2026-10-03

## Physical ORS inventory

The local script corpus contains 33 opening ORS files matching `*-OP*.ENG.ORS`.

Recovered normal-New-Game router coverage:

- 32 `OP1` SceneKeys are present in `SchoolDaysRouteData.generated.inc`;
- 0 `OP2` SceneKeys are present in the router;
- the only physical `OP2` is `01/01-00-OP2`.

Therefore the opening-script inventory itself explains one of the two ORS-only set-difference entries.

## Opening media variants

Structural inspection of the 33 opening ORS files gives:

| Variant | Physical ORS count | Movie | Audio |
|---|---:|---|---|
| Sekai | 17 | `System/OP/SDHQ_SEKAI` | `BGM/Vocal/SDV01` |
| Kotonoha | 14 | `System/OP/SDHQ_KOTONOHA` | `BGM/Vocal/SDV02` |
| Setsuna | 2 | `System/OP/SDHQ_SETSUNA` | `BGM/Vocal/SDV03` |

The two Setsuna-bearing ORS files are:

- `01/01-00-OP2`;
- `03/03-KA-OP1`.

This proves that the suffix `OP2` is **not** a generic marker for "the Setsuna opening". The same Setsuna opening is also used by an ordinary RouteProc-visible `OP1` node at `03/03-KA-OP1`.

What is proven about `01/01-00-OP2` is narrower and stronger:

- it is a real opening script;
- it is structurally parallel to `01/01-00-OP1`;
- `01/01-00-OP1` plays the Sekai opening;
- `01/01-00-OP2` plays the Setsuna opening;
- only `OP1` exists as the RouteProc node for the `01-00` opening position.

## Remaining question

The unresolved point is **selection semantics**.

The current recovered 55-table normal-New-Game router contains `01/01-00-OP1` but not `01/01-00-OP2`. Therefore one of the following must be proven before final closure:

1. another subsystem substitutes `OP2` for the logical `OP1` opening slot under some state;
2. `OP2` is directly loaded by a non-RouteProc mechanism;
3. `OP2` is retained but unused in this edition/path.

The media identity alone does not choose between those cases.

## 05/05-9O-B00 correction

A previous generic literal-reference scan reported `05/05-9O-A00 -> 05/05-9O-B00`.

Manual inspection proved that the matching line is a `PlayVoice` event whose **voice asset path** contains `05-9O-B00`. It is not an ORS control-flow reference.

Any future ORS-to-ORS reference audit must classify references by command semantics rather than basename coincidence.
