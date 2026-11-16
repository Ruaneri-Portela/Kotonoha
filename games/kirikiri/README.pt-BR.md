# Kirikiri2 / KirikiriZ

[English (US)](README.md) | **Português (Brasil)**

Leitura de XP3 e tags KAG básicas para diálogos, personagens, escolhas e áudio.

**Estado:** Bytecode TJS2 genérico, plugins externos e alguns formatos gráficos ainda não são suportados.

## Compatibilidade visual e título (KAG parcial)

O menu provisório do Kotonoha agora aparece mesmo sem roteiros `.ks`
executáveis, evitando a tela vazia quando só existem arquivos TJS2.
Se houver uma imagem de título reconhecível em formato comum, ela
aparece atrás do menu. Os comandos KAG `[bg storage=...]`,
`[image layer=... storage=...]`, `[freeimage]` e `[layopt]` de ocultação
agora alteram fundos e camadas realmente desenhados. Os nomes UTF-16
dos XP3, incluindo caracteres japoneses, também são decodificados.

**Limitações:** não é o menu original do jogo em TJS2. KiriKiri normalmente
inicia por `startup.tjs`; o porte ainda não executa TJS2 completo, nem
SCN/PSB complexo, transições fiéis, TLG5/TLG6 ou filtros específicos.
PNG/JPEG/BMP/WebP usam o decoder que o Kotonoha já possui.
Ver o menu provisório não comprova a jogabilidade de NEKOPARA.

Teste com dados sintéticos:

```sh
cmake -S . -B build -DKOTONOHA_BUILD_KIRIKIRI=ON -DBUILD_TESTING=ON
cmake --build build --target KirikiriXp3Tests Kirikiri
ctest --test-dir build -R '^KirikiriXp3

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
 --output-on-failure
```

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
