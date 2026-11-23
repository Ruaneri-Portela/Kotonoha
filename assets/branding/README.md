# Kotonoha Engine - Carta de Estilo v1.0

Este README e tools/generate_brand_assets.py sao a referencia permanente da identidade
visual Kotonoha. Alteracoes futuras devem preservar as coordenadas e a leitura
do K do antigo WebApp; a imagem de preview nao e um arquivo de build.

## Monograma invariavel

K branco, geometrico, de tres tracos com pontas arredondadas:
- Haste vertical (0.375, 0.310) ate (0.375, 0.690)
- Diagonal superior (0.615, 0.310) ate (0.385, 0.510)
- Diagonal inferior (0.395, 0.500) ate (0.635, 0.690)
- Raio de traco 0.041 em quadrado normalizado (1:1).

O K sempre ocupa a zona de leitura; flores de sakura aparecem somente
como detalhe secundario. A geometria deve ser a mesma no WebApp, Windows,
Linux, Android, iOS e macOS, mesmo quando o material de fundo muda.

## Tokens visuais oficiais

| Token | Hex | Uso |
| --- | --- | --- |
| SplashBlack | #000000 | fundo universal de inicializacao |
| Navy | #0D142E | fundo profundo |
| Raised | #182449 | cards e superficies elevadas |
| Indigo | #2C3E9A | superficies |
| Violet | #6A5BE7 | primario, foco e selecao |
| Magenta | #C86BD9 | acento, hover |
| Sakura | #FF8FBD | detalhes da marca |
| Cyan | #00D6FF | foco alternativo, brilho sutil |
| White | #FFFFFF | texto / marca |
| Muted | #B9B5D4 | texto secundario |
| Outline | #39315E | bordas e divisores |

## Componentes, estados e layout

Botoes, campos de busca, text inputs, radios, checks, toggles, sliders,
abas, menus, cards, listas, toolbars, modais, barras de progresso e tooltips
usam os mesmos tokens semanticos. Hierarquia clara: fundo navy, superficies
raised, primario violet, hover magenta, foco visivel cyan, texto branco.
Erro, alerta e sucesso possuem cores funcionais proprias, distintas da marca.
Espacamentos: 4, 8, 16, 24; raios: controles 8, cards 12, modais 20.
O estilo nunca deve sacrificar legibilidade e contrastes. Evitar animacoes
ininterruptas e respeitar configuracoes de reducao de movimento.

## Adaptacao por plataforma

- Generico Windows/Linux/WebApp: K branco + fundo indigo/violeta, sakuras
  discretas; icone PNG, ICO para WinRes e PWA por app. No WebApp, apenas o
  acento de fundo varia por jogo, mantendo as proporcoes.
- Android: widgets Material 3, foreground/background em adaptive icon,
  themed/monochrome icon, formas e navegacao nativas. Sem simular Liquid Glass.
- iOS e macOS: SwiftUI, tipografia e gestos nativos; Liquid Glass nos sistemas
  que suportam glassEffect e materiais nos anteriores. Vidro apenas em
  superficies apropriadas, nunca substituir widgets do sistema por imagens.
- ImGui: manter o fluxo de navegacao existente; tokenizar estados de botao,
  seletor, popup, barra, tab, card e janela. Interface Kotonoha gerenciada
  pelo motor usa os mesmos tokens no shell e debug overlay.
- Interfaces originais de jogos de terceiros nao sao substituidas.

## Splash universal

Fundo puro #000000, marca exatamente centralizada, preservacao da razao
de aspecto e sem paisagem, molduras de tela, bordas responsivas ou
assumir portrait/landscape. O renderer compoe preto no espaco sobrante.

## Reproducao, distribuicao e fonte da verdade

Gerar por: python3 tools/generate_brand_assets.py --kind all --out build/branding

Os binarios produzidos na pasta build/branding sao derivados e nao devem
ser editados manualmente. CMake, Xcode, Android Gradle e Emscripten preparam
os assets para os seus bundles, sem sobrescrever os arquivos antigos do Git.

O arquivo assets/branding/tokens.json e a versao estruturada desta tabela;
tools/generate_pwa_icons.py original define a geometria de referencia do K.
