# Configurações compartilhadas

O menu interno da engine tem abas **Display**, **Audio** e **VN text**.
Use toque ou mouse; Tab troca a aba e End seleciona Aplicar.

VSync vem ativado por padrão em todos os jogos, inclusive ao carregar uma
configuração sem esse campo. Uma escolha salva de VSync desligado é preservada.

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

Com upscaling ativado, a logo de boot do motor também recebe o filtro.
A imagem original é preservada; a textura filtrada é preparada uma vez e
reutilizada durante o fade, sem filtrar o canvas ou cada quadro. Trocar a
configuração invalida somente essa textura derivada; desativar restaura a original.

Legendas, diálogos e textos rasterizados não passam pelo upscaler de imagens.
No AIR, inclusive texto criado pelo roteiro é desenhado depois do filtro global
da cena, preservando a rasterização original dos glifos.

Os campos opcionais `channelVolumes`, `vnFontPath`, `vnTextColorOverride`,
`vnTextColor` e `vnSpeakerColor` são persistidos no settings.json do jogo.
Configurações antigas continuam válidas.

## Legendas forçadas

`Enable forced subtitles`, no Advanced do launcher, é salvo em
`enableForcedSubtitles` (ativado por padrão). O recurso vale para todas as
novels, incluindo interpretadores nativos como AIR e roteiros VN Standard
ou Timed; não depende de vídeos nem de cues de legenda na timeline.

Ao construir o renderer, a pasta `forced_subtitles/` é resolvida pela mesma
ordem de busca de referências do overlay (`FileResolver::candidates`).
O primeiro diretório existente é guardado como caminho absoluto; a busca
recursiva de ASS e os reloads usam essa mesma origem, mesmo se o diretório de
trabalho mudar. Se nenhuma pasta existir na inicialização, não há origem
automática nessa sessão. Apenas o dump é salvo em
`current_path()/forced_subtitles/`. Depois de gerar um dump, o reload desse
nodo/evento passa a usar o caminho absoluto do dump durante a sessão, mesmo
se o diretório de trabalho mudar. Nodos sem dump continuam recarregando o
original da pasta resolvida na inicialização. O nome usa o ID completo mostrado no canto:
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
Quando falta largura para FPS e nodo, o HUD esconde primeiro o clock do nodo,
depois o clock do evento e, por fim, os milissegundos por frame. FPS e ID do
nodo continuam visiveis; os detalhes reaparecem ao aumentar a janela.
As legendas são desenhadas sobre as cenas da novel, mas abaixo de Settings,
InGameMenu, Backlog, saves, diálogos e painéis nativos. Ao pausar, continuam
visíveis atrás da UI, com o clock congelado. Sair do gameplay ou desligar o
recurso limpa a legenda atual.

A pausa do menu do jogo e da Debug UI usa o mesmo estado do engine,
consultado também pelos interpretadores nativos. Roteiro, mídia e clock
permanecem pausados enquanto qualquer fonte de pausa estiver ativa; fechar
uma UI não remove a pausa da outra. **Ignore pause** ignora apenas a pausa
da Debug UI, sem liberar um menu, Settings ou pausa via MCP ainda ativos.

Em F10, **Generate Forced Subtitles (.ass)** gera um modelo para o nodo/evento
atual usando exatamente o mesmo ID do HUD, sem exigir um nodo Timeline e sem
sobrescrever arquivos existentes. `PlayResX` e `PlayResY` recebem o tamanho
lógico da cena, não a resolução da tela. A rasterização das legendas usa os
pixels físicos da área apresentada, incluindo a densidade Retina, tanto nas
legendas forçadas quanto nas legendas nativas de School Days.
As texturas dos glifos são desenhadas diretamente, sem passar pelo upscaler
de imagens (Anime4K, ArtCNN, MetalFX ou linear), nem aplicar filtragem linear
à textura da legenda. A nitidez vem da rasterização na resolução física.
Edite o modelo, troque `Comment` por
`Dialogue` e ajuste tempos e texto. Use **Reload forced subtitles** para
reler arquivos novos ou editados sem reiniciar o jogo.
O inspetor exibe **Forced ASS directory** com a origem resolvida na
inicialização e **Active forced ASS** com o caminho completo do arquivo
carregado, ou `(none)` quando não há ASS ativo.
