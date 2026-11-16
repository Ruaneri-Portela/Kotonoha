# Kirikiri2 / KirikiriZ

[English (US)](README.md) | **Português (Brasil)**

Porte C++17 de KiriKiri para o Kotonoha: montagem XP3, interpretador KAG experimental e apresentação básica de imagens, diálogos, menus, escolhas e áudio.

## Compatibilidade implementada

- O provedor XP3 aceita índices canônicos sem compressão ou com zlib, índices encadeados, vários segmentos por arquivo (inclusive mistos), nomes Unicode, busca ASCII sem diferenciar maiúsculas/minúsculas e prioridade de patches. A leitura dos recursos acontece sob demanda.
- Além dos formatos comuns via SDL/FFmpeg, há decodificação de **TLG5 RGB/RGBA** e **TLG6 com entropia Golomb**, inclusive encapsulamento TLG0. O decodificador TLG6 inclui limites para sequências Golomb, filtros LZSS, transformações de cores e predição MED/AVG; testes sintéticos cobrem pixels, encapsulamentos e arquivos malformados.
- Os comandos KAG `[bg storage=...]`, `[image layer=... storage=...]`, `[freeimage]` e `[layopt visible=false]` alteram as camadas realmente desenhadas. Algumas transições ainda ocorrem imediatamente.
- Um **menu provisório do Kotonoha** aparece mesmo sem `.ks`. Além do analisador estático, existe um **interpretador experimental e limitado de TJS2 fonte** para variáveis, expressões numéricas e strings, condicionais, laços limitados e chamadas `Scripts.execStorage` / `Scripts.evalStorage` entre scripts. Ele reconhece o roteiro KAG referenciado e o oferece no botão Iniciar; não cria o menu original em TJS2. Requisições de plugins são apenas registradas, não executadas.
- Índices malformados, segmentos fora dos limites e encadeamentos cíclicos são rejeitados. Um XP3 inválido não bloqueia a montagem automática dos outros.

## Limitações conhecidas

**O menu provisório não é o menu original de NEKOPARA.** O fluxo original de KiriKiri inicia pelo `startup.tjs` e depende do interpretador TJS2, plugins e interface específica do título. O porte ainda não executa TJS2 genérico nem toda a semântica de arquivos PSB/SCN compilados. **O método bruto 3 do TLG6 é experimental e só é aceito com tamanho exato. Gamma (método 1), LZSS modificado (método 2) e filtros específicos XP3 seguem pendentes.** O próprio decodificador KiriKiri Z original rejeita métodos TLG6 não-Golomb. Saves, efeitos, traduções e áudio ainda não possuem paridade com o jogo original.

Classes, métodos, objetos/janelas nativos, plugins e bytecode TJS2 compilado **não foram implementados**; a VM parcial interrompe o script quando encontra sintaxe não suportada. Essas melhorias não foram validadas em uma instalação comercial de NEKOPARA. Montar o XP3 ou mostrar o menu provisório não comprova jogabilidade completa. Os testes do repositório usam dados sintéticos.

## Compilação e testes

```sh
cmake -S . -B build -DKOTONOHA_BUILD_KIRIKIRI=ON -DBUILD_TESTING=ON
cmake --build build --target KirikiriXp3Tests Kirikiri
ctest --test-dir build -R '^KirikiriXp3$' --output-on-failure
```

Use uma pasta legítima de jogo com scripts KAG ou pacotes XP3. Execute o launcher ou passe `--kirikiri /caminho/para/dados`. Não versione assets comerciais.

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)

## Funções e objetos Dictionary no subconjunto TJS2

A VM experimental em `modules/vms/tjs2` agora aceita funções nomeadas
com parâmetros e `return`, variáveis locais por chamada, recursão limitada,
laços `for` limitados e objetos compartilhados `new Dictionary()` com leitura/escrita de
propriedades aninhadas. Isso permite calcular configurações e o roteiro
KAG inicial antes de apresentar o menu provisório do Kotonoha.

**Ainda não é TJS2 completo:** expressões de função, classes definidas
pelo jogo, objetos nativos `Window`/`Layer`, o menu original do
KiriKiri e bytecode continuam pendentes. Construtores nativos desconhecidos
geram um erro explícito, sem simular um menu original. Os testes dessa etapa
são sintéticos; os recursos não foram validados com assets comerciais.

## Ordem das escolhas KAG

Tags de escolha do subconjunto KAG implementado pelo Kotonoha (`[select]` e `[choices]`) agora apresentam `option1`, `option2`, `option10` na ordem numérica e não tratam atributos não numéricos como `optionColor` como opções. O teste sintético também verifica que a callback da escolha retoma o script. Isso não substitui o interpretador TJS2 nem comprova a interface original de NEKOPARA.
