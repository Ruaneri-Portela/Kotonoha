# RealLive

[English (US)](README.md) | **Português (Brasil)**

Motor nativo para cenários, gráficos e áudio RealLive sem executar o programa Windows.

**Estado:** Uma edição de referência possui ampla cobertura, mas a equivalência total de rotas/interface não foi validada.

## Compilação

```sh
cmake -S . -B build -DKOTONOHA_BUILD_REALLIVE=ON
cmake --build build --target RealLive
```

## Dados do jogo

Use instalação original ou ISO compatível com Seen.txt, Gameexe.ini e recursos.
Use o launcher ou passe `--reallive /caminho/para/dados` ao executável. Nunca versione dados comerciais.

## Investigação detalhada

[Resumo do estado do port](STATUS.pt-BR.md) · [Investigação detalhada](PORT_STATUS.md). Resultados históricos se aplicam aos assets/revisão analisados e podem conter informações substituídas depois.

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)
