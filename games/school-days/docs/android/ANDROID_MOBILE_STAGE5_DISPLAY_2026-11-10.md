# Android Display Options — paridade com PC (Etapa 5)

O painel Android é uma interface nativa Java em
`Android/app/src/main/java/org/kotonoha/android/SchoolDaysMobileDisplayOptions.java`.
O PC e a interface interna do jogo não foram modificados. A persistência móvel utiliza
os campos já reconhecidos pelo motor no `settings.json` e `ContentAspectPreference`
em `Kotonoha.ini`, os mesmos lidos por `SchoolDaysRuntime`.

| Display Options no PC | Android | Implementação |
|---|---|---|
| Windowed, borderless, exclusive | Não aplicável | Surface Android é sempre paisagem sem janela; `mode=1` (borderless) |
| Monitor, resolução da janela, redimensionável | Não aplicável | Sem monitor/desktop virtual no telefone |
| Proporção Standard 4:3, Wide 16:9, Auto | Adaptado | Spinner em dp; `ContentAspectPreference=0/1/2` em `Kotonoha.ini`, mesmo formato usado no jogo |
| Escala de renderização | Feito no código | Native 100%, Balanced 75%, Performance 50%; `settings.json.renderScale` |
| Resolução e taxa de atualização | Adaptado | Apenas modos realmente reportados por `Display.getSupportedModes()`; `fullscreenWidth/Height/refreshRate` no JSON |
| Limite de FPS | Android próprio | Sem limite extra, 30 ou 60 FPS por frame pacing apenas no callback SDL Android; `AndroidFrameLimit` em `Kotonoha.ini`. **Não altera** roteiro/vídeos com clock de 24 fps |
| VSync | Feito no código | `settings.json.vsync` |
| Renderer backend | Fixo | `opengles2`, sem escolhas de D3D/Metal/software PC |
| Upscaler / shader preset | Adaptado | Off, Linear, Auto ArtCNN / Denoise / Large / Anime4K, `upscaler`, `preset`, `factor` e `shaderFiles` do formato de PC; Auto pode cair para Linear conforme suporte GLES do dispositivo |
| GLSL personalizado, caminhos de arquivos | Não aplicável | Sem seletor desktop nem shaders externos na launcher móvel |

Verificação desta etapa: análise das opções `SchoolDaysLauncher.cpp` e `SchoolDaysRuntime.cpp` e compilação nativa/Java ARM64. O funcionamento real das opções de frequência e shaders depende de hardware: não marcar como testado enquanto ADB não estiver autorizado ou sem teste manual.

O frame pacing é aplicado **somente** em `main.cpp` Android, antes da próxima iteração SDL. `0` permite VSync/device; valores não reconhecidos são ignorados em vez de inventar uma taxa. O perfil persistido de display pertence ao pacote Android selecionado, sem alterar a distribuição Windows nem saves retail.
