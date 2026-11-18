# N2System

[English (US)](README.md) | **Português (Brasil)**

Port do motor Nitroplus N2System / NsScript VM, gerenciador de variáveis, grafo de cena, sistema de áudio, caixa de diálogo com typewriter e persistência de save state baseado no [NitroSharp](https://github.com/CommitteeOfZero/nitrosharp).

## Compilação

```sh
cmake -S . -B build -DKOTONOHA_BUILD_N2SYSTEM=ON
cmake --build build --target N2SystemGame
```

## Compilação no Android

```sh
cd buildsystems/android
./gradlew :app:assembleN2SystemDebug
```

## Estado Detalhado

[Resumo do estado](STATUS.pt-BR.md) | [Status in English](STATUS.md)

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)
