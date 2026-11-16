# Pure Mail native syscall coverage (16 November 2026)

[Português (Brasil)](NATIVE_COVERAGE.pt-BR.md)

## Unimplemented instructions observed in live reports

| SYS ID | Call site reported | Current status |
| --- | --- | --- |
| 18 | `start.obj:5` | Unimplemented; native semantics unknown |
| 24 | `start.obj:7` | Unimplemented; native semantics unknown |
| 20 | `start.obj:11` | Unimplemented; native semantics unknown |
| 216 | `start.obj:14` | Unimplemented; native semantics unknown |
| 17 | `start.obj:899` | Unimplemented; native semantics unknown |
| 212 | `extra.OBJ:83` | Unimplemented; native semantics unknown |

These warnings come from the `PmailGame::native` fallback, rather than
`PmailAudio`. None of the six IDs is implemented by the currently
documented audio or presentation handlers. **Do not assign guessed no-op
semantics or suppress their warnings**: an unsupported native call may
affect state, presentation, input or control flow.

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

- `Pmail.ComputerUi` (synthetic; no game assets) checks all six observed
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
