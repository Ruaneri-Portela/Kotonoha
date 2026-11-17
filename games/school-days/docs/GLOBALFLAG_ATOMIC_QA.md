# GlobalFlag durability and synthetic AllClear QA

**Scope:** School Days game-specific persistence and synthetic tests. This
verification does not require commercial packs, real saves or a retail game
executable. The on-disk `FlgH` format remains unchanged.

## Standalone atomic writer — 2026-11-17

`SchoolDaysAtomicFile` is C++17-only, with no engine, SDL or vcpkg dependency.
The standalone test covers 1,000 complete replacements, injected write/flush/
publish/directory-sync failures, orphan cleanup, and 220 randomized `SIGKILL`
writer interruptions per POSIX runner. macOS uses `fcntl(F_FULLFSYNC)` when
available, falling back to `fsync` on `ENOTSUP`; interrupted syscalls and
partial writes are handled. The same test passed locally on Windows.

The temporary GitHub Actions verification ran on **2026-11-17** in the fork:

| Runner | Image version from CI | Result |
| --- | --- | --- |
| Ubuntu 26.04 | `20261111.196.1` | C++17 and ASan/UBSan PASS |
| macOS 26 ARM64 | `20261102.0473.1` | C++17 PASS |

Runs: [original](https://github.com/tsubaki-0x/Kotonoha/actions/runs/44013431802)
and [follow-up](https://github.com/tsubaki-0x/Kotonoha/actions/runs/44014281654).
The temporary CI branch and its workflow were removed. The results establish
standalone writer behavior, **not** an entire Linux/macOS game build or proof
of durability under physical power loss.

## AllClear and DIST test boundaries

`SchoolDays.AllClearSynthetic` covers N-1/N final flags, `EndBGView`,
promotion when the title is prepared, idempotence, corrupt GlobalFlag rejection
and persistence after a new object reload. The marker-protected
`tools/make_synthetic_globalflag.py` refuses writes outside a marked isolated
QA profile and refuses overwrites without `--replace`; the read-only
`tools/inspect_synthetic_globalflag.py` prints only relevant flags.

The complete sequence of ending scenes has **not** been visually validated.
An end-to-end retail parity claim would require gameplay verification.
