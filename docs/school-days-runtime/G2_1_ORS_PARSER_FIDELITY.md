# G2.1 — fidelidade estrutural do parser ORS

Escopo: corpus distribuído de School Days HQ v1.02, com `UnicodeFile != 0` e `UseEnglish != 0` (reader UTF-16/TAB no executável original). Os arquivos de `assets/00` a `assets/05` usados no audit são byte a byte iguais aos 1.857 ORS do inventário G1.10: 0 divergências SHA-256. Evidência de origem: `Research_engine/13_COMPLETE_RUNTIME_RESEARCH/parser_edge_cases/ORS_GRAMMAR_V1.md` e `reports/G1_10_1_RESULT.md` na pesquisa local do jogo.

## Alterações

- `src/parsers/Ors.c`: exige a aridade do comando antes de criar o evento e usa apenas TAB como separador. As duas linhas de `05-KI-OP1` com vírgulas e o `PrintText` de cinco campos em `03-KB-D10` não criam eventos. O `;` final termina a linha; um `;` dentro do texto não a trunca.
- `src/parsers/Ors.c`: remove o ajuste condicional de `+1` em `START == previous.END`. A inserção ordenada já usa comparação estrita `>` e, portanto, mantém a ordem textual entre eventos com mesmo `START`.
- `include/Kotonoha/parsers/Ors.h` e `src/parsers/Ors.c`: reconhecem `MoveSom` com `START`, valor numérico e `END`. O modelo o armazena e libera; o runtime existente cai no `default` neutro. Nenhum SOMCON foi implementado.
- `src/parsers/Ors.c`: os campos vazios de `PlayVoice` continuam ocupando suas posições; NUMERIC vazio vira `0` e KEY vazia permanece `""`. `SetSELECT` trata `null` e `NULL` como opções ausentes (além da opção vazia já tratada), sem tornar `null` um token global. `CreateBG.FIELD0` continua armazenado sem nova semântica.
- `SkipFRAME` e `Next` continuam tipos separados. Seus `START` são preservados individualmente no modelo; a lógica de skip/scene no runtime não faz parte deste gate.

## Validação

O teste independente `tests/OrsParserFidelityTest.cpp` cobre evento padrão, ordem textual no mesmo tick, igualdade `END == START`, voz normal e vazia, `SetSELECT null/NULL`, `MoveSom`, as três linhas malformadas, `Next` versus `SkipFRAME` e `;` literal em fala. Pode ser habilitado com `-DKOTONOHA_ORS_PARSER_TESTS=ON` ou junto da suíte existente `-DKOTONOHA_ROUTER_TESTS=ON`.

Comandos executados no Windows:

```powershell
cmake --build build-win --target Kotonoha OrsParserFidelityTest --config Debug
ctest --test-dir build-win -C Debug -R '^OrsParserFidelityTest$' --output-on-failure
& 'build-win\Debug\OrsParserFidelityTest.exe' --audit 'assets'
```

Resultado: build e teste **PASS**. Audit real: 1.857 arquivos; 86.539 linhas de evento; 86.536 eventos aceitos (86.387 padrão + 149 `PlayVoice` com vazios); 3 descartados (2 em `05-KI-OP1`, 1 em `03-KB-D10`); 281 `MoveSom`; 0 headers `UNKNOWN`. O relatório JSON da execução está em `build-win/G2_1_CORPUS_AUDIT.json` e é temporário, fora do commit.

Durante o audit, apareceu inicialmente uma quarta rejeição em `05-KC-F00`: um `PrintText` válido contém `;` no texto. O terminador final foi corrigido e o caso entrou nos testes. Os resultados finais coincidem com o corpus G1; nenhuma linha adicional foi descartada.

## Limites deste gate

O parser ainda representa timestamps na unidade legada usada pelo Kotonoha; a conversão exata para o tick original de 24 fps e a base absoluta da cena são assuntos do runtime, não foram alterados aqui. Também permanecem fora de escopo a execução de `MoveSom`, comportamento de skip, mídia, Save/Load e a semântica de `Next` na cena.
