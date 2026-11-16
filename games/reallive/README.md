# RealLive

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Native engine executing RealLive scenarios, graphics and audio without launching the Windows executable.

**Status:** An initial reference edition has extensive coverage, but 1:1 route and UI parity is unverified.

## Build

```sh
cmake -S . -B build -DKOTONOHA_BUILD_REALLIVE=ON
cmake --build build --target RealLive
```

## Game data

Use an original installation or compatible ISO containing Seen.txt, Gameexe.ini, and resources.
Use the launcher or pass `--reallive /path/to/game-data` to the executable. Never commit commercial game files.

## Compatibility work in this branch

- VFS/ISO-backed SEEN archives now retain Gameexe.ini's REGNAME when
  selecting the RealLive script format. This fixes a concrete startup
  incompatibility affecting Full Voice-style second-level-encoded scripts.
- Title input and return-to-menu commands now use the configurable SEEN_MENU
  scenario instead of assuming that every game uses 9032.
- Empty/truncated synthetic compressed scripts receive bounds checks.

These changes aim to improve CLANNAD startup and planetarian's front-end
behavior. **They do not demonstrate complete playability:** both games still
need tests against the relevant editions' original content, including choices,
graphics, audio, saves and route transitions.

## Detailed research

[Concise support status](STATUS.md) · [Detailed port research](PORT_STATUS.md). Historical results are specific to the reference assets/revision and may contain superseded implementation notes.

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)

## VM call/return safeguards

Local and cross-scenario calls now enforce a return-stack depth limit.
Missing target scenarios do not leave phantom return frames; invalid
returns keep the pending frame until validation succeeds. The session test
keeps its assertions enabled in `Release`, but requires original RealLive
game data.

## RealLive case/switch dispatch

The VM checks explicit matching cases before taking the `()` default,
even when the default precedes other cases in compiled bytecode. The
synthetic `RealLive.CaseSelection` regression test needs no `Seen.txt`:

```sh
cmake --build build --target RealLiveCaseSelectionTests
ctest --test-dir build -R RealLive.CaseSelection --output-on-failure
```

## Shift-JIS strings and binary memory

String length, substring and comparison now account for embedded NUL bytes,
avoid quadratic Shift-JIS length scans and prevent integer overflow when
combining substring offsets. The synthetic `RealLive.StringOps` test requires
no `Seen.txt`.

```sh
cmake --build build --target RealLiveStringOpsTests
ctest --test-dir build -R RealLive.StringOps --output-on-failure
```

## Decimal string VM opcodes

Added `Str.digits` (opcode 19) and `Str.digit` (opcode 20), with
one-based-from-the-right digit positions, safe handling of `INT_MIN`,
and explicit rejection of invalid positions before modifying the target
variable. These are additional value operations, not new UI rendering.

```sh
cmake --build build --target RealLiveDecimalDigitsTests
ctest --test-dir build -R RealLive.DecimalDigits --output-on-failure
```

## Atomic string clear ranges

The interval overload of `Str.strclear` now validates the entire reference
range before erasing any string variable. Invalid or reversed ranges do not
partially mutate persistent VM string storage. Standalone C++17 regression:

```sh
cmake --build build --target RealLiveBoundedRangeTests
ctest --test-dir build -R RealLive.BoundedRange --output-on-failure
```

## Partial Shift-JIS string copy

The `Str.strcpy` count overload now truncates by complete Shift-JIS
**characters**, not raw bytes. It avoids leaving an orphan lead byte in
script string variables. Synthetic coverage is added to
`RealLiveStringOpsTests`; no retail files are required.

## Input controls (macOS, iOS and SDL3 gamepads)

- **Keyboard:** Enter/Space advances dialogue; hold Left/Right Ctrl for held
  skip (when the scenario allows it); S toggles skipping previously read pages;
  A toggles auto advance; Page Up opens backlog; Escape opens the pause menu.
- **Gamepad:** East confirms/advances, South holds skip during dialogue,
  either trigger also holds skip, West toggles read-skip, Right Shoulder toggles
  auto, Back/Left Shoulder opens backlog, and Start opens the pause menu.
  D-pad and the left stick navigate the native title and choices. The global
  swap-A/B preference is respected for face buttons.
- **Touch:** a tap releases on the native RealLive pointer; text and the
  original toolbar activate on touch release. Swipes, canceled fingers and
  multi-touch must not advance or trigger toolbar buttons. Menu/backlog gestures
  remain managed by the shared engine.

Read-skip persists from one **read** page to the next and stops at an unread
page. Held Ctrl/gamepad skip is independent and may advance unread dialogue
when the RealLive script has not disabled Ctrl-skip.

The data-free input regression can be run with:

```sh
cmake --build build --target RealLiveInputTests
ctest --test-dir build -R '^RealLive.Input$' --output-on-failure
```

Game-specific controller layouts and original touch hitboxes still require
macOS and iOS hardware testing with the relevant RealLive editions.
