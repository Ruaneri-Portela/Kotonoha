# Kotonoha Engine - Carta de Estilo e Identidade Visual v2.0

[English (US)](README.md) | **Português (Brasil)**

Este README e `tools/generate_brand_assets.py` são a referência permanente da identidade visual do Kotonoha. Alterações futuras devem preservar as coordenadas e a leitura do monograma K; imagens de pré-visualização não são entradas de build.

## Monograma K Invariável

K branco, geométrico, de três traços com pontas arredondadas:
- Haste vertical: `(0.375, 0.310)` até `(0.375, 0.690)`
- Diagonal superior: `(0.615, 0.310)` até `(0.385, 0.510)`
- Diagonal inferior: `(0.395, 0.500)` até `(0.635, 0.690)`
- Raio do traço: `0.041` em quadrado normalizado (`1:1`).

O K sempre ocupa a zona principal de leitura; flores de sakura aparecem somente como detalhe secundário. A geometria permanece idêntica no WebApp, Windows, Linux, Android, iOS e macOS, mesmo quando o material de fundo muda.

## Tokens Visuais Oficiais

| Token | Hex | Uso |
| --- | --- | --- |
| SplashBlack | `#000000` | Fundo universal de inicialização |
| Navy | `#0D142E` | Fundo profundo |
| Raised | `#182449` | Cards e superfícies elevadas |
| Indigo | `#2C3E9A` | Superfícies interativas |
| Violet | `#6A5BE7` | Primário, foco e seleção |
| Magenta | `#C86BD9` | Acento secundário e hover |
| Sakura | `#FF8FBD` | Detalhes ornamentais da marca |
| Cyan | `#00D6FF` | Foco alternativo e brilho sutil |
| White | `#FFFFFF` | Texto primário e monograma |
| Muted | `#B9B5D4` | Texto secundário |
| Outline | `#39315E` | Bordas e divisores |

Os ícones utilizam uma paleta de fundo clara própria, documentada em `design/tokens.json` sob `icon`: `backgroundStart` `#C4DEFF` (azul gelo), `backgroundEnd` `#CDBCFC` (lilás), `glow` `#FFFFFF`, `wave` `#EC6AA5`, `monogram` `#FFFFFF`, `monogramContour` `#4A3496` e `sakura` `#FF8FBD`. Os tokens `Navy`/`Raised`/`Indigo` continuam válidos para superfícies de interface e não são usados como fundo de ícone.

## Componentes, Estados e Layout

Botões, campos de busca, entradas de texto, rádios, checkboxes, toggles, sliders, abas, menus, cards, listas, barras de ferramentas, modais, barras de progresso e tooltips compartilham os mesmos tokens semânticos. Hierarquia clara: fundo navy, superfícies raised, primário violet, hover magenta, foco visível cyan e texto branco. Erro, alerta e sucesso possuem cores funcionais próprias, distintas da marca. Espaçamentos: `4`, `8`, `16`, `24`; raios de borda: controles `8`, cards `12`, modais `20`. O estilo nunca sacrifica legibilidade ou contraste; evite animações contínuas e respeite preferências de redução de movimento.

## Adaptação por Plataforma

- **Genérico Windows / Linux / WebApp**: K branco com contorno índigo suave sobre degradê claro ice/lilac e sakuras discretas; ícone PNG, `.ico` para WinRes e ícones PWA por aplicação. No WebApp, apenas o brilho central fortemente clareado varia conforme o acento de cada jogo, mantendo as proporções exatas.
- **Android**: Widgets Material 3, camadas foreground/background em adaptive icon, ícone monocromático/temático e navegação nativa (sem simular Liquid Glass).
- **iOS e macOS**: Tipografia e gestos nativos em SwiftUI; Liquid Glass nas versões com suporte a `glassEffect` e materiais do sistema nas anteriores. Nunca substitua controles nativos por imagens estáticas.
- **Dear ImGui**: Mantém o fluxo de navegação existente com estados tokenizados para botões, seletores, popups, barras, abas, cards e janelas no launcher e no debug overlay.
- **Interfaces originais dos jogos**: As interfaces internas dos jogos portados nunca são substituídas pela identidade do motor.

## Splash Universal

Fundo puro `#000000`, marca exatamente centralizada e preservação da razão de aspecto, sem molduras nem suposições de orientação retrato/paisagem. O renderizador preenche em preto o espaço restante. (Nota: o port `school-days` preserva seus overrides autorizados em `assets/SPLASH.PNG` e `assets/SchoolDays.ico`.)

## Reprodução, Distribuição e Fonte da Verdade

A pasta `design/` é a fonte única da verdade da identidade visual. O gerador grava diretamente em `design/generated/` (versionado no Git), e não em um diretório efêmero de build. CMake, Xcode, Android Gradle, WinRes e o shell web importam apenas os arquivos específicos de cada plataforma; a pasta `design/` **nunca** é empacotada como diretório de assets em tempo de execução.

Gere todos os recursos com:

```sh
python3 tools/generate_brand_assets.py --kind all
```

Para o ícone do macOS no Icon Composer (exportação `.icon`), gere o PNG full-bleed `1024x1024`:

```sh
python3 tools/generate_brand_assets.py --kind apple
# -> design/generated/apple/AppIcon-1024.png
```

Use `--out <pasta>` sempre que executar testes isolados ou exportações avulsas para não modificar os arquivos rastreados em `design/generated/`.

O modo `--kind desktop` também emite esse PNG full-bleed `1024x1024` (`alpha=255` nos quatro cantos, eliminando bordas brancas no macOS). Os binários em `design/generated/` são artefatos derivados e não devem ser editados manualmente. `design/tokens.json` armazena a tabela estruturada de tokens e `tools/generate_pwa_icons.py` define a geometria de referência do K.

## Observações sobre PWA e Empacotamento

No WebAssembly, o shell HTML utiliza o splash universal gerado mantendo independente o splash interno de cada jogo. O CMake incorpora `icon.ico` na seção WinRes dos executáveis Windows (exceto `school-days`, que utiliza `assets/SchoolDays.ico`), instala `icon.icns` em `.app/Contents/Resources/` no macOS e referencia os PNGs Apple no `Info.plist` compartilhado de iOS.

## Referência de Interface Visual v2.0 e Variante ImGui "Soft"

`design/tokens.json` (versão 2) rege o shell web (controles violeta/índigo, foco ciano, barra de progresso em degradê e splash preto centralizado), superfícies Material 3 no Android e views SwiftUI em iOS/macOS.

Para o cabeçalho do launcher e o debug overlay em Dear ImGui (`lib/src/debug/ImGuiTheme.cpp`), aplica-se a derivação dessaturada **"Soft"**, preservando inalterados `design/tokens.json`, o monograma K e os ícones de plataforma:

| Papel | Base v2 | Soft ImGui |
| --- | --- | --- |
| Fundo da janela (`Navy`) | `#0D142E` | `#1B2740` |
| Superfície elevada (`Raised`) | `#182449` | `#273451` |
| Cabeçalho de tabela / seleção (`Indigo`) | `#2C3E9A` | `#525D84` |
| Acento / título (`Violet`) | `#6A5BE7` | `#A79BD4` |
| Hover (`Magenta`) | `#C86BD9` | `#C3B4E0` |
| Borda de tabela (`Cyan`) | `#00D6FF` | `#8FB8D9` |
| Borda / divisor (`Outline`) | `#39315E` | `#3D4763` |
| Texto primário (`White`) | `#FFFFFF` | `#DCE3F0` |
| Texto secundário (`Muted`) | `#B9B5D4` | `#9DA9C2` |
| Botão primário | `#6A5BE7` | `#525D84` |
| Sucesso / aviso / erro | `#47D7A1` / `#FFBE68` / `#FF7389` | `#7FC8A8` / `#E8C89A` / `#E0A3AC` |

A seleção de texto usa lilás com alpha `0.25` (antes `0.35`) e linhas alternadas de tabela usam slate com alpha `0.16`, mantendo contraste WCAG AA (`> 4.5:1` em texto normal e `> 3:1` em títulos).
