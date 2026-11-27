# School Days HQ v1.02 — visible speed labels, internal clock, and media audio

## Evidence and correction

The original gameplay MenuBar screenshot
`build/school-days-parity-2026-11-05/retail-gameplay-topmenu-98033046000000.png`
(captured 2026-11-05) visibly labels the five speed cells `x1`, `x2`, `x4`,
`x16`, `x32`. **The older conclusion that the UI displays `x12`/`x24` was
incorrect.** This does not contradict the authentic binary's internal rate
table: the UI labels and the numeric playback parameters are different.

Static evidence was read directly from the original `SCHOOLDAYS HQ.exe`
(SHA-256 `e4e09563f44a57a023884fb8f023138a4e2168b9fe174b363fe1cfcfb3a98e26`).
`SysMenuSDHQ.dll` in that installation has SHA-256
`494fe7851fe68ae7eb09ed13808d19a45b9d6509e167aece628a65c3014e8836`.

| Internal selector (controls 5–9) | Original UI label | EXE internal float | Nominal 24-fps logical ticks per real second |
|---:|---:|---:|---:|
| 0 | x1 | 1.0 | 24 |
| 1 | x2 | 2.0 | 48 |
| 2 | x4 | 4.0 | 96 |
| 3 | x16 | 12.0 | 288 |
| 4 | x32 | 24.0 | 576 |

The last column is the **static mathematical rate of the retail secondary
clock** assuming the authenticated 24-fps scene timebase; it is not an
independent real-time measurement of every decoder, rendering path, or skip
operation.

### Rate and clock cross-references

- `EXE 0x00424F90`: validates indices 0..4, performs
  `FLD DWORD PTR [index*4+0x004F99F0]`, and stores the exact float at
  `host+0x50C`. It forwards this rate to `0x00431C90` and
  `0x00429500 -> 0x004433D0`, without a x16/x32 conversion in this chain.
- `EXE 0x004F99F0`: literal IEEE-754 float table
  `{1.0, 2.0, 4.0, 12.0, 24.0}`.
- `EXE 0x00422F70` (notably `0x00423007..0x0042306A`): computes
  `delta_ms * [0x0050C468] * [host+0x538] / 1000`, then adds the
  logical base position `[host+0x540]` and updates the monotonic position
  relative to `[host+0x544]`. This directly establishes that `host+0x538`
  is a **real multiplier of elapsed time**, rather than just a user-interface
  selection number.
- `EXE 0x004250B0(index)`: copies the preceding secondary index
  `[host+0x530]` to `[host+0x534]` and applies a new rate from the same
  table to `[host+0x538]` and both secondary playback components.
- `EXE 0x00431740`, branch `0x004317E2..0x004317F0`: calls
  `0x004250B0(0)` when opening the live choice presentation (subject to the
  host predicate). At choice-completion `0x004316B0` branches to
  `0x004251D0` through call `0x00431723`. This restores the saved
  secondary selector `[host+0x534]` into `[host+0x530]`, reloads
  `[host+0x538]` and reapplies the associated rate. The main MenuBar selector
  `[host+0x504]` / `[host+0x50C]` is a **separate state**; do not simply
  overwrite it to simulate the temporary choice presentation.

### Sound / rate boundary

- `EXE 0x004433D0`: stores the incoming media rate and compares it to
  **double 4.0** at `0x004D5080`. The `>4.0` branch sets its internal
  rate gate, calls `0x00443650(1)` and forwards the flag through
  `0x0041A2E0` as message `0x8010`; the `<=4.0` branch sends `0`.
- This authenticates a media-subsystem state transition **at a rate strictly
  greater than 4**, matching the distinction between the first three
  modes and the two turbo modes. The ultimate meaning of message `0x8010`
  (including whether it mutes all game media, particular streams, or UI SE)
  remains to be closed with a PCM/witness or a resolved media-message handler.
  Do not cite this alone as measured output silence.

## v3 integration boundary

The game-only `SchoolDaysPlaybackPolicy::SelectionForIndex` now exposes the
original visible labels, the internal float rates and the `rate>4` narrative
audio-suppression gate as *separate properties*. The existing `RateForIndex`
continues returning internal 1/2/4/12/24, preventing any change of scene
timing based solely on image text.

Engine `AudioChannel::setMuted` (`lib/include/Kotonoha/media/AudioMixer.hpp`,
`lib/src/media/AudioMixer.cpp`) sets stream gain to zero without stopping
the audio device, decoder or media clock. It is suitable for applying a
game-specific narrative-only audio gate. **Do not** use `MediaPlayer::setPaused`
or persisted `masterMuted` for the turbo audio effect.

Narrative media currently use mixer channels `Movie`, `Voice`, `BGM`, `SE`;
the game `Runtime::playSound` also uses `SE` for UI selection/click effects.
Applying turbo mute to `SE` therefore also silences UI sounds unless those
sounds are intentionally moved to a separate, SE-volume-controlled UI
channel. The retail UI SFX behavior in turbo remains an evidence gap.

No original GUI operation or full campaign test was performed in this
audit, and no `SchoolDaysRuntime.cpp/.hpp` or engine changes were made.
Scoped validation: `SchoolDaysMenuBarLayoutTest` built and passed in Release.
