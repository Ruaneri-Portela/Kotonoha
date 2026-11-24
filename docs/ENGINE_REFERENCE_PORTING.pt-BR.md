# Compatibilidade dos motores — referências e progresso (novembro de 2026)

> **Registro histórico, não estado atual dos ports.** As descrições abaixo refletem uma etapa anterior à integração dos PRs #71 e #76. Consulte [o estado atual do N2System](../games/n2system/STATUS.pt-BR.md), [o relatório de paridade](NITROSHARP_PORT_PARITY.pt-BR.md) e [a auditoria de licenças](LICENSING.pt-BR.md).

Este documento registra avanços **incrementais** no Kotonoha com base em outros interpretadores e ferramentas. Testes sintéticos e leitura de formatos não comprovam compatibilidade 1:1 com jogos comerciais.

## Referências e licenças

| Referência | Módulo do Kotonoha | Política de licença |
| --- | --- | --- |
| [rlvm](https://github.com/eglaysher/rlvm) | `modules/vms/reallive` | O projeto rlvm completo usa GPL-3.0 (metadados públicos; conferir cabeçalhos de cada arquivo); foi consultado como referência comportamental, sem incorporar código GPL novo ao Kotonoha MIT. Os arquivos já incluídos de `libreallive` têm seus próprios cabeçalhos MIT. |
| [NitroSharp](https://github.com/CommitteeOfZero/nitrosharp) | `modules/vms/n2system` | NitroSharp é MIT, mas componentes externos podem ter outras licenças. O indexador foi implementado em C++17, sem importar o runtime C#/.NET. |
| [LuckSystem](https://github.com/wetor/LuckSystem) | `games/luca-system` | MIT; referência para estrutura dos scripts, padding e mensagens AIR. |
| [Kirikiroid2Yuri](https://github.com/YuriSizuku/Kirikiroid2Yuri) | `modules/vms/kag` e `modules/providers/xp3` | Licença global não classificada nos metadados públicos, com componentes de terceiros. Conferir a licença de cada arquivo antes de copiar. |

## Implementado na `dev`

- **RealLive:** destinos de salto verificados antes de modificar a pilha de chamadas; proteção contra destino nulo.
- **LucaSystem:** tamanho real das instruções `CodeLine`, preservação dos bytes de padding, rejeição de scripts incompletos e suporte a campos japoneses vazios nas mensagens AIR. O serializador recompõe exatamente os bytes de instruções desconhecidas, permitindo validar um round-trip sem reescrever opcodes.
- **Kirikiri/KAG:** comandos `@nome`, comentários iniciados por `;` no início da linha, escape `[[`, destinos `*label`, chamadas `[call target=*label]`, retornos `[return]` e estado de execução para que o host salve/restaure a pilha local.
- **N2System:** indexação C++17 de capítulos, rótulos e chamadas NsScript com offsets, linhas, BOM UTF-8, comentários, strings, blocos aninhados e detecção de erros. **Ainda não executa jogos N2System**. O novo módulo de arquivos lê AFS e algumas variantes NPA.

## Integração adicional na `dev`

- **N2System AFS/NPA:** novos leitores C++17 em `modules/providers/n2`, com adaptador `AssetProvider`, montagem, normalização de nomes, leitura, listagem, verificações de limites e testes sintéticos `KotonohaN2ArchivesTests`. NPA compactado ou com dados criptografados ainda **não é suportado**. Este módulo não transforma o N2System em um jogo executável.
- **Kirikiri:** callbacks agora pertencem à VM, corrigindo uma referência inválida após o carregamento do cenário. Variáveis KAG e subconjunto de condicionais `[if]` / `[elsif]` / `[else]` / `[endif]`; save V3 com pilha local/variáveis e leitura retrocompatível de V1/V2. Os temporizadores retomam a VM na thread de atualização. TJS2 completo e chamadas entre scripts ainda não estão prontos.
- **RealLive:** indicação detalhada de comandos ainda não suportados, incluindo cenário, índice da instrução, tipo, módulo, opcode e overload.
- **LucaSystem:** `WAIT`, `DRAW` e `FADE` passam a consumir a instrução uma única vez; relógios de 64 bits e orçamento de instruções por quadro. Suporte a patches de tamanho fixo sem movimentar offsets. Edição com mudança de tamanho e recálculo de saltos ainda não está disponível.

Os testes de regressão foram adicionados, mas **não foram executados nesta sessão**. Todas as Actions continuam de execução manual.

## Próximas etapas técnicas

1. **RealLive:** ampliar compatibilidade de opcodes, snapshots, arquivos de áudio KOE/OVK/NWK e rlBabel, com validação sobre dados legalmente obtidos.
2. **N2System:** ampliar variantes NPA/AFS, criar um launcher, lexer/AST tipado, execução de scripts e integração de áudio/vídeo/efeitos.
3. **LucaSystem:** tabelas de opcodes por título, escrita de parâmetros, recálculo de saltos e round-trip de edição/reimportação; ampliar decodificação CZ e vozes.
4. **Kirikiri:** chamadas entre arquivos, TJS2 completo, animações, plugins e variantes adicionais de XP3.

## Validação

Depois de configurar as dependências e selecionar os três ports existentes:

```sh
cmake -S . -B build -DKOTONOHA_BUILD_REALLIVE=ON -DKOTONOHA_BUILD_LUCA_SYSTEM=ON -DKOTONOHA_BUILD_KIRIKIRI=ON -DBUILD_TESTING=ON
cmake --build build --target KotonohaN2SystemScriptTests KotonohaN2ArchivesTests LucaSystemFormatTests KirikiriXp3Tests RealLiveScriptTests
ctest --test-dir build --output-on-failure -R 'N2System|LucaSystem|KirikiriXp3|RealLiveScript'
```

Os testes de integração de jogos originais necessitam de arquivos adquiridos legalmente. **A compilação completa e os testes CTest ainda não foram executados nesta sessão.** As GitHub Actions do projeto continuam apenas com acionamento manual.

O código-fonte MIT do Kotonoha não altera as licenças de projetos upstream nem autoriza redistribuir recursos comerciais dos jogos.

## Avanços recentes na `dev`: condicionais, arquivos e saltos

- **KAG:** corrigida a escolha do primeiro `elsif` verdadeiro, inclusive em condicionais aninhados. Indentação vazia não bloqueia mais diálogos e loops com saltos cedem execução após um orçamento finito. O save **KIRIKIRI_SAVE_4** registra o estado dos condicionais e a profundidade de cada chamada, mantendo leitura V1/V2/V3. Não equivale ao TJS2 completo.
- **N2System:** leitura por streaming com seek dos assets AFS/NPA, evitando copiar o asset inteiro uma segunda vez. Arquivos INI de nomes AFS são aceitos por `N2ArchiveAssets::addAfsNames`, com validação de caminhos, índices e conflitos antes de atualizar a montagem. Os testes não dependem mais de `assert`, portanto funcionam em builds Release.
- **LucaSystem:** saltos por offset binário verificam limites reais de instruções; busca por nome restringe-se a `JUMPPOINT`. Chamadas só alteram a pilha quando o destino foi encontrado.
- **RealLive:** diagnósticos de comandos não implementados são limpos ao tentar novamente, mudar de cenário ou restaurar o estado.

### Testar AFS/NPA sem compilar todo o motor

```sh
cmake -S modules/providers/n2/tests -B build/n2-archives -DCMAKE_BUILD_TYPE=Release
cmake --build build/n2-archives
ctest --test-dir build/n2-archives --output-on-failure
```

**Ainda não executei a compilação nem os testes CTest nesta sessão.** Os seis workflows GitHub Actions permanecem exclusivamente manuais.
