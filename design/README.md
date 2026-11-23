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

O icone usa uma paleta de fundo clara e propria, documentada em
`tokens.json` sob `icon`: `backgroundStart` #C4DEFF (ice/azul claro),
`backgroundEnd` #CDBCFC (lilac), `glow` #FFFFFF, `wave` #EC6AA5,
`monogram` #FFFFFF, `monogramContour` #4A3496 e `sakura` #FF8FBD.
Os tokens Navy/Raised/Indigo continuam validos para superficies de UI;
nao sao mais usados como fundo do icone.

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

- Generico Windows/Linux/WebApp: K branco com contorno indigo sobre fundo
  claro ice/lilac, sakuras discretas; icone PNG, ICO para WinRes e PWA por
  app. No WebApp, apenas o brilho central do fundo varia por jogo (o acento
  e fortemente clareado), mantendo as proporcoes.
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

Esta pasta `design/` e a fonte da verdade da identidade visual. O gerador
escreve diretamente em `design/generated/` (versionado, visivel no repositorio),
nao em um diretorio de build efemero. CMake, Xcode, Android Gradle, WinRes e
o shell web importam apenas os arquivos especificos de cada plataforma; a pasta
`design/` **nunca** e empacotada como asset generico do motor.

Gerar por: python3 tools/generate_brand_assets.py --kind all

Para o icone do macOS (Icon Composer -> export .icon), gerar o PNG full-bleed:

    python3 tools/generate_brand_assets.py --kind apple
    # -> design/generated/apple/AppIcon-1024.png (abrir no Icon Composer -> export .icon)

Use `--out <pasta>` apenas para testes isolados ou exports avulsos.

O comando `--kind desktop` tambem emite esse mesmo PNG 1024x1024, entao o
bundle de desktop ja sai pronto para o Icon Composer. O macOS usa o tratamento
`generic` full-bleed: o fundo preenche o canvas inteiro e os quatro cantos tem
alpha=255. Nao existem cantos transparentes — foi exatamente essa mascara que
causava a borda branca ao redor do icone no macOS. O monograma K preserva as
coordenadas invariaveis da marca; somente a mascara de cantos foi removida.

### Fundo claro (v1.1)

O quadrado escuro navy foi removido. Antes, os cantos do icone `generic`
mediam #2C2C6D (44,44,109) e a regiao livre perto do K #3F3578 (63,53,120).
Agora o fundo e um degrade claro ice -> lilac: cantos em torno de #C9C7FD
(201,199,253) e regiao livre perto do K em #C7C8FC (199,200,252), com a onda
sakura clara resultante em #CFB9E4. O brilho
central mantem uma leve tintura do acento de cada jogo, preservando a
variacao das PWA icons. Como o K continua branco, um contorno indigo suave
(#4A3496) envolve os tracos para garantir contraste sobre o fundo claro. As
flores de sakura e a geometria do monograma nao foram alteradas.

Os binarios produzidos na pasta design/generated sao derivados e nao devem
ser editados manualmente. CMake, Xcode, Android Gradle e Emscripten preparam
os assets para os seus bundles, sem sobrescrever os arquivos antigos do Git.

O arquivo `tokens.json` desta pasta e a versao estruturada desta tabela;
tools/generate_pwa_icons.py original define a geometria de referencia do K.

## Observacoes sobre PWA e build

O WebAssembly usa uma copia temporaria de assets do motor para carregar o novo
splash (o original de cada jogo, quando existir, continua independente). O
launcher HTML apresenta o icone da PWA centralizado sobre o fundo preto.
CMake embute o icon.ico na secao WinRes de cada executavel Windows. O recurso
icon.icns fica na raiz Resources do bundle macOS; no iOS o Info.plist
compartilhado enumera os PNGs gerados pelos scripts de empacotamento.

## Referência definitiva: guia de interface visual v2.0

A arte aprovada nesta conversa é a referência obrigatória para identidade-base,
componentes e adaptação por plataforma. Os tokens oficiais estão em
`design/tokens.json` (versão 2) e não devem ser substituídos por
paletas genéricas. A página web preserva o canvas e exibe a seleção de assets
com botões violeta/índigo, foco ciano, abas de idioma e barra de progresso
em degradê; o splash deve continuar preto e centralizado. O ImGui aplica a
hierarquia Navy → Raised → Indigo → Violet/Magenta a janelas, botões, abas,
campos, sliders e popups. Android mantém Material 3, com cores semânticas e
cards contornados. iOS/macOS mantêm SwiftUI e Liquid Glass nativo, usando
bordas magenta e luzes ciano/rosa discretas. Ornamentos de sakura são de
baixo contraste e nunca aparecem sobre o jogo em execução.

Estados de confirmação, erro, aviso, seleção, hover e teclado são obrigatórios.
A renderização nativa pode divergir em detalhes de superfície, mas não na
paleta, hierarquia ou identidade do monograma K.

### Variante ImGui "Soft" (somente launcher / debug overlay)

O tema Dear ImGui usa uma derivação dessaturada e mais clara da paleta oficial,
aplicada em `lib/src/debug/ImGuiTheme.cpp` (constantes `constexpr`) e na faixa
de cabeçalho do launcher. Os tokens de `design/tokens.json`, o monograma K, as
flores de sakura e os assets de ícone **não** foram alterados; web, Android,
iOS e macOS continuam usando a paleta v2 original.

| Papel | Antes | Depois |
| --- | --- | --- |
| Fundo da janela (Navy) | #0D142E | #1B2740 |
| Superfície elevada (Raised) | #182449 | #273451 |
| Cabeçalho de tabela / seleção (Indigo) | #2C3E9A | #525D84 |
| Acento / título (Violet) | #6A5BE7 | #A79BD4 |
| Hover (Magenta) | #C86BD9 | #C3B4E0 |
| Borda de tabela (Cyan) | #00D6FF | #8FB8D9 |
| Borda / divisor (Outline) | #39315E | #3D4763 |
| Texto (White) | #FFFFFF | #DCE3F0 |
| Texto secundário (Muted) | #B9B5D4 | #9DA9C2 |
| Botão primário | #6A5BE7 | #525D84 |
| Sucesso / aviso / erro | #47D7A1 / #FFBE68 / #FF7389 | #7FC8A8 / #E8C89A / #E0A3AC |

A seleção de texto usa lilás com alpha 0,25 (antes 0,35) e as linhas
alternadas da tabela passam a slate com alpha 0,16. A hierarquia
fundo → superfície → acento é preservada e o contraste do texto se mantém
acima de 4,5:1 em texto normal (e acima de 3:1 no título grande), conforme
WCAG AA. Raios e espaçamentos foram aumentados de forma leve para uma leitura
mais calma.
