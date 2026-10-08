# G2.4.1 — ABC overlay ownership and transparency

Baseline: `d65a3b0` (`research/ktrf-binary-v0.1-20261005`). Esta alteração permanece **sem commit** até o smoke visual manual.

## Diagnóstico

`Image::Register` descobria os grupos A/B/C no PREPARE. `DrawAbcForBase` desenhava o estado A de **todos** os grupos cujo path era igual ao da base desenhada, mesmo sem binding PlayVoice. O path não distinguia duas instâncias de CreateBG para o mesmo PNG. Por isso, grupos futuros, sem voz, ou pertencentes a outra instância podiam aparecer como recortes órfãos.

Não existe desenho explícito de bounding box no caminho ABC. Os PNGs reais `00-00-A01-004MAK.A.PNG` e `00-00-A02-001BMAK.A.PNG` têm alpha 0 nos quatro cantos, mas uma área facial central retangular e opaca. No A01, essa área é `(543,182)..(585,219)`; seus pixels de borda coincidem com a composição `00-00-A01-004.PNG`. O retângulo torna-se perceptível quando esse recorte é exibido sem o vínculo correto. A base semitransparente `00-00-A02-001B.PNG` também era renderizada em modo premultiplied, embora o decoder entregue RGBA direto; isso podia produzir artefatos adicionais de composição.

## Correção

Cada registro de CreateBG agora recebe uma geração numérica própria, inclusive quando duas instâncias usam o mesmo path. O grupo A/B/C conserva a geração da base que o criou. O renderizador exige simultaneamente base ativa, geração idêntica, binding explícito de voz e janelas válidas de base/voz. PREPARE pode descobrir recursos, mas não autoriza pixels A/B/C. A troca de base desativa todos os bindings anteriores sob o mesmo mutex usado pelo renderer; vozes já ativadas e ainda dentro de sua janela podem se vincular aos grupos da nova base. Isso cobre A02, onde a voz `00-00-A02-0070` começa no tick 40, a base `001B` começa no tick 55 e a voz termina no tick 77. No END da base, a camada de imagem é limpa com alpha 0 antes de reter a base, removendo pixels ABC antigos da textura intermediária. Reset limpa base, grupos e texturas. O evento mantém a geração no binding e descarta o binding anterior quando a base muda ou a voz acaba.

As texturas PNG são RGBA direto (straight alpha). A composição-base passou a usar `SDL_BLENDMODE_BLEND`; o overlay já usava esse modo. Nenhuma posição, escala ou regra PCM/24 Hz foi modificada. FadeRuntime, PresentedFrame e SchoolDaysSceneTransaction não foram alterados.

## Evidência automática

`SchoolDaysAbcOverlayHotfixTest` usa SDL software e os PNGs distribuídos de A01/A02, sem copiá-los ao Git. Confere: PREPARE invisível; grupo sem voz invisível; MAK e TAI independentes; pixels com alpha 0 não modificam a base; boca opaca modifica pixels; nova base preparada invisível; troca A01→A02 remove imediatamente os bindings antigos; END limpa o recorte antigo; mesma base registrada duas vezes mantém identidades distintas; reset descarta ownership. Os testes G2.4/G2.6 foram ajustados para a API por geração.

Build Release completo e `ctest --test-dir build-win -C Release --output-on-failure`: **13/13 passaram**. O teste de software valida a composição; a validação perceptual no app real ainda é obrigatória antes do commit.

## Resultado do primeiro smoke manual

O ownership foi validado: nenhum overlay aparece antes ou depois de sua base. Uma borda preta ainda ficou visível nos limites dos recortes A/B/C. A investigação e a correção de amostragem desta borda estão em [G2_4_1B_ABC_EDGE_SEAM.md](G2_4_1B_ABC_EDGE_SEAM.md); ambas permanecem sem commit até o próximo smoke visual.
