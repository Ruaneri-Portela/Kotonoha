# Cobertura dos comandos nativos do Pure Mail (16/11/2026)

[English (US)](NATIVE_COVERAGE.md)

## Instruções sem implementação identificadas nos logs

| SYS | Local informado | Estado |
| --- | --- | --- |
| 18 | `start.obj:5` | Sem implementação; semântica nativa desconhecida |
| 24 | `start.obj:7` | Sem implementação; semântica nativa desconhecida |
| 20 | `start.obj:11` | Sem implementação; semântica nativa desconhecida |
| 216 | `start.obj:14` | Sem implementação; semântica nativa desconhecida |
| 17 | `start.obj:899` | Sem implementação; semântica nativa desconhecida |
| 212 | `extra.OBJ:83` | Sem implementação; semântica nativa desconhecida |

Esses avisos vêm do fallback de `PmailGame::native`, não do áudio.
Nenhum dos seis IDs tem implementação nos handlers atualmente
documentados. **Não mascarar os avisos nem inventar um no-op**: chamadas
nativas podem alterar flags, cenas, entrada, apresentação ou a execução.

## Diagnóstico novo

Toda chamada desconhecida é identificada por **ID do SYS, objeto de
script e PC de retorno**, com contagem limitada e assinatura dos argumentos.
Valores numéricos e das variáveis são registrados; strings aparecem apenas
como referências de objeto/índice, sem expor diálogo ou nomes do jogador.
O limite é 512 locais distintos, com no máximo 24 argumentos por assinatura.

Por padrão o aviso continua aparecendo uma vez por ID. Para registrar
também cada local distinto, ativar `PMAIL_NATIVE_VERBOSE=1`. O teste
de rotas `Pmail.RouteAudit` exporta linhas TSV
`PMAIL_UNHANDLED_SITE` com locais, assinaturas e contadores.
A auditoria continua **falhando quando encontra comandos não implementados**;
isso não é uma alegação de compatibilidade integral.

## Validação

- `Pmail.ComputerUi`: teste sintético sem assets originais para os seis
  IDs dos logs, deduplicação, assinaturas sem texto e roteamento de áudio.
- `Pmail.RouteAudit`: com dados já fornecidos localmente, permite
  identificar todos os locais visitados que invocam comandos desconhecidos.
- Antes de implementar cada SYS, identificar o contrato dos parâmetros,
  escrita de variáveis, comportamento de espera e efeitos observáveis sobre
  cena/áudio/estado, comparando com o executável de referência.
- Nenhuma semântica nativa foi inventada para os seis comandos desconhecidos.
