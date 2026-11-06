# School Days HQ v1.02 PC — post-choice feeling-meter sound (retail evidence)

This audit identifies the short **musical effect after a choice** as the
**directional feeling-meter sound**, separate from the immediate
`SeSelect` click. The exact event is a change of an observed feeling value;
it does **not** play for every choice indiscriminately. The audit used the
retail EXE, `SysMenuSDHQ.dll`, and original GPK entries without interacting
with retail saves or running a complete playthrough.

## Native call chain and trigger

- `SysMenuSDHQ.dll:0x10024C60` calls the feeling-meter update
  `0x10026B40` when the host's update latch is active
  (`0x10024C67..0x10024C93`, host getter
  `SCHOOLDAYS HQ.exe:0x0042C3A0`).
- `SysMenuSDHQ.dll:0x10026B40` captures the provider's `001` and
  `002` into `this+0x38/+0x40` in state 0. In state 1,
  `0x10026C5C..0x10026CCF` compares those values with their previous
  samples. **Only when a change starts an interpolation**, the method
  records `timeGetTime` (`0x10026C80`) and invokes the host virtual
  slot `+0x50` with `3` at `0x10026CA1..0x10026CB4`, or `4`
  at `0x10026CB8..0x10026CCB`. The two paths select the direction;
  they are not BGM/script playback. For a changed `002` while `001`
  remained equal, the second branch at `0x10026CCF..0x10026D40`
  emits the same code 3 via `0x10026D14` or code 4 via
  `0x10026D2B`. Both unchanged take `0x10026D42` without playing
  anything. `001` is checked first even if both values have changed.
- The actual host vtable is proven using entries
  `+0x30=0x0042C100` (known feeling-latch setter) and
  `+0x154=0x0042C3A0` (getter) in the EXE's `.rdata`
  (file offset `0xD1694` for the vtable base;
  the `0x0042C100` entry is at `0xD16C4`).
  Host `+0x50` is **`SCHOOLDAYS HQ.exe:0x00429C80`**.
- `SCHOOLDAYS HQ.exe:0x00429CB8..0x00429D45` dispatches codes
  `0..6` to the seven stored UI sound resources, at
  host offsets `+0x5A4/+0x5C0/+0x5DC/+0x5F8/+0x614/+0x630/+0x64C`.
  The EXE's `FILMENGINE.INI` loader at `0x004226DD..0x00422733`
  assigns **SeUp** to the string at host `+0x5F4`
  (read as `+0x5F8` by code 3); `0x00422742..0x00422798`
  assigns **SeDown** to `+0x610` (read as `+0x614` by code 4).
  The other UI resource names occupy the adjacent 28-byte records.
- In `0x00429D75..0x00429E2D`, the EXE locates an available sound
  object in an **eight-entry pool** (`host+0x540`), loads the resource
  through `0x00442BF0`, configures playback through
  `0x004434A0/0x004433D0`, and starts it with `0x004430A0`.
  Therefore this is an **SE/UI sound voice**, not a BGM selection.
  Source behavior can overlap other sound voices; do not route it as BGM.
- The meter interpolation lasts **1500 ms**, with a subsequent
  **2000 ms** meter phase (`SysMenuSDHQ.dll:0x10026D51..`);
  these are animation times and **not** clip lengths. The sounds start
  when the change is detected, not at the end of the 1500 ms animation.

The original direction arbitration is already reproduced by
`SchoolDaysTopMeterModel::Tick`: it prefers the change of `001`;
if `001` did not change, it tests `002`. It emits code `3` for a
strict increase of the selected field and `4` for a non-increase on
the changed field (`SchoolDaysTopMeterModel.cpp:106-124`).
No code is emitted when neither value changes, and the values are
captured anew only when the meter is in its `Capture` state.

## Original resources and exact GPK offsets

`Ini.GPK/FILMENGINE.INI` maps `SeSelect`, `SeUp` and `SeDown`
to the `SysSe/NewSys` resource family. The GPK directory's
case-folded logical paths and first data-byte file offsets are:

| Native cue | Logical resource in `SysSe.GPK` | Physical data offset | Packed bytes | OGG duration (`ffprobe`) |
|---|---|---:|---:|---:|
| 1 — confirmation | `NEWSYS/SESELECT.OGG` | `0x22AF2` | 16,784 | 1.140680 s |
| 3 — increase | `NEWSYS/SEUP.OGG` | `0x26C7E` | 45,088 | 3.924172 s |
| 4 — decrease | `NEWSYS/SEDOWN.OGG` | `0xF881` | 44,711 | 4.022857 s |

These three GPK entries are raw/uncompressed (index `magic=b'    '`).
All three are **Ogg Vorbis, 44,100 Hz, two-channel stereo**.
Other entries named `SYS/SEUP.OGG`, `SYS/SEDOWN.OGG` and
`SYS/SESELECT.OGG` are present in the same GPK, but are **different
samples**; the retail `FILMENGINE.INI` selects `NewSys`, so do not
substitute the `Sys` variants.

## Independent ORS cross-check

Enumerating all **1,859** retail `Script.GPK` ORS entries gives
**287** `[SetSELECT]` and no `[PlayES]` events. Exactly **four**
`[PlaySe]` events are authored in the 48-tick interval from a
`[SetSELECT]` time, each bound to a specific scene/event asset.
Therefore the common second cue is accounted for by the SysMenu/EXE
host callback, not by inserting a universal `PlaySe` into scripts.
Actual next-scene `PlaySe/PlayBgm` events remain independent.

## Kotonoha v3 integration point

The runtime already has the required retail assets and cue enums:

- `SchoolDaysSystemUi::UiSfxCue::Select=1, Up=3, Down=4`;
- `Runtime::playSound` selects `profile.seUp` / `profile.seDown`
  and plays each OGG as a one-shot `MediaPlayer` in the `"SE"` group;
- `SchoolDaysTopMeterModel::TickResult.hostDirectionCode` already emits
  the **exact native code 3/4 or 0** with the correct state-machine
  timing (see `SchoolDaysTopMeterModelTest`).

**Implemented after this retail audit:**
`SchoolDaysMenuBarPresenter::Update` now captures
`hostDirectionCode` when the meter detects a change and exposes it once
through `ConsumeFeelingDirectionCode()`; `Runtime::updateMenuBar`
consumes code `3` to play `UiSfxCue::Up`, code `4` to play
`UiSfxCue::Down`, and does not play a directional cue for `0`.
The existing `Runtime::armChoiceExit` confirmation `SeSelect` remains
independent. This retains the `SchoolDaysTopMeterModel` 1500/2000 ms
state machine and its original directional priority; no cue is
synthesized on unchanged values or at confirmation alone.

The existing Config/General Options path controls the `SE` audio
channel through `SchoolDaysRetailConfigPolicy::NativeAudioBank::Se`,
`SeVolume` (persisted `0..10`) and `Runtime::applyConfig`'s
`audio.channel("SE")->setVolume(...)` (
`SchoolDaysRuntime.cpp:4748-4758`). Master mute also applies.
The new cue must use that same channel and obey its gain and mute.
The existing acceleration policy may temporarily mute the `SE`
channel at fast rates; the choice presentation enforces its existing
1x effective clock override. A dedicated separately mixed UI channel
would require independent retail evidence.

**Focused integration tests added:** verify increase/decrease
in `001` (3/4), increase/decrease in `002` with unchanged `001`
(3/4), simultaneous opposite-sign changes (`001` wins), and no
change (0, no second sound). Also verify no artificial sound on meter
initialization/load, only one sound at `Detect` per state-machine
cycle, later changes are coalesced until `Capture` after the
1500 ms + 2000 ms cycle, one independent `SeSelect` on user
confirmation, and `SeVolume`/master mute application. The existing
`SchoolDaysTopMeterModelTest` covers several native direction and
phase cases. `SchoolDaysMenuBarRuntimeTest::CheckMeterSfxHostCodes`
uses isolated synthetic `System/MenuBar` PNG assets and a dummy
`Script/English` folder to exercise the presenter one-shot
capture/detect/consume boundary; the target passed in Release and the
complete serial CTest suite subsequently passed 57/57.

This evidence authenticates the asset, trigger and internal SE voice
pool; it does not rely on a new retail GUI audio capture and does not
establish measured speaker onset latency.
