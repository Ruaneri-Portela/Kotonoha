# Routing Closure Roadmap

## A — DEV recovery instrumentation

Deliverables: compile-time DEV gate, `-k`, causal checkpoint `sd-ep3-r4-b00`, `DumpState()`, F6 dump, standalone checkpoint test, documentation.

Exit condition: checkpoint build opens at `02/02-2K-B00` and its next transition matches uninterrupted routing.

## B — quick regression suite

Formalize the existing 22-ending witness test as the fast regression command.

Exit condition: 22/22 witnesses pass with all expected transition IDs.

## C — exhaustive executable verification

Deliverable: `SchoolDaysRoutingVerifier --full`.

Exit condition: 22 endings, 1,856 reachable SceneKeys, 2,458 expected reachable transitions, no unexpected unresolved state, and all six known dead transitions still unreachable.

## D — full Android path

Resume through DEV checkpoint and continue `02 -> 03 -> 04 -> 05 -> ending`.

At every useful boundary save `KTN-ROUTER`, trigger `DumpState()`, record transition ID and SceneKey, and create a new DEV checkpoint when it saves substantial replay time.

## E — targeted mechanism smoke tests

Use checkpoints for timeout routing, same-episode route changes, convergences, BS/history predicates, 99x flags, global/callback behavior where reachable, and different ending families.

## F — certified graph generation

Generate full scene graph, episode map, route-table map, decision map, ending map, dead-branch overlay and machine-readable graph directly from verifier results.

## G — final report

Create `SCHOOLDAYS_ROUTING_VALIDATION_FINAL.md` with router commit, verifier commit, toolchain, exact baselines, witness result, exhaustive result, device result, known exclusions and hashes of generated reports.

## H — KTRF

Only after the final report is frozen: specify KTRF v0.001, compile the recovered model to `.ktroute`, implement the read-only runtime, run differential verification, require zero divergences, then consider migration from generated `.inc` data.
