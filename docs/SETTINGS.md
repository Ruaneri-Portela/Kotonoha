# Configurações compartilhadas

O menu interno da engine tem abas **Display**, **Audio** e **VN text**.
Use toque ou mouse; Tab troca a aba e End seleciona Aplicar.

Audio oferece volume geral, mute geral e os canais do mixer. Os controles
aceitam arrastar o dedo. O ganho escolhido pelo jogador é multiplicado pelo
volume nativo do roteiro e pelo ganho de cada stream, preservando fades e
comandos de áudio do jogo. Canais criados depois herdam os valores salvos.

VN text permite selecionar uma fonte TTF/OTF/TTC da pasta de fontes da engine,
ou voltar à fonte original. Diálogo e nome têm seletores de cor separados;
desligar a substituição recupera as cores originais. A mudança ocorre ao aplicar,
sem reiniciar a novel. AIR, DDLC, Summer, Snow, Zero e legendas libass usam essas
preferências na apresentação nativa, sem reescrever o texto ou o estado da VM.

O launcher expõe volumes, fonte e cores em Advanced, incluindo edição RGBA.
Cada porte injeta seus canais usando `Launcher::setAudioChannels`; o motor não
mantém uma lista fixa de mixers para todos os jogos. Valores antigos de outros
canais não acrescentam controles ao launcher.
A seleção de shaders só aparece para MPV/OpenGL, incluindo Automatic quando
esse backend é resolvido. Outros upscalers continuam com o fator de escala.

Os campos opcionais `channelVolumes`, `vnFontPath`, `vnTextColorOverride`,
`vnTextColor` e `vnSpeakerColor` são persistidos no settings.json do jogo.
Configurações antigas continuam válidas.

## Legendas forçadas

`Enable forced subtitles`, no Advanced do launcher, é salvo em
`enableForcedSubtitles` (ativado por padrão). O recurso vale para todas as
novels, incluindo interpretadores nativos como AIR e roteiros VN Standard
ou Timed; não depende de vídeos nem de cues de legenda na timeline.

As legendas `.ass` ficam em `forced_subtitles/`, dentro do diretório de trabalho
(`working_dir`), com busca recursiva. Dump e leitura usam essa mesma pasta,
em vez da pasta de documentos do jogo. O nome usa o ID completo mostrado no canto:
por exemplo, `80005:66` corresponde a `80005-66.ass`. Caracteres inválidos
em nomes de arquivo são substituídos por `-`; evite nomes duplicados.

Em Timed, o nome é somente o nodo (`8000.ass`) e os tempos ASS seguem o
clock global desse nodo/timeline, preservando seeks e restauração de posição.
Nos interpretadores Standard, o nome inclui evento e posição
(`8000:30` no HUD corresponde a `8000-30.ass`), e o clock conta o tempo não
pausado desde a entrada nessa cena, zerando quando o ID completo muda.
Um clock global de diagnóstico de um interpretador Standard não é usado
como clock da cena. Roteiros VN simples sem uma posição nativa usam seu ID.
O HUD exibe o mesmo clock das legendas; F8 alterna sua visibilidade.
As legendas são desenhadas sobre as cenas da novel, mas abaixo de Settings,
InGameMenu, Backlog, saves, diálogos e painéis nativos. Ao pausar, continuam
visíveis atrás da UI, com o clock congelado. Sair do gameplay ou desligar o
recurso limpa a legenda atual.

Em F10, **Generate Forced Subtitles (.ass)** gera um modelo para o nodo/evento
atual usando exatamente o mesmo ID do HUD, sem exigir um nodo Timeline e sem
sobrescrever arquivos existentes. `PlayResX` e `PlayResY` recebem o tamanho
lógico da cena, não a resolução da tela. A rasterização das legendas usa os
pixels físicos da área apresentada, incluindo a densidade Retina, tanto nas
legendas forçadas quanto nas legendas nativas de School Days.
Edite o modelo, troque `Comment` por
`Dialogue` e ajuste tempos e texto. Use **Reload forced subtitles** para
reler arquivos novos ou editados sem reiniciar o jogo.
