# Kirikiri2 / KirikiriZ

[English (US)](README.md) | **Português (Brasil)**

Leitura de XP3 e tags KAG básicas para diálogos, personagens, escolhas e áudio.

**Estado:** Bytecode TJS2 genérico, plugins externos e alguns formatos gráficos ainda não são suportados.

## Compilação

```sh
cmake -S . -B build -DKOTONOHA_BUILD_KIRIKIRI=ON
cmake --build build --target Kirikiri
```

## Dados do jogo

Use pasta de jogo legítima com scripts .ks ou arquivo .xp3.
Use o launcher ou passe `--kirikiri /caminho/para/dados` ao executável. Nunca versione dados comerciais.

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)
