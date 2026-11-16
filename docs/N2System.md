# N2System — formato e status de integração

O suporte atual é formado pelos módulos independentes
`Modules::Providers::N2` e `Modules::VMs::N2System`. **Não é um
executável de jogo nem uma VM NsScript completa.** Os testes abaixo usam
dados sintéticos, sem arquivos comerciais.

## Arquivos AFS e NPA

O provider lê AFS e NPA simples **não compactados e não criptografados**.
A indexação de arquivos utiliza nomes normalizados para ASCII e separadores
`/` / `\\`, preservando bytes multibyte legados.

- O parser rejeita nomes e aliases inseguros (segmentos `.` / `..`,
  barras repetidas, prefixos absolutos, `:`, NUL e controles), antes de
  incluir o arquivo no índice.
- A importação de aliases AFS `nome,índice` continua transacional: um
  registro inválido não adiciona nomes parciais.
- Streams de assets permitem leitura e seek sem duplicar cada arquivo
  descompactado; o arquivo de archive montado ainda está **inteiro na
  memória**.
- NPA criptografado/compactado ainda é rejeitado explicitamente.

## NsScript

O `ScriptIndex` identifica capítulos, labels e chamadas sem executar o
script. A contagem de linhas também suporta arquivos com apenas `CR`
(além de `LF` e `CRLF`), inclusive comentários e literais de texto.

A semântica de invocações, transições, áudio, save/load e a VM completa ainda
não estão implementadas.

## Testes

Em um build CMake com `BUILD_TESTING=ON` e os módulos habilitados:

```sh
ctest --test-dir build -R 'Kotonoha.N2System.(Archives|ScriptIndex)' \
    --output-on-failure
```

Para compilar diretamente os parsers C++17 e seus testes sem SDL:

```sh
g++ -std=c++17 -Imodules/providers/n2/include \
  modules/providers/n2/src/Archive.cpp \
  modules/providers/n2/tests/ArchiveTest.cpp -o /tmp/n2-archive-test
/tmp/n2-archive-test

g++ -std=c++17 -Imodules/vms/n2system/include \
  modules/vms/n2system/src/ScriptIndex.cpp \
  modules/vms/n2system/tests/ScriptIndexTest.cpp -o /tmp/n2-script-test
/tmp/n2-script-test
```

## Navegação experimental por capítulos

O `ScriptCursor` percorre chamadas identificadas pelo `ScriptIndex` com
operações `peek()`, `advance()`, `enterChapter()` e `jumpToLabel()`.
Uma referência de capítulo/label inexistente não altera a posição atual. Isso
viabiliza ferramentas de inspeção e pontos de entrada para uma futura VM.

**O cursor não executa NsScript.** Os comandos dentro de condições e
expressões são ainda indexados em ordem textual, sem avaliação de branches,
loops ou semântica de comandos. Não se deve apresentar essa navegação como
prova de jogabilidade.

## Executor experimental NsScript (C++17)

`N2System::ScriptExecutor` conecta o `ScriptCursor` a callbacks do host,
com orçamento de comandos por chamada de `run()`, pausa explícita
(`VmActionKind::Pause` / `resume()`), término e saltos para capítulos
ou labels. Em vez de executar a lista de chamadas indiscriminadamente,
o compilador **valida todo o script antes da primeira ação**. O subconjunto
aceito é: capítulos, labels no início da linha e chamadas diretas de
funções com argumentos literais (strings ou inteiros decimais), terminadas
por ponto e vírgula. Os callbacks decidem o significado de cada função;
nomes como `Show` e `WaitKey` são exemplos de integração, não um contrato
afirmado para todos os títulos N2System.

```cpp
N2System::ScriptExecutor vm(source, "main",
  [&](const N2System::Invocation& call,
      const std::vector<std::string>& args) -> N2System::VmAction {
    if (call.function == "Show") {
      drawText(args.at(0)); // callback implementado pelo frontend
      return {};
    }
    if (call.function == "WaitKey")
      return {N2System::VmActionKind::Pause, {}};
    throw std::runtime_error("NsScript command unsupported");
  });
vm.run(1024); // não bloqueia indefinidamente a thread da interface
// Na próxima confirmação do jogador:
vm.resume();
vm.run(1024);
```

Qualquer fluxo `if`, loop, atribuição ou expressão aninhada é rejeitado no
pré-processamento. Isso é intencional: o N2System ainda **não possui VM
NsScript completa** nem frontend de jogo integrado. Teste independente:

```sh
cmake --build build --target KotonohaN2SystemExecutorTests
ctest --test-dir build -R Kotonoha.N2System.Executor --output-on-failure
```

## Segurança de vida útil do executor

O `ScriptExecutor` guarda um `ScriptCursor` que referencia o índice NsScript
pertencente ao próprio executor. Agora são proibidas cópias e movimentações
desse objeto para evitar ponteiros suspensos ao mover/destruir a instância
original. As garantias são verificadas por asserções de compilação no teste
de execução sintético. O frontend deve conservar o executor enquanto houver
callbacks ou estado NsScript ativo.

## Chamadas de subrotina experimentais

O executor NsScript aceita os resultados de callback `CallLabel`,
`CallChapter` e `Return` para chamadas com retorno entre labels e
capítulos. A pilha guarda o capítulo e o índice exato da próxima instrução;
o limite é 1024 quadros, e um destino ausente não altera o cursor nem a
pilha. `EnterChapter` (salto permanente) descarta retornos pendentes;
`Finish` também limpa a pilha.

Essas são **ações da VM propostas para integração com o frontend**, não
uma interpretação automática de todo o dialeto NsScript. Comandos reais,
expressões, loops e condições ainda precisam ser identificados e suportados.
Os testes sintéticos verificam chamada local, chamada entre capítulos,
retorno, ausência de destino e recursão excessiva.
