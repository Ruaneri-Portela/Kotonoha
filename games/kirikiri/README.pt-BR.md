# Kirikiri2 / KirikiriZ

[English (US)](README.md) | **Português (Brasil)**

Porte C++17 de KiriKiri para o Kotonoha: montagem XP3, interpretador KAG experimental e apresentação básica de imagens, diálogos, menus, escolhas e áudio.

## Compatibilidade implementada

- O provedor XP3 aceita índices canônicos sem compressão ou com zlib, índices encadeados, vários segmentos por arquivo (inclusive mistos), nomes Unicode, busca ASCII sem diferenciar maiúsculas/minúsculas, resolução de discrepâncias em pacotes protegidos (permitindo indexar pacotes como `data.xp3`, `emote.xp3`, `evimage.xp3`, `voice.xp3` do NEKOPARA) e prioridade de patches. A leitura dos recursos acontece sob demanda.
- Além dos formatos comuns via SDL/FFmpeg, há decodificação de **TLG5 RGB/RGBA** e **TLG6 com entropia Golomb**, inclusive encapsulamento TLG0. O decodificador TLG6 inclui limites para sequências Golomb, filtros LZSS, transformações de cores e predição MED/AVG; testes sintéticos cobrem pixels, encapsulamentos e arquivos malformados.
- Os comandos KAG `[bg storage=...]`, `[image layer=... storage=...]`, `[freeimage]` e `[layopt visible=false]` alteram as camadas realmente desenhadas. Algumas transições ainda ocorrem imediatamente.
- Um **menu provisório do Kotonoha** aparece mesmo sem `.ks`. Além do analisador estático, existe um **interpretador experimental e limitado de TJS2 fonte** para variáveis, expressões numéricas e strings, condicionais, laços limitados, extração de tabelas de constantes de bytecode compilado TJS2 (`TJS2100\0`) e chamadas `Scripts.execStorage` / `Scripts.evalStorage` entre scripts. Ele reconhece o roteiro KAG referenciado e o oferece no botão Iniciar; não cria o menu original em TJS2. Requisições de plugins são apenas registradas, não executadas.
- Índices malformados, segmentos fora dos limites e encadeamentos cíclicos são rejeitados. Um XP3 inválido não bloqueia a montagem automática dos outros.

## Limitações conhecidas

**O menu provisório não é o menu original de NEKOPARA.** O fluxo original de KiriKiri inicia pelo `startup.tjs` e depende do interpretador TJS2, plugins e interface específica do título. O porte ainda não executa TJS2 genérico nem toda a semântica de arquivos PSB/SCN compilados. **O método bruto 3 do TLG6 é experimental e só é aceito com tamanho exato. Gamma (método 1), LZSS modificado (método 2) e filtros específicos XP3 seguem pendentes.** O próprio decodificador KiriKiri Z original rejeita métodos TLG6 não-Golomb. Saves, efeitos, traduções e áudio ainda não possuem paridade com o jogo original.

Classes TJS2 complexas, comportamento nativo completo de Window/Layer, plugins e bytecode TJS2 compilado **não foram implementados**; a VM parcial interrompe o script quando encontra sintaxe não suportada. Essas melhorias não foram validadas em uma instalação comercial de NEKOPARA. Montar o XP3 ou mostrar o menu provisório não comprova jogabilidade completa. Os testes do repositório usam dados sintéticos.

## HTTP(S) e WebAssembly

No navegador, um servidor HTTP comum não permite enumerar arquivos
desconhecidos. O detector Kirikiri agora consulta caminhos conhecidos como
`data.xp3`, `patch.xp3`, `startup.tjs` e `scenario/first.ks` diretamente.
Sem índice de diretório, o provedor XP3 consulta nomes comuns e lê índice
e segmentos sob demanda com Fetch/Range. Entradas KAG soltas também são
consultadas por nome. Pacotes com nomes diferentes ainda podem exigir
índice/manifesto ou pasta local. A detecção não comprova compatibilidade
integral com o jogo.

O servidor precisa aceitar Range (206), Content-Range e CORS apropriado;
veja [guia WebAssembly](../../docs/BUILD_WEB.pt-BR.md).

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

**Ainda não é TJS2 completo:** há suporte inicial a classes declaradas no script, funções anônimas simples para eventos e objetos Window/Layer virtuais. Permanecem pendentes closures léxicas, herança arbitrária entre classes, propriedades avançadas, interfaces nativas completas, menu original do KiriKiri e execução de bytecode. Construtores nativos desconhecidos
geram um erro explícito, sem simular um menu original. Os testes dessa etapa
são sintéticos; os recursos não foram validados com assets comerciais.

## Ordem das escolhas KAG

Tags de escolha do subconjunto KAG implementado pelo Kotonoha (`[select]` e `[choices]`) agora apresentam `option1`, `option2`, `option10` na ordem numérica e não tratam atributos não numéricos como `optionColor` como opções. O teste sintético também verifica que a callback da escolha retoma o script. Isso não substitui o interpretador TJS2 nem comprova a interface original de NEKOPARA.

## Ponte gráfica experimental TJS2 → Kotonoha

O subconjunto TJS2 inclui agora **objetos gráficos virtuais** `new Window()`,
`window.primaryLayer` e `new Layer(window[, parentLayer])`. `Layer` aceita
`left`, `top`, `width`, `height`, `visible`, `enabled`, `opacity`, `order`,
`loadImages("image/arquivo.tlg")`, `setPos(x,y)`, `setSize(w,h)` e
`show()`/`hide()`. As imagens vêm do provedor XP3 e são compostas na cena
do Kotonoha, com origem relativa aos pais e ordem de camadas.

Uma função TJS2 **nomeada ou anônima** (sem captura de escopo léxico) pode ser atribuída a `layer.onClick`, `layer.onMouseDown`, `layer.onMouseEnter` ou `layer.onMouseLeave`. O runtime realiza hit-testing em coordenadas lógicas, dispara os handlers de clique e de entrada/saída do mouse na VM preservada
e reconhece `Scripts.execStorage("roteiro.ks")` para entrar no KAG. Se o
bootstrap produzir camadas com imagens e regiões clicáveis e terminar
sem erro, a interface gerada pelo script substitui o menu provisório.
Em outros casos, o menu provisório permanece acessível.

```tjs
var window = new Window();
var button = new Layer(window, window.primaryLayer);
button.setPos(100, 80);
button.setSize(320, 100);
button.loadImages("image/title.tlg");
function startGame() { Scripts.execStorage("scenario/start.ks"); }
button.onClick = startGame;
```

**Limitações:** viewport compatível de 1280×720, sem janelas do SO nem DLLs.
Closures completas, eventos de teclado e movimento contínuo do mouse, timers, animações, classes nativas reais e bytecode TJS2 seguem pendentes. Caso as imagens ou callbacks do menu TJS2 falhem, o menu de compatibilidade é restaurado. Isto **não comprova o
menu original de NEKOPARA**; os testes usam arquivos sintéticos.

## Classes e métodos TJS2 (subconjunto)

O interpretador agora aceita `class Nome { function Nome(...) {...} function metodo(...) {...} }` e `class Nome extends Layer`, `Window` ou `Dictionary`, com instâncias, `this`, métodos associados ao objeto, `return` e construtores. Métodos de classes derivados de `Layer` podem ser usados diretamente como handlers de clique, preservando a identidade de cada instância.

```tjs
class TitleButton extends Layer {
  function TitleButton(owner) {
    this.setPos(100, 80);
    this.setSize(320, 100);
    this.loadImages("image/title.tlg");
    this.onClick = this.start;
  }
  function start() { Scripts.execStorage("scenario/start.ks"); }
}
var window = new Window();
var button = new TitleButton(window);
```

**Limites explícitos:** até 128 classes, 64 métodos por classe, 256 instâncias, 2048 métodos associados e profundidade de chamadas limitada. Herança de classes de usuário, `super`, closures, eventos nativos completos e bytecode ainda não são interpretados. Esses testes são sintéticos; não representam validação de uma instalação comercial.
