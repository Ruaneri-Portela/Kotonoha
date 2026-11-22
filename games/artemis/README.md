# Artemis Engine

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Native Artemis Engine runtime supporting `.pfs` / `.pfs.000..999` (`pf6` and SHA-1 XOR `pf8+`) layered archives, `.ast` / `.ipt` Lua-table scripts, multi-layer sprites, localized dialogue, choices, audio (BGM/SE/VO), movies, and save/load persistence.

## Build

```sh
cmake -S . -B build -DKOTONOHA_BUILD_ARTEMIS=ON
cmake --build build --target ArtemisGame
```

## Android Build

```sh
cd buildsystems/android
./gradlew :app:assembleArtemisDebug
```

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)
