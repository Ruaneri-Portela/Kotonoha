# Kirikiri2 / KirikiriZ

[English (US)](README.md) | **Português (Brasil)**

Porte C++17 de KiriKiri para o Kotonoha: montagem XP3, interpretador KAG experimental e apresentação básica de imagens, diálogos, menus, escolhas e áudio.

## Compatibilidade implementada

- O provedor XP3 aceita índices canônicos sem compressão ou com zlib, índices encadeados, vários segmentos por arquivo (inclusive mistos), nomes Unicode, busca ASCII sem diferenciar maiúsculas/minúsculas e prioridade de patches. A leitura dos recursos acontece sob demanda.
- Além dos formatos de imagem comuns via SDL/FFmpeg, é possível decodificar **TLG5 RGB/RGBA**, inclusive dentro de TLG0. Há testes sintéticos para pixels, LZSS e arquivos truncados.
- Os comandos KAG `[bg storage=...]`, `[image layer=... storage=...]`, `[freeimage]` e `[layopt visible=false]` alteram as camadas realmente desenhadas. Algumas transições ainda ocorrem imediatamente.
- Um **menu provisório do Kotonoha**, com imagem compatível opcional, aparece mesmo quando não há roteiro `.ks` executável, exibindo a limitação em vez da tela vazia.
- Índices malformados, segmentos fora dos limites e encadeamentos cíclicos são rejeitados. Um XP3 inválido não bloqueia a montagem automática dos outros.

## Limitações conhecidas

**O menu provisório não é o menu original de NEKOPARA.** O fluxo original de KiriKiri inicia pelo `startup.tjs` e depende do interpretador TJS2, plugins e interface específica do título. O porte ainda não executa TJS2 genérico nem toda a semântica de arquivos PSB/SCN compilados. **TLG6 e filtros/criptografia XP3 específicos de jogos permanecem sem suporte.** Saves, efeitos, traduções e áudio ainda não possuem paridade com o jogo original.

Essas melhorias não foram validadas em uma instalação comercial de NEKOPARA. Montar o XP3 ou mostrar o menu provisório não comprova jogabilidade completa. Os testes do repositório usam dados sintéticos.

## Compilação e testes

```sh
cmake -S . -B build -DKOTONOHA_BUILD_KIRIKIRI=ON -DBUILD_TESTING=ON
cmake --build build --target KirikiriXp3Tests Kirikiri
ctest --test-dir build -R '^KirikiriXp3$' --output-on-failure
```

Use uma pasta legítima de jogo com scripts KAG ou pacotes XP3. Execute o launcher ou passe `--kirikiri /caminho/para/dados`. Não versione assets comerciais.

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)
