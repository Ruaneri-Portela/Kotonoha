# Kirikiri2 / KirikiriZ

[English (US)](README.md) | **Português (Brasil)**

Leitura de XP3 e tags KAG básicas para diálogos, personagens, escolhas e áudio.

**Estado:** Bytecode TJS2 genérico, plugins externos e alguns formatos gráficos ainda não são suportados.

## Compilação

```sh
cmake -S . -B build -DKOTONOHA_BUILD_KIRIKIRI=ON
cmake --build build --target Kirikiri
```

## Compatibilidade de pacotes

O provedor XP3 agora reconhece barras do Windows/Unix e correspondência
única sem diferenciar maiúsculas/minúsculas ASCII, rejeitando nomes ambíguos.
Fluxos MDF/zlib malformados geram erro em vez de repetir a descompressão
indefinidamente. Testes sintéticos cobrem esses casos.

Isso melhora a camada de assets reutilizável de Kirikiri2/Z. Não implementa
o runtime TJS2 completo nem todas as estruturas PSB/SCN compiladas. Portanto,
montar um arquivo ou extrair texto não garante que um jogo comercial complexo
seja executável.

## Dados do jogo

Use pasta de jogo legítima com scripts .ks ou arquivo .xp3.
Use o launcher ou passe `--kirikiri /caminho/para/dados` ao executável. Nunca versione dados comerciais.

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)
