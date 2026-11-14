# LucaSystem

[English (US)](README.md) | **Português (Brasil)**

Interpretação nativa de PAK/bytecode e renderização de imagem/áudio/texto testadas com AIR.

**Estado:** O suporte documentado é específico do AIR; não implica compatibilidade equivalente com outros títulos LucaSystem.

## Compilação

```sh
cmake -S . -B build -DKOTONOHA_BUILD_LUCA_SYSTEM=ON
cmake --build build --target LucaSystem
```

## Dados do jogo

Use PAK originais (incluindo PARAM.PAK e SCRIPT.PAK); AIR é o título de referência.
Use o launcher ou passe `--luca-system /caminho/para/dados` ao executável. Nunca versione dados comerciais.

## Investigação detalhada

[Resumo do estado do port](STATUS.pt-BR.md) · [Investigação detalhada](PORT_STATUS.md). Resultados históricos se aplicam aos assets/revisão analisados e podem conter informações substituídas depois.

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)
