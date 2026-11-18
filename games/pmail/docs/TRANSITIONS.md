# Pure Mail DVD: transition evidence

[Português (Brasil)](TRANSITIONS.pt-BR.md)

## What the executable and existing retail research establish

The original Pure Mail DVD binary (the same `pmail.exe` SHA-256 recorded in
[ANIMATIONS.md](ANIMATIONS.md)) dispatches `SYS 51` through native handler
`0x4240e0` and its preset table. In the documented `PRO1` flow,
`osboot.anim` is preset 0, played at **(245, 152)**, with **80 frames**
of **54 ms** each (about **4320 ms**). Its content says **FORIS OS 7.7**,
and it is not an AVI. The VM must wait until the animation completes.

**Not established:** that the OSBOOT animation is immediately before the
Overflow company photograph mentioned in the bug report. The identity of
that precise pre-Overflow scene cannot be asserted without a retail route
capture (or a matching trace with those assets).

## Obtain a reproducible asset/VM trace

Build the developer-only route audit with the user's own original files:

```sh
cmake -S . -B build -DBUILD_TESTING=ON \
  -DKOTONOHA_BUILD_PMAIL=ON -DPMAIL_TEST_ASSET_DIR=/path/to/PMAILDVD
cmake --build build --target PmailRouteAuditCheck
ctest --test-dir build -R '^Pmail.RouteAudit$' --output-on-failure
```

For a report with a specific choice path, locate `PmailRouteAuditCheck`
under the chosen build output directory and execute:

```sh
PmailRouteAuditCheck /path/to/PMAILDVD 0,1,2 300 > pmail-transition.tsv
```

Output includes the bounded resource/VM timeline:

```text
PMAIL_TRACE_HEADER seq ms script pc kind resource frame x y
PMAIL_TRACE       ...
PMAIL_OVERFLOW_MARKER ...
```

The TSV is intentionally limited to the first 200 and last 200 events,
plus a snapshot from immediately before the first resource whose name
contains `overflow` (case-insensitive). **If no filename contains that
word, the marker is reported as `not-found`; it does not prove the logo
was absent.** Search the resource order and nearby scene or SYS 51 events.

`ms` is relative test-runner time, not a timestamp measured inside the
2001 executable. Sampled animation frames (0, every 10th, final) provide
phase landmarks; they are not every-frame coverage.

## Compare with the original

Run the same game data and sequence of choices under the original executable,
recording the displayed logo, animation/frame landmarks, and timestamps
from video capture. Compare: (1) script/PC, (2) `SYS 48/49/51/69`,
(3) resource filename, (4) animation preset and frame order,
(5) relevant positions in the 800×600 source image, and (6) elapsed
times. Differences must be investigated using the real files before
changing a preset or inventing a missing animation.

`Pmail.TransitionTrace` (synthetic) validates the reporting window and
format; `Pmail.ComputerSave` (synthetic) verifies save restoration and
animation frame continuation. **Neither verifies full retail route parity.**
GitHub Actions remain manual-only.
