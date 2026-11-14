# Zero no Tsukaima

[English (US)](README.md) | **Português (Brasil)**

Suporte a arquivos PS2, cenas em estilo KAG, mídia e menus. O port de Nocturne é jogável; Concerto e Symphony têm níveis distintos de suporte.

**Estado:** Os três jogos e suas rotas não têm paridade completa de interpretação e renderização.

## Compilação

```sh
cmake -S . -B build -DKOTONOHA_BUILD_ZERO=ON
cmake --build build --target ZeroNoTsukaima
```

## Dados do jogo

Use sua própria imagem compatível do disco original.
Use o launcher ou passe `--zero /caminho/para/dados` ao executável. Nunca versione dados comerciais.

## Investigação detalhada

[Estado original do port e evidências](PORT_STATUS.md). Resultados históricos se aplicam aos assets/revisão analisados e podem conter informações substituídas depois.

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)
