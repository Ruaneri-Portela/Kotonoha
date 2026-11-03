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
