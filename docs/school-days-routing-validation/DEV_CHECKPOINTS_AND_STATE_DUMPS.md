# DEV Checkpoints and State Dumps

## Purpose

Manual Android validation is useful for engine integration, but restarting from New Game after every process loss is wasteful and does not improve coverage. DEV checkpoints exist only to resume controlled routing experiments.

They are not a save-game format and are not intended for release builds.

## Compile-time gate

Feature macro: `KOTONOHA_DEV_CHECKPOINTS`.

Enable with CMake using `-DKOTONOHA_DEV_CHECKPOINTS=ON`.

When disabled, checkpoint restoration, `-k`, and F6 router state dumps are not compiled.

## CLI

`-k <checkpoint-name>`

Current checkpoint: `-k sd-ep3-r4-b00`.

The engine waits until the checkpoint SceneKey exists in `sceneIndex`, restores the router state, maps the SceneKey to the loaded Gameplay index, and then begins execution there. This matters because the six School Days script directories are loaded incrementally on Android.

## Recovered checkpoint

- SceneKey: `02/02-2K-B00`
- ROUTE: 4
- SCENE: 1
- choiceResult: -2
- callback34: 0
- feelingApplied: false
- endingRegistrations: empty
- 000=0
- 001=10
- 002=47
- 003=0
- 004=0
- 991=1
- 992=1
- 994=1
- 996=1
- 998=1

Recovered BS/history values:

- BS0000B00=4
- BS0000H02=15
- BS0100B04=2
- BS0100B05=5
- BS0100D00=9
- BS0100E01=21
- BS0100E05=24
- BS0100F00=28
- BS0100G00=33
- BS0100I00=38
- BS0100K00=52
- BS0100N00=67
- BS0100N04=76
- BS0100Q00=79
- BS0100U00=69
- BS011KD00=3
- BS011KE06=18
- BS011KF00=19
- BS011KK03=27
- BS011KK07=31

Globals remain `dword_3A6F40=0` and `dword_3A2294=1`.

## Why this is not a teleport

A plain scene jump would leave feelings, flags, BS/history variables, globals, callback state and route state inconsistent. Later predicates could therefore select impossible branches. A DEV checkpoint must restore a known router snapshot and verify that `CurrentScene()` resolves to the expected SceneKey.

## DumpState

`SchoolDaysRouter::DumpState()` emits current SceneKey, route, scene index, choice state, callback34, feelingApplied, all session variables, all global variables, and ending registrations in deterministic map order.

The engine logs a dump immediately after a checkpoint is applied. F6 emits a manual dump while School Days routing is active.

## Future checkpoint protocol

Do not invent future checkpoint values. Reach the desired scene through real routing, trigger `DumpState()`, save the router trace, record the previous transition ID, add the recovered snapshot, add a regression test, and verify that the next transition matches the uninterrupted trace.

Recommended naming: `sd-ep<episode>-r<route>-<scene>`.

## Standalone test

`tests/SchoolDaysDevCheckpointTest.cpp` validates the current checkpoint and verifies that the next route resolution is `t263`.

Example compile command:

    g++ -std=c++17 -DKOTONOHA_DEV_CHECKPOINTS=1 -Iinclude tests/SchoolDaysDevCheckpointTest.cpp src/SchoolDaysRouter.cpp -o SchoolDaysDevCheckpointTest.exe
    .\SchoolDaysDevCheckpointTest.exe

No original game asset is needed for this standalone test.
