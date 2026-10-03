# Routing Validation Worklog

This is a chronological engineering journal. New validation milestones should append entries rather than rewriting history.

## 2026-10-03 — Full router baseline

- baseline commit: `a81ed02d0171be1e8bb13d9d4b17658e66aaac95`
- generated full normal-New-Game router integrated
- 22 known ending witnesses pass in standalone C++

## 2026-10-03 — Android build integration

- Windows -> Android ARM64 build restored locally
- full router compiled into Android build
- APK packaging initially failed because original School Days media exceeded ZIP32 4 GiB limits
- media was moved out of APK packaging
- Android app then built successfully

## 2026-10-03 — Android media access correction

- media was first copied under app-specific external storage by `adb push`
- shell-owned files were visible to adb but inaccessible to the app process
- media was moved into the app private files directory for the DEV test
- visual playback resumed

## 2026-10-03 — Real device routing evidence

- `00 -> 01` confirmed through `t25`
- Route 1 -> Route 2 confirmed inside episode prefix `01`
- `01 -> 02` confirmed through `t240`
- Route 4 continued through `t262` and `t393` to `02/02-2K-B00`

## 2026-10-03 — Process loss and checkpoint recovery

The Android process was accidentally left, losing the live router state. The saved trace was sufficient to reconstruct the causal state at `02/02-2K-B00`.

Recovered checkpoint name: `sd-ep3-r4-b00`.

## 2026-10-03 — Validation lab branch

Created dedicated branch:

`research/school-days-routing-validation-lab-20261003`

Added:

- compile-time DEV checkpoint gate
- `-k` checkpoint argument
- causal checkpoint restore
- deterministic `DumpState()`
- F6 manual state dump
- standalone checkpoint regression test
- dedicated routing-validation documentation

## Next entry

Build the validation-lab branch locally with the existing Windows/Android compatibility fixes, enable `KOTONOHA_DEV_CHECKPOINTS`, restore `sd-ep3-r4-b00`, and confirm the next device transition matches the uninterrupted path.
