# Compatibilidade dos motores — referências e progresso (novembro de 2026)

Este documento registra avanços **incrementais** no Kotonoha com base em outros interpretadores e ferramentas. Testes sintéticos e leitura de formatos não comprovam compatibilidade 1:1 com jogos comerciais.

## Referências e licenças

| Referência | Módulo do Kotonoha | Política de licença |
| --- | --- | --- |
| [rlvm](https://github.com/eglaysher/rlvm) | `modules/vms/reallive` | O projeto rlvm completo usa GPL-3.0-or-later; foi consultado como referência comportamental, sem incorporar código GPL novo ao Kotonoha MIT. Os arquivos já incluídos de `libreallive` têm seus próprios cabeçalhos MIT. |
| [NitroSharp](https://github.com/CommitteeOfZero/nitrosharp) | `modules/vms/n2system` | NitroSharp é MIT, mas componentes externos podem ter outras licenças. O indexador foi implementado em C++17, sem importar o runtime C#/.NET. |
| [LuckSystem](https://github.com/wetor/LuckSystem) | `games/luca-system` | MIT; referência para estrutura dos scripts, padding e mensagens AIR. |
| [Kirikiroid2Yuri](https://github.com/YuriSizuku/Kirikiroid2Yuri) | `modules/vms/kag` e `modules/providers/xp3` | Contém componentes e avisos de terceiros diversos. Não copiar bibliotecas sem auditoria por arquivo. |

## Implementado na `dev`

- **RealLive:** destinos de salto verificados antes de modificar a pilha de chamadas; proteção contra destino nulo.
- **LucaSystem:** tamanho real das instruções `CodeLine`, preservação dos bytes de padding, rejeição de scripts incompletos e suporte a campos japoneses vazios nas mensagens AIR. O serializador recompõe exatamente os bytes de instruções desconhecidas, permitindo validar um round-trip sem reescrever opcodes.
- **Kirikiri/KAG:** comandos `@nome`, comentários iniciados por `;` no início da linha, escape `[[`, destinos `*label`, chamadas `[call target=*label]`, retornos `[return]` e estado de execução para que o host salve/restaure a pilha local.
- **N2System:** indexação C++17 de capítulos, rótulos e chamadas NsScript com offsets, linhas, BOM UTF-8, comentários, strings, blocos aninhados e detecção de erros. **Ainda não executa jogos N2System**, nem lê NPA/AFS.

## Próximas etapas técnicas

1. **RealLive:** ampliar compatibilidade de opcodes, snapshots, arquivos de áudio KOE/OVK/NWK e rlBabel, com validação sobre dados legalmente obtidos.
2. **N2System:** criar providers NPA/AFS, lexer/AST tipado, execução de scripts e integração de áudio/vídeo/efeitos.
3. **LucaSystem:** tabelas de opcodes por título, escrita de parâmetros, recálculo de saltos e round-trip de edição/reimportação; ampliar decodificação CZ e vozes.
4. **Kirikiri:** chamadas entre arquivos, expressões e condicionais TJS2/KAG, plugins e variantes adicionais de XP3.

## Validação

Depois de configurar as dependências e selecionar os três ports existentes:

```sh
cmake -S . -B build -DKOTONOHA_BUILD_REALLIVE=ON -DKOTONOHA_BUILD_LUCA_SYSTEM=ON -DKOTONOHA_BUILD_KIRIKIRI=ON -DBUILD_TESTING=ON
cmake --build build --target KotonohaN2SystemScriptTests LucaSystemFormatTests KirikiriXp3Tests RealLiveScriptTests
ctest --test-dir build --output-on-failure -R 'N2System|LucaSystem|KirikiriXp3|RealLiveScript'
```

Os testes de integração de jogos originais necessitam de arquivos adquiridos legalmente. **A compilação completa e os testes CTest ainda não foram executados nesta sessão.** As GitHub Actions do projeto continuam apenas com acionamento manual.

O código-fonte MIT do Kotonoha não altera as licenças de projetos upstream nem autoriza redistribuir recursos comerciais dos jogos.
