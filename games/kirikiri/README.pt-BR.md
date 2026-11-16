# Kirikiri2 / KirikiriZ

[English (US)](README.md) | **Português (Brasil)**

Porte C++17 de KiriKiri para o Kotonoha: montagem XP3, interpretador KAG experimental e apresentação básica de imagens, diálogos, menus, escolhas e áudio.

## Compatibilidade implementada

- O provedor XP3 aceita índices canônicos sem compressão ou com zlib, índices encadeados, vários segmentos por arquivo (inclusive mistos), nomes Unicode, busca ASCII sem diferenciar maiúsculas/minúsculas e prioridade de patches. A leitura dos recursos acontece sob demanda.
- Além dos formatos comuns via SDL/FFmpeg, há decodificação de **TLG5 RGB/RGBA** e **TLG6 com entropia Golomb**, inclusive encapsulamento TLG0. O decodificador TLG6 inclui limites para sequências Golomb, filtros LZSS, transformações de cores e predição MED/AVG; testes sintéticos cobrem pixels, encapsulamentos e arquivos malformados.
- Os comandos KAG `[bg storage=...]`, `[image layer=... storage=...]`, `[freeimage]` e `[layopt visible=false]` alteram as camadas realmente desenhadas. Algumas transições ainda ocorrem imediatamente.
- Um **menu provisório do Kotonoha** aparece mesmo quando não há `.ks` executável. Um novo **analisador estático de startup.tjs** identifica o script de abertura, dependências literais (Scripts.execStorage/evalStorage e plugins), arquivos ausentes e referências dinâmicas. Os diagnósticos deixam claro que TJS2 não é executado.
- Índices malformados, segmentos fora dos limites e encadeamentos cíclicos são rejeitados. Um XP3 inválido não bloqueia a montagem automática dos outros.

## Limitações conhecidas

**O menu provisório não é o menu original de NEKOPARA.** O fluxo original de KiriKiri inicia pelo `startup.tjs` e depende do interpretador TJS2, plugins e interface específica do título. O porte ainda não executa TJS2 genérico nem toda a semântica de arquivos PSB/SCN compilados. **Métodos de entropia TLG6 diferentes de Golomb e filtros/criptografia XP3 específicos de jogos permanecem sem suporte.** Saves, efeitos, traduções e áudio ainda não possuem paridade com o jogo original.

Essas melhorias não foram validadas em uma instalação comercial de NEKOPARA. Montar o XP3 ou mostrar o menu provisório não comprova jogabilidade completa. Os testes do repositório usam dados sintéticos.

## Compilação e testes

```sh
cmake -S . -B build -DKOTONOHA_BUILD_KIRIKIRI=ON -DBUILD_TESTING=ON
cmake --build build --target KirikiriXp3Tests Kirikiri
ctest --test-dir build -R '^KirikiriXp3$' --output-on-failure
```

Use uma pasta legítima de jogo com scripts KAG ou pacotes XP3. Execute o launcher ou passe `--kirikiri /caminho/para/dados`. Não versione assets comerciais.

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)

## Ordem das escolhas KAG

Tags de escolha do subconjunto KAG implementado pelo Kotonoha (`[select]` e `[choices]`) agora apresentam `option1`, `option2`, `option10` na ordem numérica e não tratam atributos não numéricos como `optionColor` como opções. O teste sintético também verifica que a callback da escolha retoma o script. Isso não substitui o interpretador TJS2 nem comprova a interface original de NEKOPARA.
