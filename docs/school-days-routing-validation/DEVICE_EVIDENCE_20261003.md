# Android Device Evidence — 2026-10-03

## Purpose

The device test verifies that ORS execution, prompt results, router state, SceneKey lookup, Gameplay switching and media execution are connected correctly on the real Android runtime.

## First confirmed episode crossing

Observed:

    ROUTE=1
    SCENE=0
    next t25 -> 01/01-00-A00

This proves episode prefix `00 -> 01` through the full router and real engine integration.

## Same-episode route change

Observed later:

    ROUTE=2
    SCENE=0
    next t189 -> 01/01-1K-A00

Route table changes and episode-prefix changes are separate concepts. Episode 2 still uses prefix `01` while Route 1, Route 2 and Route 3 can participate.

## Second confirmed episode crossing

Observed:

    ROUTE=4
    SCENE=0
    next t240 -> 02/02-2K-A00

This proves episode prefix `01 -> 02` and entry into Route 4.

The same uninterrupted process continued:

    ROUTE=4
    SCENE=92
    next t262 -> 02/02-2K-OP1

    ROUTE=4
    SCENE=1
    next t393 -> 02/02-2K-B00

The app process was later lost, which motivated the DEV checkpoint system.

## State mutation evidence

Examples observed on-device:

    scene=01/01-00-B00 choice=0
    001=5
    feeling 001 += 5 -> 5

    scene=01/01-00-C00 choice=0
    002=15
    feeling 002 += 6 -> 15

    scene=01/01-1K-C00 choice=0
    002=47
    feeling 002 += 5 -> 47
    991=1

This is evidence that the engine is not merely walking a static scene list.

## Remaining device closure path

`02 -> 03 -> 04 -> 05 -> terminal ending`.

One complete device path is enough for end-to-end integration confidence. Exhaustive combinatorial coverage belongs to the automated verifier.

## Third confirmed episode crossing

Observed after resuming from the validated DEV checkpoint:

```text
ROUTE=15
SCENE=0
next t322 -> 03/03-KB-A00
```

Interpretation:

- episode prefix changed from `02` to `03`;
- the router entered Route 15;
- the engine resolved and opened `03/03-KB-A00` on the real Android runtime.

Device closure progress is now:

```text
00 -> 01  PASS
01 -> 02  PASS
02 -> 03  PASS
03 -> 04  pending
04 -> 05  pending
05 -> ending pending
```

