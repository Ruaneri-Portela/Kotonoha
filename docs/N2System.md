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
