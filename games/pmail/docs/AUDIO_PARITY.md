# Pure Mail DVD audio investigation

Executable SHA-256: `a9359d55f23831ebee3b4ca803c6aff1d28a37468de1ccb0e19f8d388ca0e654`.

REA 6.7.0 with its Rizin provider was used against the local `data/pmail.exe`:

```
rea execute-rizin-command /path/to/data/pmail.exe 'pd 85 @ 0x41f760'
```

At `0x41f7bb..0x41f7ee`, the original prepares WAVEFORMATEX and invokes
waveOutOpen: PCM, 2 output channels, 44100 Hz (`0xac44`), 16 bits,
4-byte block alignment, 176400 bytes/second. This differs from Snow Radish's
22050 Hz provider default. The PMAIL provider profile is identified by its
`TITL00a.CRGB` archive entry.

Both complete 1408-entry ADPCM tables match the executable: delta values at
file offset `0x4bdb8` (int32), next-state values at `0x4d3b8` (int32 in the
executable, int16 in the shared decoder). These were compared byte for byte
after expanding the next-state entries to little-endian int32.

The script/runtime dispatch restored in this revision:

- 96: SE channel, including effects whose source is VOICE.DAT.
- 97: replacement SE with fade-in; 98: crossfade with outgoing SE retained.
- 99: stop both SE streams; 100: fade out SE.
- 112: MM-numbered BGM; the second argument controls loop-block repetition.
- 115: stop BGM; 116: fade out BGM.
- 128: voice channel; 132: stop voice.
- 101, 131, 133: host waits inspect the corresponding active player.

SOUND.DAT contains both ADPS music and ADP4 effects. Codec selection must use
the archive filename extension; applying the permissive ADPS decoder to ADP4
produces noise. VOICE.DAT also contains legitimate four-byte, zero-unit records;
these must finish silently. Provider previews and playback use the same rate
and codec, with effective overlays read through AssetProvider.

Fades follow the native 256 gain steps with a base interval of 43 samples,
scaled by the mode (half, double, quarter, quadruple). The runtime applies gain
on presentation updates rather than in the original sample mixer, so envelope
edges are not yet sample-exact. Save/load preserves the outgoing crossfade
stream as well as BGM, SE and voice. Pause freezes the players and fade clocks.

## Reproduction

Build `PmailAudioCheck` and `PmailRuntimeAudioCheck` with BUILD_TESTING enabled.
Both accept a PMAILDVD directory argument. The first validates all archived
audio against header sample counts, output format and channel counts. The
second can run with SDL_VIDEODRIVER=dummy and SDL_AUDIODRIVER=dummy; it enters
the title, starts PRO1, waits for the first voice and reaches the first dialogue.
The dummy driver verifies scheduling, not audible output quality.

Observed full archive result: 29 music entries, 515 effects, 15294 nonempty
voices. Original-executable listening comparisons, sample-exact fade envelopes, mute/skip
interaction across every route, and every audio opcode are not yet certified
as 1:1. These checks do not establish complete game parity.
