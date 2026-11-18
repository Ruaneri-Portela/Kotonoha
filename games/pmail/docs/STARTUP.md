# Pure Mail DVD: startup correspondence

**English (US)** | [Português (Brasil)](STARTUP.pt-BR.md)

The original `data/pmail.exe` (PE32, x86, SHA-256
`a9359d55f23831ebee3b4ca803c6aff1d28a37468de1ccb0e19f8d388ca0e654`)
was analyzed as a binary. These observations are specific to this version.

## Established evidence

- PE entry `0x42d5f0` runs the Windows/C runtime before game setup.
- `0x4137e0` resolves installation/data files. Kotonoha uses a cross-platform
  virtual filesystem instead of a Windows installation/registry dependency.
- At `0x422357..0x4223df`, the game starts with `pmail.obj` (user-function
  library) and `start.obj` (entry script). The port now rejects an installation
  missing either of these; it no longer guesses unrelated scripts.
- Original global seed at `0x43f7dc` (file offset `0x3f7dc`) is
  `01 00 00 00`. The Pure Mail VM now initializes/resets its LCG to 1.
- The four movie strings at `0x441358..0x4413cc` reference
  `PM01L_I5.AVI`, `PM01S_I5.AVI`, `PM01L_I3.AVI`, `PM01S_I3.AVI`.
  The port previously omitted `PM01L_I3`.

## Implemented behavior and tests

`Pmail.Startup` verifies initial random output and repeatable reset, all
original movie variants, missing movies, native AVI priority and browser
sidecar priority with synthetic data only.

Native builds prefer original AVI over converted versions of the same movie.
Browsers prefer WebM/MP4 sidecars when available. Both use VFS roots
`data/movie/` and `movie/`. Original movie absence is currently nonfatal
to preserve the port's existing behavior.

`PMAIL_MOVIE_VARIANT=PM01L_I3` (or another known stem) requests a particular
installed movie variant for controlled A/B comparison. This is a **port-only
developer override**, not an original game setting. `PMAIL_SKIP_INTRO`
remains for CI.

## Not yet 1:1

The original movie preference depends on the CPU/configuration logic at
`0x4145bc..0x414720`; that complete decision tree has not been
reconstructed. Accurate logo-to-movie-to-menu ordering and exact frame
timings also need a comparative recording of the original process.
SDL3's cross-platform setup does not reproduce the 2001 Windows registry,
mutex or CPU-specific DLL loading, although equivalent user-visible
behavior is the goal.
