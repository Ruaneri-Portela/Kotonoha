# G2.4 — A/B/C conduzido pelo PCM de PlayVoice

O runtime de cena agora associa a **chave textual genérica** de `PlayVoice` ao grupo gráfico do `CreateBG` ativo. Para uma base `stem.PNG`, a descoberta procura `stem<key>.A.PNG`, `.B.PNG` e `.C.PNG` no mesmo diretório. As chaves são comparadas sem distinguir maiúsculas/minúsculas; não há lista de personagens nem fallback para trio sem chave. Grupos incompletos são registrados, mas a seleção de um estado inexistente conserva o estado anterior. Os triplets são overlays transparentes, desenhados na dimensão nativa sobre a composição-base. Cada textura é criada no primeiro uso e reutilizada. O antigo toggle de `Image::Render` a cada 750 ms foi removido.

## Proveniência

Base dinâmica: G1.2–G1.4.1 em `D:/SCHOOL DAYS HQ/Overflow/SCHOOLDAYS HQ/Research_engine/13_COMPLETE_RUNTIME_RESEARCH/asset_trace/reports/`. O produtor original é `SCHOOLDAYS HQ.exe` VA `0x0041BA90` (RVA `0x1BA90`); o consumer é `0x0041A830` (RVA `0x1A830`). O scheduler gráfico é `0x00444CF0` (RVA `0x44CF0`) e o seletor `0x00444B80` (RVA `0x44B80`). O áudio original de MAK/TAI foi observado em PCM mono s16 a 44.100 Hz. Esta implementação não altera o decoder float usado para reprodução: `SchoolDaysVoicePcm.cpp` decodifica uma cópia de análise pelo FFmpeg/SWR na tarefa de preparação do evento. A callback do mixer não acessa Image/SDL render. O runtime de evento consome a sequência produzida sob seu mutex; o render usa o mutex da Image.

## Geometria da atividade

A cópia normalizada é particionada em blocos determinísticos de **44.100 bytes** e um bloco final parcial. Para cada bloco, `B` é seu tamanho em bytes, `P` os bytes dos blocos anteriores e `n` o contador global de registros. O limite é `float((P+B)/44100.0 * 11.5)` e gera registros enquanto `n <= limite`. O índice local é `floor(B*n/23)-floor(P/2)`. O resultado vale 1 quando a amostra s16 é `>60` ou `<=-60`; caso contrário, 0. A comparação original da guarda é `index < B` **em bytes**, preservada no helper; a implementação acrescenta um limite de leitura ao bloco realmente alocado para não ler memória fora do buffer. Não há evidência para reproduzir eventual overread do binário.

O timestamp original de um registro é `round(n*10.000.000/24)` unidades de 100 ns. O port guarda `SceneTick n` relativo à mídia e consulta o último registro válido para o tick relativo da voz. Isso evita conversões acumulativas. A última amostra não é mantida depois do fim da sequência de registros.

O `graphicTick` é o tick da cena a 24 Hz. Enquanto a atividade é 1, `graphicTick % 3 == 0` incrementa o contador do binding e `counter % 3` seleciona A/B/C. Com atividade 0, o grupo repousa em A sem incrementar o contador. Cada chave/grupo conserva contador próprio; nova voz com a mesma chave substitui o binding anterior de modo determinístico. Não misturamos o PCM de eventos de voz distintos.

`Reset` limpa bindings, registros, decoders e grupos/texturas. No rebuild da timeline de G2.2, os eventos ainda ativos são preparados de novo, os registros são reconstruídos a partir do OGG e o estado gráfico é calculado desde o START da voz até o tick alvo. `ComputeSchoolDaysAbcStateAt` oferece a mesma reconstrução como função pura. O estado semântico só usa caminhos/chaves, ticks, contador, índice e registros; não depende de ponteiros GPU serializáveis.

## Validação

`SchoolDaysAbcRuntimeTest` cobre os limiares `0`, `+60`, `+61`, `-60`, `-59`; índices negativos/fora da guarda; bloco cheio e parcial; independência da segmentação de AVFrame; relógio de 100 ns; A→B→C→A, repouso em A e reconstrução após seek; chaves independentes; chave `x01` e grupo incompleto; chave vazia sem fallback. O teste read-only com os assets reais encontrou os grupos MAK e TAI de `Event00/00-00/00-00-A01/00-00-A01-004` e confirmou overlay MAK de **800×452**. Em `05-KC-A02-009`, `kot` seleciona `009KOT`, nunca `009.A/B/C`.

O decoder de análise produziu **59.943 amostras MAK** e **78.693 TAI**, exatamente os tamanhos PCM observados em G1.4.1. Os **32 valores MAK** e **42 TAI** calculados coincidem bit a bit com a sessão corrigida G1.4 `g1_2_20261007T020213Z.jsonl`. Um renderizador SDL de software, usando os PNGs reais de A01, confirmou hashes de pixels distintos para MAK A, B e C. A sessão G1.4.1 registrou uma sequência MAK diferente em outra execução; o motivo da variação ainda não foi fechado. Nenhum asset privado foi adicionado ao Git.

Build desktop Release e testes de regressão G2.1–G2.3B foram executados. Não houve smoke visual interativo nesta rodada.

## Limites preservados

- Significado nominal de `x01`/`x03`: desconhecido; são apenas chaves genéricas.
- Uso em runtime do trio `05-KC-A02-009.A/B/C` sem chave: não comprovado; sem fallback.
- Política original exata para duas vozes simultâneas da mesma chave: não fechada; o port usa a última ativada.
- Leitura do original quando o índice satisfaz `index < B` mas excede as amostras contidas no bloco: desconhecida; o port retorna atividade 0 por segurança.
- Pequena variação entre os registros MAK das duas sessões originais: ainda não explicada. A sessão corrigida G1.4 coincide com o decoder desta implementação.
