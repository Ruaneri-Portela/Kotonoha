# Pure Mail native syscall coverage (16 November 2026)

[Português (Brasil)](NATIVE_COVERAGE.pt-BR.md)

## Recovered native syscall handlers

| SYS ID | Call site reported | Current status |
| --- | --- | --- |
| 18 | `start.obj:5`, `PRO1.OBJ:3` | Handled: sets current text reference (`0x422bb0`) |
| 24 | `start.obj:7` | Handled: resets message display & chat (`0x422410`) |
| 20 | `start.obj:11` | Handled: releases text surfaces (`0x4223f0`) |
| 216 | `start.obj:14` | Handled: sets text mode (`0x4227e0`) |
| 17 | `start.obj:899` | Handled: resets message queue & text mode (`0x422570`) |
| 19 | `PRO1.OBJ:5` | Handled: text surfaces ready (`0x4223e0`) |
| 65 | `PRO1.OBJ:809` | Handled: backbuffer copy (`0x424490`) |
| 137 | `PRO1.OBJ:2413` | Handled: per-day sound playback on SE bus (`0x4248f0`) |
| 212 | `extra.OBJ:83` | Handled: extra/omake menu mode initialization |

All nine previously observed native syscalls have been analyzed against `pmail.exe` and handled in the port runtime.

## New diagnostics

Every unsupported native call is recorded per **SYS ID, script object and
return PC**, with a bounded invocation count and a compact calling signature.
Numeric arguments and variable values are retained; string arguments are
represented only as object/index references, never by dialogue text or
player-provided names. At most 512 distinct sites are recorded, with
individual signatures truncated after 24 arguments.

The existing warning remains once per native ID by default. Set
`PMAIL_NATIVE_VERBOSE=1` when collecting a technical trace to report
each new call site. In the retail-only route test the
`PMAIL_UNHANDLED_SITE` TSV rows report all encountered sites and their
signatures. All unknown native IDs continue to be reported as failures by
the route audit; this is **not** a compatibility-complete fix.

## Validation

- `Pmail.ComputerUi` (synthetic; no game assets) checks all nine observed
  IDs are reported, duplicates are coalesced, reference-based signatures are
  retained and audio/unknown routing is separated.
- `Pmail.RouteAudit` (retail data required) reports each distinct
  unknown call site so missing behavior can be connected to the original
  dispatch and script effects.
- To implement an unsupported SYS safely, confirm its argument contract,
  any variables written back, blocking/wait semantics, and observable
  scene/audio/state effects. Add a synthetic regression, then compare the
  affected route with the original executable.
- No native semantics have been fabricated for the six unknown IDs.

## Other messages observed on macOS

- `com.apple.linkd.autoShortcut` / AppIntents / error 4097:
  system XPC service connection failure, not a VM syscall.
- `[aqsrv] AudioQueueInternalStop_Sync -66671` and
  `AudioObjectGetPropertyData: no object with given ID 0`:
  CoreAudio diagnostics. If audio is missing or crackling, investigate
  the default output device, device changes, and audio start/stop timing.
  These logs alone do not establish a VM fault.
- `AddInstanceForFactory ... F8BB1C28-...`: CoreAudio plugin message
  observed in other programs; also not a VM syscall.
- `Upscaler backend=2` / `Anime4K ... initialized`: informational
  rendering initialization messages.
