# Pure Mail

[English (US)](README.md) | **Português (Brasil)**

Port nativo do DVD com progressão PRO1, decodificação de animações, áudio e saves.

**Estado:** Opcodes da interface do computador, transições e finais completos ainda precisam de validação.

## Compilação

```sh
cmake -S . -B build -DKOTONOHA_BUILD_PMAIL=ON
cmake --build build --target Pmail
```

## Dados do jogo

Use a pasta original PMAILDVD, com data/pack/*.dat e data/movie.
Use o launcher ou passe `--pmail /caminho/para/dados` ao executável. Nunca versione dados comerciais.

## Investigação detalhada

[Resumo do estado do port](STATUS.pt-BR.md) · [Investigação detalhada](PORT_STATUS.md). Resultados históricos se aplicam aos assets/revisão analisados e podem conter informações substituídas depois.

[Pesquisa de animações](docs/ANIMATIONS.pt-BR.md), [áudio](docs/AUDIO_PARITY.pt-BR.md) e [caixa de diálogo](docs/TEXTBOX.pt-BR.md).

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)
