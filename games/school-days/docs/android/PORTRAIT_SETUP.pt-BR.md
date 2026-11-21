# School Days HQ no Android — Launcher em Retrato e Port Completo

O port Android do School Days HQ compartilha a mesma lógica C++17 de jogo, montagem de pacotes, validação de traduções, persistência retail `Config.DAT` e integração L×H da build Windows L4. O launcher abre em **retrato**; o gameplay inicia em **paisagem tela cheia** (`sensorLandscape`) com controles por toque.

## Estrutura da Pasta no Celular (`SchoolDaysHQ/`)

Coloque os seus arquivos legítimos do School Days HQ no armazenamento interno (por exemplo `/storage/emulated/0/SchoolDaysHQ/` ou `/storage/emulated/0/Download/SchoolDaysHQ/`):

```text
SchoolDaysHQ/
├── Packs/
│   ├── Script.GPK
│   ├── System.GPK
│   ├── ... todos os 29 GPKs originais HQ ...
│   └── LxH.GPK
├── game-assets/
│   ├── school-days-hq.ktnroute
│   ├── styles.ass                  (folha de estilos ASS opcional)
│   ├── launcher-art/
│   │   └── SchoolDaysHQ-Launcher.png  (arte opcional do launcher)
│   └── forced_subtitles/           (opcional, <idioma>/<nodo>.ass)
│       ├── en/
│       ├── pt-BR/
│       ├── es/
│       ├── ru/
│       └── zh-CN/
├── overlay/
│   └── System/Option/              (Display Options e substituições opcionais)
└── translations/
    ├── scripts/                    (pacotes GPK nativos: Script - *.GPK + *-lxh.GPK)
    └── jsons/                      (pares JSON: <idioma>.json + <idioma>-lxh.json + reviewed-identities)
```

> **Importante:** Selecione sempre a pasta pai `SchoolDaysHQ/` no seletor do Android — **nunca** a subpasta `Packs/` diretamente. O SAF do Android só concede leitura à pasta escolhida e às suas subpastas; escolher `Packs/` impede acesso a `game-assets/`, `overlay/` e `translations/`.

## Guia Rápido de Uso

1. Instale o APK ARM64 no aparelho Android (Android 11 / API 30+).
2. Abra o **School Days HQ** (launcher em retrato).
3. Toque em **Selecionar pasta SchoolDaysHQ** e autorize a pasta `SchoolDaysHQ`.
4. Abra **Opções** para configurar:
   - **Tela**: Resolução nativa / taxa de atualização, VSync, Proporção narrativa (`Automático` / `4:3` / `16:9`), Limite de quadros Android (`Ilimitado` / `30` / `60` FPS).
   - **Gráficos**: Escala de renderização (`0.25x`–`1.00x`), indicador OpenGL ES, Upscaler (`Auto` / `None` / `Linear` / `MPV`), Fator de upscale (`1.00x`–`4.00x`), Preset de shader (Anime4K / ArtCNN em níveis Low/Medium/High).
   - **Áudio**: Volumes retail `BGM`, `SE`, `Vozes` (0–10), Vozes masculinas, Silenciar jogo, Volumes Master e Vídeos do motor, e **Música de fundo do launcher** (reproduz `TitleBGM` de `Ini.GPK`/`BGM.GPK` somente no launcher, independente do mudo do jogo).
   - **Geral**: Exibir texto, Pular cenas não lidas, SuperSkip / próxima escolha, Auto draw, Modo automático, Inverter botões A/B, Ativar legendas forçadas (`.ass`).
   - **Idioma**: Escolha entre **Pacotes GPK nativos** (`translations/scripts/`) ou **Dicionários JSON** (`translations/jsons/`) para `en`, `pt-BR`, `es`, `ru`, `zh-CN`. Arquivos HQ e L×H são obrigatórios; fallback silencioso para inglês é bloqueado.
   - **Arquivos**: Confira contagem de pacotes (`30/30`), rota KTRF, `styles.ass`, `forced_subtitles/`, `overlay/` e traduções detectadas; troque ou desconecte a pasta.
   - Toque em **Aplicar** para gravar (`settings.json`, `save/Config.DAT`, `save/Kotonoha.ini`), **Cancelar** para descartar ou **Restaurar padrões**.
5. Toque em **Iniciar jogo** para jogar em paisagem tela cheia.
6. Controles no jogo: toque na tela para avançar/escolher opções, toque no botão **MENU** no canto inferior esquerdo para abrir a barra retail (Save, Load, BackLog, Route Map, Config, Auto, Skip, Title) e use o botão **Voltar** do Android para fechar menus modais.

## Arquitetura e Limites de Armazenamento

- **Montagem SAF somente leitura (`DocumentMount`)**: Os 30 arquivos (`Packs/*.GPK` + `LxH.GPK`) permanecem no local original e são lidos via URIs SAF com cache de stream protegido por mutex por arquivo (`SchoolDaysGpkArchive`), eliminando reaberturas repetidas sem copiar gigabytes de pacotes.
- **Cache temporário por sessão (`SchoolDaysFolder`)**: Apenas arquivos pequenos que exigem caminho físico (`school-days-hq.ktnroute`, GPKs ou JSONs + sidecars `reviewed-identities` do idioma ativo, `game-assets/styles.ass`, `forced_subtitles/` do idioma ativo e `overlay/`) são preparados no cache do app e removidos ao sair.
- **Configurações e saves compartilhados**: `settings.json` fica em `Android/data/<pacote>/files/settings.json`; `Config.DAT`, `Kotonoha.ini`, `GlobalFlag.DAT` e `Save/` ficam em `Android/data/<pacote>/files/save/` — nunca dentro do SAF nem na pasta de arquivos do jogo.
- **Legendas forçadas**: Preferência por `game-assets/forced_subtitles/<idioma>/<nodo>.ass` (encaminhado via `--forced-subtitles` em execuções SAF), mantendo compatibilidade legada com `forced_subtitles/`.
- **Segurança de fontes no ImGui**: `ImGuiTheme::loadFonts` valida limites SFNT/TTC e compatibilidade com o carregador ativo do ImGui, usando `AddFontDefaultVector()` como fallback explícito quando coleções CJK do sistema (como `/system/fonts/NotoSansCJK-Regular.ttc`) são incompatíveis com `stb_truetype` ou em telas de alto DPI.
- **Empacotamento do APK**: Nenhum GPK comercial, rota KTRF, tradução ou arte comercial entra no APK (`-PbundleGameAssets=false`). Todas as bibliotecas ARM64 `.so` usam alinhamento ELF de 16 KiB (`-Wl,-z,max-page-size=16384`).

## Resumo de Verificação (POCO X7 Pro, Android 16 / API 36)

Verificado em hardware real **POCO X7 Pro (ARM64, Android 16)** com acesso SAF somente leitura à pasta `SchoolDaysHQ/` do usuário:
- Launcher em retrato, validação da pasta (`30/30` pacotes, KTRF, `styles.ass`, `overlay/`, detecção GPK e JSON), todas as 6 abas de Opções, `Aplicar`/`Cancelar`/`Restaurar padrões`, persistência em `settings.json` / `Config.DAT` / `Kotonoha.ini` e reprodução real de `TitleBGM` a partir de `Ini.GPK` + `BGM.GPK`.
- Inicialização rápida em paisagem (`sensorLandscape`, `opengles2`, áudio de baixa latência) nos modos **JSON (`pt-BR`)** e **GPK nativo (`pt-BR`)**, sem SIGABRT nem fallback silencioso.
- Tela de título, menu `OPTION` nativo (com overlay `Display Options`), vídeo HD `YUV420P` + áudio do Episódio 1, legendas, botão tátil `MENU` e ciclo Home/Retomada (`!isTaskRoot()`).
- Uso de memória durante gameplay: ~517 MB TOTAL PSS (Gráficos ~78 MB).
- Builds Windows DEV Release e Windows DIST Release e suítes CTest aprovadas sem regressões.
