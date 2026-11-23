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

## Compatibilidade nesta branch

- Arquivos SEEN carregados por VFS/ISO agora preservam o REGNAME do
  Gameexe.ini ao selecionar o formato de script RealLive. Isso corrige uma
  incompatibilidade concreta na inicialização de roteiros com a segunda
  camada de codificação, como a edição Full Voice.
- Entrada e retorno ao menu utilizam o cenário SEEN_MENU configurável, sem
  pressupor que todo jogo use o cenário 9032.
- Há validação de limites para scripts sintéticos comprimidos e truncados.

Essas mudanças visam melhorar a abertura do CLANNAD e o frontend de
planetarian. **Não comprovam jogabilidade completa:** ainda faltam ensaios
com as edições originais, escolhas, imagens, áudio, saves e transições.

## Investigação detalhada

[Resumo do estado do port](STATUS.pt-BR.md) · [Investigação detalhada](PORT_STATUS.md). Resultados históricos se aplicam aos assets/revisão analisados e podem conter informações substituídas depois.

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)

## Segurança do fluxo de execução

Chamadas locais e entre cenários agora impõem um limite de profundidade para
a pilha de retorno. Uma tentativa de entrar em um cenário inexistente não deve
deixar um retorno fantasma; um retorno inválido não deve consumir o quadro
salvo antes da validação. O teste de sessão mantém suas asserções ativas
também em builds `Release`, mas depende de dados RealLive originais.

## `case` e `switch` em RealLive

O opcode de seleção procura uma condição explícita correspondente antes de
usar `()` como caso padrão, mesmo quando esse caso aparece primeiro no
bytecode. Adicionado o teste sintético `RealLive.CaseSelection`, que roda
sem o arquivo `Seen.txt`:

```sh
cmake --build build --target RealLiveCaseSelectionTests
ctest --test-dir build -R RealLive.CaseSelection --output-on-failure
```

## Strings Shift-JIS e bytes nulos

As operações de comprimento, comparação e substring agora preservam bytes
NUL existentes na memória da VM, contam caracteres Shift-JIS em tempo
linear e evitam overflow em somas de offsets de substring. O teste sintético
`RealLive.StringOps` não depende do `Seen.txt`.

```sh
cmake --build build --target RealLiveStringOpsTests
ctest --test-dir build -R RealLive.StringOps --output-on-failure
```

## Novos opcodes de strings numéricas

Foram implementados os opcodes `Str.digits` (19) e `Str.digit` (20),
respeitando o índice decimal a partir da direita (base 1). A conversão usa
um inteiro de 64 bits intermediário para tratar `INT_MIN` sem overflow;
índices inválidos são rejeitados antes de modificar o destino. Esses são
comandos de manipulação de valores, não novas instruções de interface.

```sh
cmake --build build --target RealLiveDecimalDigitsTests
ctest --test-dir build -R RealLive.DecimalDigits --output-on-failure
```

## Limpeza atômica de strings

O overload de intervalo do opcode `Str.strclear` valida todas as posições
antes de apagar qualquer variável. Um índice fora dos limites ou uma faixa
invertida não pode deixar parte das strings persistentes apagadas. Adicionado
o teste C++17 independente `RealLive.BoundedRange`, sem arquivos de jogo:

```sh
cmake --build build --target RealLiveBoundedRangeTests
ctest --test-dir build -R RealLive.BoundedRange --output-on-failure
```

## Cópia parcial de strings Shift-JIS

O overload de `Str.strcpy` com contagem limita o número de **caracteres**
Shift-JIS completos, não o número de bytes. Assim não corta um caractere
multibyte e não produz uma sequência inválida em diálogos ou variáveis. A
regressão foi adicionada ao `RealLiveStringOpsTests`, sem dados comerciais.

## Controles de entrada (macOS, iOS e gamepads SDL3)

- **Teclado:** Enter/Espaço avança; segure Ctrl esquerdo/direito para
  skip contínuo (quando permitido pelo script); S alterna o skip de textos já
  lidos; A alterna o modo automático; Page Up abre o histórico; Esc abre o menu.
- **Gamepad:** Leste confirma/avança, Sul segura o skip durante diálogos,
  ambos os gatilhos também seguram skip, Oeste alterna o skip de mensagens
  lidas, ombro direito alterna automático, Back/ombro esquerdo abre o histórico
  e Start abre o menu de pausa. Direcional e analógico esquerdo navegam pelo
  título original e pelas escolhas. A preferência global de troca A/B é respeitada.
- **Toque:** o toque é ativado na soltura; o diálogo e a barra de controles
  original respeitam essa sequência. Arrastar, cancelar um contato ou usar
  múltiplos dedos não deve avançar o diálogo nem disparar botões. Gestos de
  menu/histórico continuam sob responsabilidade da engine compartilhada.

O skip de textos já lidos continua entre páginas **lidas** e interrompe ao
encontrar uma inédita. Segurar Ctrl ou o botão/gatilho do gamepad funciona
independentemente e pode pular texto inédito, desde que o script RealLive não
tenha desabilitado o skip por Ctrl.

Teste sintético sem arquivos comerciais:

```sh
cmake --build build --target RealLiveInputTests
ctest --test-dir build -R '^RealLive.Input$' --output-on-failure
```

Ainda é necessário validar os layouts e áreas de toque nos dispositivos
macOS/iOS com cada edição original de RealLive.

## A música começa apenas em um opcode BGM explícito

As cenas de boot/logo do AIR (`SEEN` 9030/9031/9032) **não** contêm nenhum
comando do módulo 20, portanto o splash permanece silencioso. A primeira
solicitação explícita é `1:20:0("BGM00")` no nó `170:246`; nada em
`Gameexe.ini` (`#BGM_MODE`, `#DSTRACK`) inicia música por conta própria.
`bgmStartsPlayback()` só habilita a reprodução para os opcodes 0..3 do módulo
20, e `bgmGainAfterStop()` restaura o ganho após `bgmStop`.
`RealLive.BgmCompat` verifica as duas regras:

```sh
cmake --build build --target RealLive
ctest --test-dir build -R RealLive --output-on-failure
```
