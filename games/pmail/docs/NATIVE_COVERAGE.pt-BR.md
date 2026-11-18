# Cobertura dos comandos nativos do Pure Mail (16/11/2026)

[English (US)](NATIVE_COVERAGE.md)

## Comandos nativos recuperados

| SYS | Local informado | Estado |
| --- | --- | --- |
| 18 | `start.obj:5`, `PRO1.OBJ:3` | Implementado: define referência de texto atual (`0x422bb0`) |
| 24 | `start.obj:7` | Implementado: reinicia exibição de mensagem e chat (`0x422410`) |
| 20 | `start.obj:11` | Implementado: libera superfícies de texto (`0x4223f0`) |
| 216 | `start.obj:14` | Implementado: define modo de texto (`0x4227e0`) |
| 17 | `start.obj:899` | Implementado: reinicia fila de mensagens e modo (`0x422570`) |
| 19 | `PRO1.OBJ:5` | Implementado: superfícies de texto prontas (`0x4223e0`) |
| 65 | `PRO1.OBJ:809` | Implementado: cópia de backbuffer (`0x424490`) |
| 137 | `PRO1.OBJ:2413` | Implementado: reprodução de som por dia no barramento SE (`0x4248f0`) |
| 212 | `extra.OBJ:83` | Implementado: inicialização do modo extra/omake |

Todos os nove comandos nativos previamente observados foram analisados no `pmail.exe` e implementados no runtime do porte.

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
- Nenhuma semântica nativa foi inventada para os nove comandos desconhecidos.

## Outros avisos registrados no macOS

- `com.apple.linkd.autoShortcut` / AppIntents / código 4097:
  falha de comunicação com serviço do sistema. Não é um SYS da VM.
- `[aqsrv] AudioQueueInternalStop_Sync -66671` e
  `AudioObjectGetPropertyData: no object with given ID 0`:
  avisos do CoreAudio. Se houver cortes no som, testar o dispositivo de
  saída padrão, mudanças de dispositivo e a inicialização/desligamento
  do áudio separadamente. O log isolado não comprova um bug da VM.
- `AddInstanceForFactory ... F8BB1C28-...`: mensagem de um plugin do
  CoreAudio observada em diferentes aplicativos. Também não é um SYS.
- `Upscaler backend=2` e `Anime4K ... initialized`: mensagens
  informativas de inicialização da renderização.
