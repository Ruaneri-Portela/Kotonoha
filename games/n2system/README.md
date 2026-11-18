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

## Detailed Status

[Status summary](STATUS.md) | [Status resumo em português](STATUS.pt-BR.md)

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)
