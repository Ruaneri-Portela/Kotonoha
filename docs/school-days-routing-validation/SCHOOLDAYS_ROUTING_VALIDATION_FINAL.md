# School Days HQ — Routing Validation Final

Status: **ROUTING MODEL v1 FROZEN**

This document closes the School Days HQ route-recovery/validation phase and defines the canonical oracle used by the future KTRF / `.ktnroute` implementation.

The Git commit containing this file and the certified artifacts, tagged `school-days-routing-oracle-v1`, is the canonical School Days HQ Routing Model v1 oracle.

## Scope

Included in the frozen routing oracle:

- logical route nodes and route/scene coordinates;
- forward routing transitions and transition priority;
- choices and delayed choice routing;
- feeling deltas/resolutions;
- conditions and comparisons;
- routing effects;
- session/global/BS state used by routing;
- ending registration;
- preserved external callbacks;
- routing-only logical nodes;
- cross-route and cross-episode transitions;
- terminal transitions;
- episode/terminal handoff metadata.

Outside this static routing oracle:

- SaveFile/Load serialization;
- GlobalFlag persistence format;
- Save/Continue frontend;
- title/menu implementation;
- Back/Replay historical behavior;
- Route Map UI;
- Android lifecycle/platform integration.

These runtime/frontend/persistence concerns must remain separate from the static route format.

## Frozen model counts

- Routes: **55**
- Logical nodes / SceneKeys: **1857**
- Normal New Game transitions: **2458**
- Conditions: **1464**
- Effects: **6183**
- Feeling deltas: **302**
- Feeling resolutions: **772**
- Terminal transitions: **23**
- Non-terminal `callback_38` handoffs: **47**
- Terminal `callback_38` handoffs: **23**

Previously certified causal baseline:

- reachable SceneKeys: **1,856 / 1,857**;
- unreachable normal SceneKey: `04/04-C0-A05`;
- reachable forward transitions: **2,458**;
- native endings reachable: **22 / 22**.

## Primitive catalog

### Conditions

- `Callback34`
- `Choice`
- `FlagOr`
- `GlobalValue`
- `SessionComparison`
- `SessionValue`

### Compare operations

- `Eq`
- `Gt`
- `Le`
- `Ne`

### Effects

- `Callback`
- `RegisterEnding`
- `SetGlobalConst`
- `SetSessionConst`
- `SetSessionFromSession`

These names describe the recovered School Days implementation. They do not automatically become the permanent KTRF opcode set; KTRF receives a separate, generic and versioned specification.

## Choice semantics

Choice routing is delayed:

``text
SetSELECT
    ↓
choice_result
    ↓
feeling delta application
    ↓
timeline continues
    ↓
[Next]
    ↓
routing evaluation
``

Choice values:

``text
-2 = pending
-1 = timeout
 0 = first option
 1 = second option
``

A choice must not route immediately when selected.

## Logical nodes vs physical ORS

Confirmed router-only logical dispatcher nodes:

- `03/03-B2-A00`
- `03/03-KB-E00`

Confirmed ORS-only physical scripts outside the natural router graph:

- `01/01-00-OP2`
- `05/05-9O-B00`

Therefore KTRF must keep logical node identity separate from physical resource location.

## Known dead branches

The recovered model preserves structurally valid but causally unreachable Normal New Game branches:

- `t971`
- `t1243`
- `t1248`
- `t1494`
- `t1824`
- `t1825`

They remain part of the oracle and must not be silently pruned.

## Ending validation

All **22 native endings** are causally reachable from Normal New Game.

The standalone witness suite validates expected transition IDs along each witness path, not merely the final ending.

``text
22 / 22 ending witnesses PASS
``

## Full Windows runtime validation

A real routed run completed:

``text
Episode 1
→ Episode 2
→ Episode 3
→ Episode 4
→ Episode 5
→ Episode 6
→ Ending 16
``

Representative final state:

``text
ROUTE=49
SCENE=28
05/05-SB-I02
→ t2357
→ Ending 16
``

## External handoff semantics

Callbacks are preserved as routing effects; frontend/UI semantics remain outside the router.

### Episode handoff

Runtime-confirmed targeted path:

``text
00/00-00-L00
Route 0 / Scene 20
→ t25
→ callback_00
→ callback_2C
→ callback_38
→ ROUTE=1
→ SCENE=0
→ 01/01-00-A00
``

Status: **WINDOWS RUNTIME PASS**

### Terminal handoff

Runtime-confirmed targeted path:

``text
05/05-SB-I02
Route 49 / Scene 28
→ t2357
→ RegisterEnding(16)
→ REP05_SB_I01=1
→ callback_38
→ terminal handoff
``

The process remains alive instead of terminating immediately.

Status: **WINDOWS RUNTIME PASS**

The original frontend uses a **20-second “Save this game?” prompt** after handoff. That frontend behavior is deliberately outside this static routing oracle.

## callback_38 structural closure

- non-terminal `callback_38` transitions: **47**
- terminal transitions: **23**
- terminal transitions containing `callback_38`: **23**

The non-terminal cases are cross-episode handoff edges in the recovered Normal New Game model.

## Certified DEV checkpoints

### `sd-ep1-r0-l00-handoff`

``text
SceneKey: 00/00-00-L00
ROUTE=0
SCENE=20
expected: t25
destination: 01/01-00-A00
callbacks: callback_00, callback_2C, callback_38
``

### `sd-ending16-r49-i02-handoff`

``text
SceneKey: 05/05-SB-I02
ROUTE=49
SCENE=28
expected: t2357
ending: 16
callback: callback_38
``

These are targeted mechanism checkpoints, not fabricated full historical save snapshots.

## Certified regression set

The oracle freeze requires:

``text
git diff --check
Windows Debug build
SchoolDaysHandoffRouterTest
SchoolDaysFullRouterTest
SchoolDaysDevCheckpointTest
22-ending deterministic witness suite
terminal/callback structural audit
structural Freeze Audit
``

All passed at freeze time.

## Certified artifacts

``text
docs/school-days-routing-validation/certified/
├── BUILD_ROUTING_CERTIFIED.txt
├── CTEST_ROUTING_CERTIFIED.txt
├── GIT_DIFF_CHECK_CERTIFIED.txt
├── QUICK_22_ENDINGS_CERTIFIED.txt
├── TERMINAL_CALLBACK_AUDIT_CERTIFIED.txt
├── ROUTING_MODEL_FREEZE.json
├── ROUTING_MODEL_FREEZE.md
├── ROUTING_MODEL_HASHES.json
└── ROUTING_MODEL_CATALOG.json
``

`ROUTING_MODEL_HASHES.json` records SHA-256 hashes of canonical routing-related sources.

`ROUTING_MODEL_CATALOG.json` records the frozen route/node/transition catalog.

`ROUTING_MODEL_FREEZE.json` records machine-readable validation status and structural checks.

## Android status

Windows routing validation is complete.

Android ARM64 still requires revalidation after the latest routing/handoff integration. This does not invalidate the frozen RouteProc-derived static model.

## Oracle rule

School Days Routing Model v1 is now the immutable conformance reference for initial KTRF development.

A future `.ktnroute` round-trip must preserve at minimum:

``text
route count
node count
SceneKeys
route/scene coordinates
transition IDs
transition order/priority
conditions
comparison semantics
effects
choice masks
feeling resolutions
ending registrations
external callback metadata
routing-only nodes
terminal transitions
``

A deliberate semantic change requires a new oracle/freeze version rather than silently replacing v1.

## KTRF boundary

This freeze closes the School Days route-recovery phase.

Next phase:

``text
KTRF — Kotonoha Routing Format
*.ktnroute
``

KTRF must be generic, extensible, versioned and independently documented. School Days HQ Routing Model v1 is its first conformance oracle.

## Final status

``text
School Days HQ Routing Model v1
================================

Route recovery       COMPLETE
Static model         COMPLETE
Choices              COMPLETE
Feelings             COMPLETE
Conditions           COMPLETE
Effects              COMPLETE
Routing-only nodes   COMPLETE
Endings 22/22        COMPLETE
Episode handoff      WINDOWS RUNTIME PASS
Terminal handoff     WINDOWS RUNTIME PASS
Freeze Audit         PASS
KTRF oracle          READY

Android revalidation PENDING
Save/Load            OUTSIDE ROUTING ORACLE
Frontend/Title       OUTSIDE ROUTING ORACLE
``

**School Days HQ Routing Model v1 is FROZEN.**
