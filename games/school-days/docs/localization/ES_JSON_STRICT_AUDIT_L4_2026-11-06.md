# School Days HQ — auditoria espanhol JSON sem fallback inglês (L4)

## RESULTADO FINAL — V4b validada com o parser nativo da L4

**O diagnóstico V1–V3 abaixo é histórico e foi superado.** A tradução
espanhola **V4b** está instalada SOMENTE na L4 Windows e passou pelo
validador do motor, com as seguintes métricas:

| Medição | DLC L×H desligada | DLC L×H ligada |
| --- | ---: | ---: |
| Scripts KTRF inspecionados | 1.855 | 1.855 |
| Cenas com fala/escolha | 1.809 | 1.809 |
| `PRINT_TEXT` + escolhas | **30.968 / 30.968** | **31.241 / 31.241** |
| Opções `SetSELECT` | **485 / 485** | **485 / 485** |
| Chaves faltantes/vazias | **0** | **0** |
| Identidades sem justificativa | **0** | **0** |
| Substituições contextualizadas | **184** | **184** |
| Conflitos de mesma fonte resolvidos | **78** | **78** |

As 184 substituições usam `@ors:<eventOrdinal>:<slot>` dentro da seção
decimal NODE do JSON de Ruaneri. **`eventOrdinal` é a posição na lista
encadeada do parser C++ após ordenação estável por `startTick` e exclusão
de eventos inválidos**, e não a linha física do ORS. Um primeiro V4 que
usava a ordem física falhou no validador nativo (183 lacunas); foi corrigido
com um censo C++ de todas as 30.968 ocorrências e reindexação rigorosa.

QA independente de idioma em 31.241 ocorrências não identificou nenhum
marcador lexical forte de inglês nas saídas espanholas. Todas as ocorrências
idênticas ao original (nomes, pontuação, interjeições, onomatopeias e
L×H espanhol nativo) têm motivo de revisão explícito no JSON adjacente.
Isso valida **os diálogos e escolhas ORS**, não textos gravados em imagens,
vídeos, menus/labels de UI ou todas as nuances semânticas por inspeção humana.

### Instalação privada L4

Pasta:
`D:/Dev/Kotonoha-v3/build/school-days-dev-control4/games/school-days/Release/translations/`

- `es.json` V4b SHA256
  `17f0db1490f97e84e1a83001ee50feee3d82f19749b3625c2f49eaf0fba8495a`.
- `es-reviewed-identities.json` SHA256
  `9eb70df4bfa7593484d4cb6a8b8010f8df6d55efff7d1ad965f8f43864355598`.
- Backups privados da V4 anterior e scripts de auditoria em
  `D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-spanish-final-prime-20261107`.

### Validações realizadas

- `SchoolDaysSpanishChoiceWrapProbe.exe`, parser KTRF/ORS e JSON/manifesto
  **nativos da L4**: OFF/ON completos, zero falta e zero identidade sem revisão.
  Todas as 485 opções usam uma linha sem largura maior que 1024 px ou
  mais de cinco linhas no modelo `SchoolDaysChoiceTextPlan`.
- `SchoolDays.exe` Release recompilado: PASS. CTest selecionado serial
  **9/9 PASS**, incluindo legenda contextual, ASS speaker original, escolhas,
  BackLog, launcher, fades, Handoff e MenuBar.
- Inicialização real do EXE da L4 em quatro perfis isolados com SDL dummy:
  **inglês/espanhol × L×H OFF/ON = 4/4 PASS** (exit 0 e modo efetivo correto).
- Não foi executado nenhum EXE/instalador comercial original. SaveFiles,
  GPKs, modo Android e código do engine genérico fora deste escopo.

**Status:** Espanhol pronto para testes Windows de diálogo/escolhas com
validação estrita de cobertura. Testes interativos de texto sobre vídeo,
escolhas e Replay em todos os capítulos ainda são a etapa de QA visual.
O inglês dos 273 diálogos L×H continua pendente como idioma inglês do DLC.

---

## Histórico do problema original (V1–V3; não representa o estado atual)

**Escopo:** somente a build Windows `build/school-days-dev-control4`.
Dados de idioma/comércio permanecem em `Release/translations` e scratch
privado, fora do Git. Não altera GPK, saves, ASS ou os sete ORS L×H.

## Diagnóstico confirmado

O dicionário Ruaneri por NODE decimal + `nodo` **não garante tradução completa**
só por existir/ser sintaticamente válido. O método genérico
`Translation::text` devolve o original quando uma chave não existe.
Na base comercial de origem inglês isso mostrava inglês em espanhol.
Além disso, antes desta correção, `Runtime::dispatch(PRINT_TEXT)` gravava
o texto original inglês no BackLog, independentemente da tradução visual.

Inventário completo (1855 ORS físicos do KTRF, 1809 com pelo menos uma
fala/escolha, 485 opções de escolha):

| JSON privado | Cobertura segura HQ/30968 | Fallback EN | Original sem alteração |
| --- | ---: | ---: | ---: |
| V1 (`828793b1…`) | 29.842 com chave (29.837 slots estritos emitidos) | 1.126 | 5.266 ocorrências |
| V2 somente eventos espanhóis ativos | 30.544 com chave; 30.542 slots estritos | 424 | 5.289 ocorrências |
| **V3 draft** (`92c16c90…`) | **30.740** | **228** | **5.299 ocorrências** |

As contagens “com chave” refletem o JSON real sob as regras NODE→`nodo`,
incluindo repetições; a contagem de slots estritos exclui conflitos na
extração. As identidades EN=ES incluem nomes, sinais, interjeições e
frases que realmente parecem inglês. Não são certificação de espanhol.

### Proveniência da V3

Foi extraída usando o Script.GPK EN comercial, o `Script.gpk` do patch ES
e o KTRF original, com match por evento, tempo, voz/speaker e contexto.
Recuperações comprovadas incluem 685 associações adicionais por voz/estrutura,
22 eventos repetidos no patch ES com valor espanhol byte-idêntico e
200 linhas de espanhol escritas no patch, mas **comentadas/inativas**. A V3
traduz essas linhas ainda exibidas pelo roteiro inglês base; assim pode
divergir da política de omissão adotada pelos autores do patch espanhol.

Restam 44 falas EN sem pareamento aceito e 78 chaves inglesas conflitantes
por NODE (184 ocorrências), totalizando **228 fallback potenciais**.
Há mais 41 candidatos por evento/tempo/speaker traduzido ainda não
aplicados; 3 não têm candidato espanhol inequívoco. Não inventar texto nem
escolher arbitrariamente uma tradução para chaves repetidas.

Mesmo com a V3, a inspeção lexical encontrou 62 ocorrências / 47 chaves
com sinal forte de inglês entre as saídas iguais ao original. Também é
necessária revisão de 3.701 chaves de identidade na origem espanhola,
incluindo nomes e interjeições legítimos. O espanhol **NÃO é 100%**.

## Correção de comportamento no jogo

- `SchoolDaysTranslationGate::Collect` lê todos os ORS efetivos da sessão
  via KTRF e os assets da opção L×H corrente. `Check` verifica cada
  `PRINT_TEXT` e cada opção `SetSELECT` com escopo NODE→`nodo`.
- Uma seleção JSON não nativa exige 100% das chaves com valores não vazios;
  se houver identidades de texto-fonte ainda não revisadas, também não é
  certificada. `SchoolDays::Launcher::validate` impede Play e a execução
  direta/CLI repete o mesmo gate, inclusive com DLC ON e OFF.
- `SchoolDaysTranslationGate::Translate` recusa chaves ausentes/valor vazio
  durante gameplay, em vez de mostrar inglês. Foi aplicado a **legendas,
  escolhas, BackLog normal, fallback BackLog e BackLog de PlayData**.
- O speaker/character permanece ORIGINAL para preservar cores ASS;
  as opções e os ORS continuam imutáveis.
- Persistência do launcher continua recuperando JSON mesmo com caminho
  completo em `language.txt`; seleção espanhola salva sem JSON gera erro,
  não entrada silenciosa em inglês. JSON via variável de ambiente também
  fica sujeito ao gate de runtime.
- Inglês original sem JSON continua funcionando normalmente. L×H no modo
  inglês ainda tem 273 falas adicionais cuja fonte é espanhol; o trabalho
  de traduzir essas falas para inglês é separado e continua pendente.

## QA L4 realizado

1. Build Release `SchoolDays.exe`/`SchoolDaysLauncherTest`/
   `SchoolDaysTranslationCoverageTest`: PASS.
2. CTest dirigido **7/7 PASS**, incluindo export/lookup JSON, falas,
   escolhas, launcher, DLC, fades e SaveFile/Handoff sintético.
3. Smoke real usando `SchoolDays.exe` L4 + Script.GPK EN + JSON V1:
   inglês OFF/ON iniciou, espanhol OFF/ON recusado com 1126 falhas.
4. Smoke V3 draft: `--lxh off` audita **30968 entradas**, encontra
   **30740 presentes, 228 ausentes, 5299 identidades** e recusa ES;
   `--lxh on` audita **31241 entradas**, encontra **31013 presentes,
   228 ausentes, 5572 identidades** e recusa ES.
5. Todos os testes usam SDL dummy e `--documents` isolado; o executável
   comercial original e seus instaladores não foram executados.

## Arquivos e retorno

- L4 JSON privado atual (draft bloqueado):
  `<Release>/translations/es.json`, SHA256
  `92c16c903f6b57bf55c4a3cd6dd02a744cc8d0a2a516a3db731aa359efcb6671`.
- Cópia do V1 anterior (também privada):
  `D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-json-es-20261106/pre-V3-installed-es-ES-merged-safe.json`.
- Auditoria/relatório completo (sem publicar textos privados):
  `D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-json-es-20261106/v2-deterministic/FINAL_GATE_REPORT.md`.

**Conclusão:** nenhum idioma alternativo incompleto será apresentado como
“traduzido” com fallback para inglês. A versão ES V3 continua BLOQUEADA
enquanto faltar tradução/revisão. Resolver conflito de chaves que aparecem
com mais de um sentido no mesmo NODE pode exigir extensão contextual
game-specific do lookup, além de correção/revisão dos valores.

Sem commit/push, alterações Android ou substituição de GPK/saves.
