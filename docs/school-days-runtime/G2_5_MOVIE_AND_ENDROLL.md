# G2.5 — PlayMovie e EndRoll

O audit read-only dos 1.857 ORS e dos WMVs extraídos está em `G2_5_MOVIE_ASSET_AUDIT.json`; o gerador reproduzível é `tests/audit_school_days_movies.py`. Há 2.041 `PlayMovie` tabulados válidos e 67 `EndRoll`. As 2.108 referências resolvem para arquivos locais; todas diferem na grafia de maiúsculas/minúsculas (sobretudo `.wmv` versus `.WMV`), sem colisão case-insensitive. A linha `PlayMovie` separada por vírgulas de `05-KI-OP1` permanece inválida no reader UTF16_TAB e não é contabilizada como evento. Nenhum WMV foi incluído no Git.

## Contrato implementado

`SchoolDaysMovieEventState` preserva o tipo (`PlayMovie` ou `EndRoll`), `PATH.wmv`, START/END como `SceneTick` de 24 Hz, NUMERIC, e estados de preparo, ativação, EOF e conclusão. O estado lógico não contém `AVFormatContext` nem textura. A pesquisa G1.9 observou ambos os comandos usando o MovieEvent original (EXE RVA `0x48BC0`) e o mesmo loader WMV (RVA `0x49D40`). O branch `PlayMovie` passa NUMERIC ao método virtual `+0x7C`; `EndRoll` passa zero. O corpus usa exclusivamente NUMERIC=0. O port preserva valores não zero, registra unsupported e não lhes atribui função nova. A guarda específica de `EndRoll` no EXE permanece desconhecida.

O caminho de ambos agora é: ORS → estado lógico → resolvedor por componentes, sem distinguir caixa → `Video::Prepare` → despacho da timeline → `Video::Activate` → renderização/EOF → END → `Video::Remove`. A decodificação antecipada pode abrir o WMV e obter seu primeiro frame, mas o item fica invisível até START. START e END são convertidos por `SchoolDaysSceneTime.h` a partir dos ticks absolutos da cena, sem acumular milissegundos arredondados. O PTS do WMV permanece no domínio do decoder FFmpeg. O antigo cálculo `1 + floor(scene_ms*24/1000) >= endTick+1` usado apenas por `PlayMovie` equivalia a `currentTick >= endTick` na janela normal; ele foi substituído por essa comparação explícita, comum aos dois tipos. Isso não afirma uma política de “penúltimo frame”.

EOF físico marca o decoder como concluído e mantém a última textura já apresentada até END; não repete a mídia nem inicia roteamento. Foi corrigida uma falha do decoder: após o EOF do demux, um AVFrame vazio permanecia alocado e impedia a drenagem até `AVERROR_EOF`. END encerra o MovieEvent e libera decoder/textura. `EndRoll` usa o mesmo backend, sem gerador de créditos separado. O fim ou EOF de `EndRoll` não termina a Current Scene: somente `Next` alimenta o `CheckEnd` e a `SchoolDaysSceneTransaction` existente. `FadeRuntime`, `PresentedFrame` e `SchoolDaysSceneTransaction` não foram alterados.

`Event::Reset` descarta todos os decoders e estados de vídeo. O `RebuildActiveAt` de G2.2 volta a preparar e ativar apenas eventos cuja janela contém o tick alvo. Seek para trás reconstrói um decoder novo; seek após END não ressuscita o evento. O backend faz seek por PTS ao abrir dentro de uma janela, mas precisão frame-exact para todos os WMVs não foi demonstrada. Se o arquivo já acabou nesse ponto, a preparação pode falhar de modo seguro enquanto a timeline prossegue.

## Validação

`SchoolDaysMovieEventTest` cobre resolução de PATH, diferenças de caixa, preparo sem apresentação, NUMERIC=0 e não zero, EOF independente de `Next`, janelas/seek/reset, salto de vários ticks, `EndRoll.end=200` com `Next=547` e a janela de `05-SB-E01` (`3336..6668`). Quando o executável `ffmpeg` está instalado, o teste gera um WMV sintético no diretório temporário, decodifica em renderer SDL de software, observa o EOF e valida a retenção do último frame até END. O teste também abre, em modo read-only, `Movie00/00-00/00-00-A00/00-00-A00-000.WMV` quando os assets locais estão disponíveis; a resolução observada foi 800×452. `ffprobe` mostrou PTS iniciais monotônicos `0,041`, `0,083`, `0,125`, `0,166` segundos para esse WMV. Os testes não exigem janela gráfica física.

## Limites preservados

- Significado de NUMERIC não zero: desconhecido e ausente do corpus tabulado alvo.
- Significado externo da guarda do branch original `EndRoll`: desconhecido.
- Destrutor individual original de `EndRoll`: não interceptado.
- Seek frame-exact em todo WMV: não comprovado; o backend usa seek por PTS.
- Parâmetros gráficos isolados do MovieEvent original: não identificados.
- A política original de apresentação exata no EOF terminal não é inferida do antigo cálculo de `+1`; este gate mantém a última textura decodificada no backend atual até END.
