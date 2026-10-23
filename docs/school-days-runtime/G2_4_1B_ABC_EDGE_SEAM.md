# G2.4.1b — borda dos overlays ABC

Baseline: `d65a3b0`, branch `research/ktrf-binary-v0.1-20261005`. Sem commit: a validação visual no jogo ainda cabe ao usuário. O smoke anterior confirmou o ownership de G2.4.1, mas encontrou uma borda preta nos sprites A/B/C.

**Atualização G2.4.1c:** o smoke manual confirmou que NEAREST removeu a borda, mas rejeitou sua pixelização. NEAREST foi um controle diagnóstico, não a solução visual final. A correção posterior com LINEAR e RGB de borda no buffer temporário está em [G2_4_1C_ABC_LINEAR_FILTERING.md](G2_4_1C_ABC_LINEAR_FILTERING.md).

## Causa

**DEBUG_RECT_FOUND = no.** A busca por `SDL_RenderRect`, `SDL_RenderDrawRect`, `SDL_RenderDebug`, `RenderRect`, `DrawRect`, `rectangle`, `bounds`, `bounding`, `outline` e `debug box` não encontrou desenho de contorno no caminho de imagem/ABC. O caminho usa `SDL_RenderTexture` com `src=nullptr` e `dst=nullptr`, ou seja, o PNG inteiro ocupa o canvas. No teste desktop, PNG = 800×452, canvas = 1280×720 e destino = `(0,0,1280,720)`; não há coordenadas fracionárias nem `width±1`/`height±1` nesse draw. A ampliação de 1,6× é a escala existente do canvas, que este hotfix não modifica.

Os PNGs originais foram examinados somente em leitura. As variantes A/B/C de cada um dos dois stems abaixo têm a mesma geometria alfa:

| Overlay | Alpha=0 | 0<alpha<255 | Alpha=255 | Bounding box dos pixels opacos | Primeiro pixel opaco |
| --- | ---: | ---: | ---: | --- | --- |
| `00-00-A01-004MAK.A.PNG` | 359.966 | 0 | 1.634 | `(543,182)..(585,219)` | `(129,136,129,255)` |
| `00-00-A02-001BMAK.A.PNG` | 359.536 | 0 | 2.064 | `(392,160)..(439,202)` | `(230,214,194,255)` |

Em ambos, alpha varia de 0 a 255, **todos** os pixels totalmente transparentes têm RGB `(0,0,0)`, e os quatro cantos são `(0,0,0,0)`. O primeiro pixel opaco coincide com o pixel correspondente da base. Assim, o asset não contém por si só uma linha preta na região exterior.

O decoder converte o PNG para RGBA direto (`SDL_PIXELFORMAT_RGBA32`; no Windows little-endian, a textura reporta `SDL_PIXELFORMAT_ABGR8888`). O ABC usa `SDL_BLENDMODE_BLEND`, que aplica straight alpha; modulações observadas na textura real são RGB `(255,255,255)` e alpha `255`. Porém, `SDL_GetTextureScaleMode` confirmou que a textura criada tinha o default `SDL_SCALEMODE_LINEAR` (`1`). Ao ampliar o overlay, a filtragem linear mistura RGB preto de um texel transparente com RGB do texel opaco vizinho e também interpola o alfa. O blend straight alpha multiplica novamente esse RGB já escurecido pelo alfa interpolado. Isso produz a faixa escura na transição retangular.

## Prova controlada

`SchoolDaysAbcEdgeSeamTest` usa os PNGs distribuídos de A01/A02, renderer SDL software e três referências, mantendo base, geometria e blend idênticos:

1. CPU: compõe base+ABC em 800×452 pela fórmula straight-alpha `outA=srcA+dstA*(1-srcA)` e `outRGB=(srcRGB*srcA+dstRGB*dstA*(1-srcA))/outA`, depois amplia a composição.
2. SDL LINEAR: amplia base e ABC separadamente, com LINEAR no ABC.
3. SDL NEAREST: igual, mudando **somente** o scale mode do ABC.

Em 1:1, integer-aligned, NEAREST + straight alpha, o erro médio RGB na borda contra a referência CPU é **zero** para os estados A dos dois stems. Ao voltar para 1280×720, LINEAR reintroduz a faixa escura; NEAREST a elimina nos pixels externos medidos. Exemplos RGB `CPU / LINEAR / NEAREST`:

| Cena, pixel de borda | CPU | LINEAR | NEAREST |
| --- | --- | --- | --- |
| A01 `(868,319)` | `80,76,79` | `62,59,61` | `80,76,79` |
| A02 `(627,288)` | `230,214,194` | `180,168,152` | `230,214,194` |

Os pixels imediatamente fora das bordas (`A01 (867,319)`, `A02 (625,288)`) permaneceram iguais à referência. Os primeiros pixels internos (`A01 (870,319)`, `A02 (628,288)`) conservaram a arte; não houve remoção da boca. Nas faixas de borda comparadas à referência CPU, a contagem de pixels escurecidos mais de 5 níveis caiu de **353 para 40** em cada variante A/B/C de A01, e de **504 para 0** nas variantes A/B/C de A02. Os 40 resíduos de A01 são pequenas diferenças de amostragem sobre pixels opacos após escala, não uma linha contínua de RGB preto no contorno; o smoke visual confirmará o resultado perceptual.

## Correção

Somente as texturas A/B/C recebem `SDL_SCALEMODE_NEAREST` ao serem criadas em `Image::DrawAbcForBase`; a base, o canvas e os outros recursos mantêm seus filtros e dimensões. `SDL_BLENDMODE_BLEND` e as modulações permanecem. Não há dilation, máscara, crop, mudança de posição ou edição de asset. O teste consulta o scale/blend/modulation da **textura criada pelo caminho real** e compara sua saída ao controle NEAREST.

Os BMPs de diagnóstico `g2_4_1b_*_{cpu,linear,nearest,actual}.bmp` e `g2_4_1b_*_native_1to1.bmp` são gerados somente sob `build-win/`, fora do Git. Os testes de ownership, base generation/binding e PCM permanecem ativos na suíte.

## Validação

`cmake --build build-win --config Release --parallel 6` e `ctest --test-dir build-win -C Release --output-on-failure`: **14/14 passaram**. Falta a validação perceptual manual em A01/A02 no backend gráfico real antes de qualquer commit. O teste SDL software demonstra a causa e o resultado de composição nas condições medidas, sem atribuir ao jogo original uma política de filtro não pesquisada.
