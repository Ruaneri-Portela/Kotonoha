# N2System

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Native Nitroplus N2System / NsScript VM, Variable Store, Scene Graph, Audio System, Typewriter Dialogue, and Save State persistence reference port based on [NitroSharp](https://github.com/CommitteeOfZero/nitrosharp).

## Build

```sh
cmake -S . -B build -DKOTONOHA_BUILD_N2SYSTEM=ON
cmake --build build --target N2SystemGame
```

## Android Build

```sh
cd buildsystems/android
./gradlew :app:assembleN2SystemDebug
```

## Runtime behavior

- `CreateSound` only creates a muted sound entity. `Request(name, "Play")` is
  what starts it; per-entity `SetVolume`/`SetLoop` changes apply to the single
  playback channel. `@BGM04` and `BGM04` address the same entity. `"Disused"`
  does not stop playback.
- Steins;Gate keeps one BGM channel from the title into gameplay. The title
  BGM (`sound/bgm/bgm04.ogg`) is started by `function.SoundPlay` and is not
  stopped on the title-to-gameplay transition.
- After choosing START, `sg00_01.nsb` creates `dx/prologue01.ngs` /
  `dx/prologue02.ngs` through `CreateMovieSG` and waits via `MovieWaitSG`
  (which polls `RemainTime`). The prologue movie is non-looping and can be
  skipped with the engine's deliberate skip gesture.

## Detailed Status

[Status summary](STATUS.md) | [Status resumo em português](STATUS.pt-BR.md)

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)
