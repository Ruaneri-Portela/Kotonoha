# G2.4.1c — filtragem linear dos overlays ABC sem borda preta

Baseline: `d65a3b0`, branch `research/ktrf-binary-v0.1-20261005`. **Sem commit** até o smoke visual final. G2.4.1 ownership foi validado manualmente. G2.4.1b confirmou o bleeding causado por LINEAR e RGB preto em texels transparentes, mas o usuário rejeitou NEAREST por serrilhar os recortes pequenos.

## Correção

`Image::DrawAbcForBase` agora carrega o PNG ABC num `SDL_Surface` temporário RGBA, prepara **apenas** os bytes RGB de texels com alpha zero, cria a textura e faz upload a partir dessa superfície. A superfície é descartada depois. O PNG original não é escrito. A textura ABC volta explicitamente para `SDL_SCALEMODE_LINEAR` e mantém `SDL_BLENDMODE_BLEND` (straight alpha). Nenhuma base ou textura não-ABC muda de filtro.

`PrepareStraightAlphaForLinearFiltering` é um helper puro e determinístico. Para cada texel com alpha zero, procura apenas os oito vizinhos imediatos cujo alpha é maior que zero. Copia o RGB do vizinho mais próximo; em empate, escolhe maior alpha e depois a primeira posição na ordem fixa da varredura. O próprio alpha não muda. Texels com alpha maior que zero permanecem RGBA byte-identical. Texels transparentes a mais de um pixel da arte permanecem intocados. A propagação não faz cascata porque só texels **originalmente visíveis** podem servir como fonte.

Um pixel de margem basta para as nove variantes reais testadas. Não há máscara, threshold, blur, crop, mudança de escala/posição, premultiplicação nem edição no disco. Ownership/binding e PCM/scheduler não foram alterados.

## Comparação controlada

O teste usa renderer SDL software em 1280×720, PNG/base de 800×452, `src=nullptr`, `dst=nullptr` e blend straight alpha. A referência CPU compõe base+overlay em 1:1 pela fórmula correta e amplia a composição. São comparados: PNG original+LINEAR, PNG original+NEAREST, staging processado+LINEAR e referência CPU. Em 1:1, a borda dos dois recortes pequenos tem erro RGB zero contra a referência.

| Overlay A real | Texels RGB ocultos preparados | Erro médio de borda LINEAR original | NEAREST | LINEAR preparado | Pixels escuros original → preparado |
| --- | ---: | ---: | ---: | ---: | ---: |
| `00-00-A01-004MAK` | 166 | 4,288 | 0,868 | **0,166** | 353 → 6 |
| `00-00-A02-001BMAK` | 186 | 8,426 | 0,054 | **0,000** | 504 → 0 |
| `04-SB-E00-020X03` (grande; 131.645 pixels visíveis) | 1.600 | 7,142 | 0,350 | **0,060** | 4.993 → 11 |

Os estados B/C dos três grupos também passaram. No ponto de referência de A02 `(627,288)`, `original LINEAR = (180,168,152)`, `NEAREST = (230,214,194)`, `processed LINEAR = (230,214,194)` e `CPU expected = (230,214,194)`. Em A01 `(868,319)`, esses valores são respectivamente `(62,59,61)`, `(80,76,79)`, `(79,75,78)` e `(80,76,79)`. A diferença de um nível em A01 é arredondamento de amostragem, não a antiga linha escura.

O filtro continua efetivamente suave: no estado A, a saída LINEAR preparada difere do controle NEAREST em 981 pixels do recorte A01, 159 de A02 e 41.966 do grupo grande. A textura gerada pelo **caminho real** reporta `SDL_SCALEMODE_LINEAR`, formato `SDL_PIXELFORMAT_ABGR8888` nesta máquina, blend straight alpha e modulações RGB/alpha em 255. Sua saída coincide com o controle LINEAR preparado no teste. A comparação visual local dos BMPs mostrou o retângulo preto no controle original+LINEAR do grupo grande e sua ausência no controle preparado+LINEAR.

O teste verifica em todos os pixels dos nove PNGs: alpha preservado, RGBA visível byte-identical e mudança de RGB oculto limitada à vizinhança de 1 pixel. Um fixture sintético de 5×5 verifica os oito texels preenchidos, ausência de expansão adicional e determinismo.

## Artefatos e validação

`SchoolDaysAbcEdgeSeamTest` salva BMPs locais em `build-win/`, incluindo `A02_linear_original.bmp`, `A02_nearest.bmp`, `A02_linear_dilated.bmp` e equivalentes para A01 e o grupo grande. Eles são diagnósticos e não entram no Git. `SchoolDaysAbcOverlayHotfixTest` continua cobrindo prepare invisível, geração de base, binding, troca de base e reset.

`cmake --build build-win --config Release --parallel 6`: **PASS**. `ctest --test-dir build-win -C Release --output-on-failure`: **14/14 PASS**, inclusive `SchoolDaysAbcOverlayHotfixTest`, `SchoolDaysAbcEdgeSeamTest`, PCM/ABC e regressões do runtime. O smoke perceptual no backend gráfico real ainda é necessário antes de qualquer commit.
