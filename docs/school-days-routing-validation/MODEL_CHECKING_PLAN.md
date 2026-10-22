# SchoolDaysRoutingVerifier — Model-Checking Plan

## Goal

Build a standalone verifier that exercises the real `SchoolDaysRouter` without SDL, video, audio or Android. Manual gameplay verifies integration; the verifier owns combinatorial coverage.

## quick mode

Re-run the 22 known ending witnesses. At every step check expected route, expected scene, optional choice or timeout injection, selected transition ID, resulting destination, and terminal ending.

This is the fast regression gate.

## full mode

Explore all reachable normal-New-Game states.

For each state:

1. identify current SceneKey;
2. determine whether a choice is expected;
3. fork one child state per valid choice result plus timeout when accepted;
4. apply `AcceptChoice()`;
5. execute `ResolveNext()`;
6. record transition ID and destination;
7. canonicalize the resulting state;
8. skip exact states already visited;
9. stop that branch at a terminal ending.

## Exact state identity

The state key must include every value that can affect future routing: route, scene, choiceResult, session variables, global variables, callback34, feelingApplied and ending registrations unless a documented liveness analysis proves a component irrelevant.

The historical 42,404,396 figure is a projected-state result. A raw C++ verifier count must not be compared blindly unless projection semantics are identical.

## Result sets

Track SceneKeys reached, transition IDs selected, endings reached, unresolved states, invalid destinations and canonical states independently. This allows graph-level proof even while state projection is being engineered.

## Required report

Expected graph-level closure:

    route tables:           55
    structural SceneKeys:   1857
    reachable SceneKeys:    1856
    reachable transitions:  2458
    reachable endings:      22
    unresolved reachable:   0
    invalid destinations:   0

Known dead transitions must remain unreachable.

## Witness export

The first discovered route to each ending should be exported in machine-readable form with SceneKey, choice, transition ID and destination for every step.

## Differential mode for KTRF

Once a `.ktroute` backend exists, add a differential mode. For every explored input state compare choice acceptance, state effects, transition ID, destination, terminal flag and ending registration against the generated C++ router.

Zero semantic divergences is the acceptance criterion for migration.

## Memory caution

A naive `std::map` snapshot for tens of millions of states can consume excessive memory. Before claiming exhaustive C++ verification, use a compact exact canonical representation, a formally documented state projection, disk-backed exact storage, or an equivalent graph exploration whose equivalence is demonstrated.

Do not replace exact state identity with a single collision-prone 64-bit hash and call that exhaustive proof.
