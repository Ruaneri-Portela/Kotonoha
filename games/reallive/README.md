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

## Read progression and BGM state in saves

RealLive presentation saves now use version 7. Each slot retains dialogue
read-history, current page identity and the script-requested BGM pause state.
Loading an earlier slot merges visited pages with the current session's read
history, so already-read text does not become unread after a rollback. Files
written by versions 1 through 6 remain readable; they have no stored read
history, so those visits cannot be reconstructed from a legacy save.

Synthetic tests (no game installation required):

```sh
cmake --build build --target RealLiveAudioFadeTests RealLiveReadProgressTests
ctest --test-dir build -R 'RealLive.(AudioFade|ReadProgress)' --output-on-failure
```

Read history is persisted **inside a save slot**, not yet in a separate
cross-save, title-wide progression database. CG gallery unlocks and original
menu parity require additional per-game reference data and testing.

## Directional SEL effects

SEL style 10 (directional wipes) and style 120 (repeating blinds) now use
native-resolution directional masks instead of generic crossfades. Direction
0/1/2/3 selects top-to-bottom, bottom-to-top, left-to-right or right-to-left.
The SEL interpolation and stripe-size parameters are honored; software
composition is filtered as a completed frame, avoiding independent sprite
upscaling. Ordered-dither styles 4/5 still use the accelerated mask when
supported. Scroll-on/off, squash-on/off and slide-on/off (styles 15–18, 20–21) now also sample both scenes at native resolution, including all four cardinal directions.
These transitions need comparison against screenshots and timing from an
original Windows RealLive executable before claiming exact visual parity.

## Cross-session read history

Read-skip now writes a separate versioned `.progress` journal after each newly
completed dialogue page, using the engine's atomically written save directory.
The journal is scoped to the original game's `#REGNAME` (or its asset mount
when no REGNAME is defined), so two unrelated games do not share seen flags.
Corrupt or unreadable journals are logged and ignored without blocking the game.
Loading a v7 save merges read marks into the global history; the journal itself
contains no saved dialogue text, BGM state, or graphic unlocks.

```sh
cmake --build build --target RealLivePersistentHistoryTests
ctest --test-dir build -R RealLive.PersistentHistory --output-on-failure
```

CG/gallery unlock rules and complete native menu fidelity still require
reference edition data and original-game playthrough testing.

## Sample-rate-aware BGM resume

Presentation saves record BGM time in seconds and restore the decoded PCM
clip at its actual sampling rate, whether 22,050 Hz, 32,000 Hz, 44,100 Hz or
48,000 Hz. The interpreter no longer assumes 44.1 kHz when converting a saved
position back to frames; relative seeks are also bounded before pointer math.
Synthetic audio regressions test round-trip and overflow cases without licensed
music files.

## Scroll, squash and slide SEL effects

Style 15 scrolls the new frame in while scrolling the old frame out; 16
scrolls the new frame in while compressing the old frame; 17 expands the new
frame while scrolling the old out; 18 expands/compresses both frames; 20
slides the new frame over the stationary old frame; and 21 slides the old
frame away from the stationary new frame. These effects compose once at native
resolution before upscaling. The synthetic `RealLive.ScrollTransition` test
checks all six styles in four directions with pixel-mapping assertions.
Exact interpolation, speed and game-specific SEL entries still require Windows
RealLive reference captures.

## Conditional selections across RealLive editions

RealLive's `select_w` now respects the encoded text-window override, with
`DEFAULT_SEL_WINDOW` as a fallback. Choice conditions distinguish hidden
entries from disabled/title-only entries, preserving original selection IDs
for scenario branches. Disabled options cannot be activated through mouse,
touch, keyboard or controller, and may use their configured color-table entry.
Selection eligibility is tested without game data by
`RealLive.SelectionRules`; shared menu controls retain enabled-by-default
behavior. Native button-object selections and remaining specialized selection
attributes still require edition-specific verification.

## Synchronous voice and PCM waits

`koeWait` and `wavWait` now yield the RealLive script VM while their
associated audio stream is still playing, instead of returning immediately
or guessing playback duration from a fixed timer. The native PCM wait follows
the authored channel ID, and the newer `wavPlayEx` signature can start
playback and wait in one opcode. These barriers cooperate with story pause,
ending naturally when sound playback stops; save-load, title reset and shutdown
clear pending waits. Data-free regression: `RealLive.AudioWait`.

## Portable 32-bit RealLive arithmetic

Native script random-range operations now accept inclusive bounds as wide as
INT_MIN..INT_MAX, without signed overflow or the bias of rand() % width.
Other 32-bit arithmetic (negate, modulo and percentage) follows defined
VM integer semantics even at extremal operands, including INT_MIN % -1.
These behaviors are covered by the data-free RealLive.NumericCompat test
on both GCC and Clang. Deterministic native-random compatibility with any
specific Windows RealLive release has **not** yet been established.

## Script-controlled voice and PCM channels

Voice and PCM playback now apply per-script gain controls normalized from the
native 0..255 volume range. Newly played sounds inherit their channel's volume,
and fades advance only while gameplay is active. Native PCM volume/set/mute
variants (11–14), voice volume/set/mute variants (11–14), and mapped sound effect
channels use the same controller. A fade-out can stop its PCM channel after
reaching silence. `RealLive.ChannelLevels` provides synthetic tests for per-channel
isolation, mute, exact fade endpoints and very large frame-time jumps.

The exact command signatures vary between RealLive generations; audio routing
and channel fades still need comparison with specific games. Voice/PCM gain is
currently session state, not serialized in the presentation save.
