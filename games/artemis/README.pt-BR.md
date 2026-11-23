# Artemis Engine

[English (US)](README.md) | **Português (Brasil)**

Runtime nativo da Artemis Engine com suporte a arquivos `.pfs` / `.pfs.000..999` (`pf6` e `pf8+` com XOR SHA-1), scripts em tabelas Lua `.ast` / `.ipt`, sprites multi-camada, diálogos localizados, escolhas, áudio (BGM/SE/VO), vídeos e persistência de saves.

## Compilação

```sh
cmake -S . -B build -DKOTONOHA_BUILD_ARTEMIS=ON
cmake --build build --target ArtemisGame
```

## Compilação Android

```sh
cd buildsystems/android
./gradlew :app:assembleArtemisDebug
```

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)
