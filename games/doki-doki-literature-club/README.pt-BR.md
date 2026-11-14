# Doki Doki Literature Club!

[English (US)](README.md) | **Português (Brasil)**

Integração de roteiros compatíveis com Ren'Py, mídia, tradução e overlays VNX opcionais.

**Estado:** A compatibilidade depende da instalação/patch e dos testes realizados.

## Compilação

```sh
cmake -S . -B build -DKOTONOHA_BUILD_DDLC=ON
cmake --build build --target DokiDoki
```

## Dados do jogo

Forneça os dados originais (pacotes RPA e demais recursos); CPython é dependência específica deste port.
Use o launcher ou passe `--ddlc /caminho/para/dados` ao executável. Nunca versione dados comerciais.

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)
