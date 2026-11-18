# GlobalFlag persistence and synthetic validation

[Português (Brasil)](GLOBALFLAG_ATOMIC_QA.pt-BR.md)

## Engine-managed filesystem

School Days now calls `Kotonoha::Filesystem::writeBinaryFileAtomically`,
via a small profile-size policy adapter in `SchoolDaysAtomicFile.cpp`.
That writer uses **SDL3** exclusively for file creation, writing, flushing,
closing, removal and same-directory replacement. Its filenames are passed to
SDL in UTF-8, and creating a pending file uses exclusive mode (`wxb`).
It deliberately does not call Win32, POSIX, `fcntl`, `fsync` or OS process
inspection routines in game-specific code.

The newly written `GlobalFlag.DAT` is only visible after a successful
`SDL_RenamePath`; no delete-then-replace fallback is attempted. Failed
writes remove only the pending file created by that invocation. A simulated
interruption leaves an isolated orphan for QA. A new writer never guesses
that an old pending file is safe to delete.

**Durability boundary:** `SDL_FlushIO` and `SDL_CloseIO` flush the
stream, but do **not** certify that the parent directory and bytes have
both reached physical media after a power failure. The prior fork's native
`fsync`/`F_FULLFSYNC` test results do not validate this replaced
implementation and must not be represented as equivalent. The format and
32 MiB payload limit of `FlgH` are unchanged.

The shared `SDLFileSource` and `OutputFile` adapters now pass UTF-8 paths,
including on Windows, and `SchoolDaysSaveStorage` uses shared engine I/O
for reads, probes and writes.

## Tests

- `Kotonoha.AtomicFile` (shared engine CTest): synthetic UTF-8 path,
  full replacement, concurrent writes, fault injection, orphan isolation,
  symlink/directory rejection.
- `SchoolDays.GlobalFlagAtomic`: actual FlgH serialization and storage
  adapter on ordinary and non-ASCII paths, size rejection, pre/post-publication
  errors and restart behavior.
- `SchoolDays.AllClearSynthetic`: synthetic end-count boundary conditions,
  title-state promotion and persistence checks. No commercial files needed.
- `shared-atomic-storage.yml`: builds the shared atomic writer and its
  isolated tests on Linux, macOS and Windows, plus a static check that
  game-specific code does not introduce direct platform I/O.
- School Days opens the same shared `GameLauncher` by default as the other
  desktop ports. `--no-launcher` or an automated `--frames` run bypasses
  it; launcher route selection and remembered settings remain unchanged.

A complete runtime build and visual route comparison are separate integration
tasks. No original game data or private saves are included.
