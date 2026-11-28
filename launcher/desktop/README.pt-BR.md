# Kotonoha Manager (Avalonia)

**Português (Brasil)** | [English (US)](README.md)

Gerenciador de jogos desktop feito com Avalonia 12 / .NET 10. Segue os padrões
de interface dos launchers Android e iOS/macOS: biblioteca pesquisável, painel
de detalhes, parâmetros por jogo, ajustes globais e execução nativa in-process
pela mesma ABI C.

## Mapa da interface

| Superfície | Equivalente Android / iOS |
|---|---|
| Cabeçalho: marca, busca, Rescan, + Pasta, + ISO/BIN, + ZIP, Ajustes | toolbar do `LauncherActivity` / header do `KotonohaLauncherView` |
| Cards: título, badge de engine, badge FOLDER/DISC, badge OVERLAY, Play | `item_game_card.xml` / `KotonohaGameItem` |
| Painel de detalhes: caminho, diretório de saves, Jogar / Parâmetros… | painel de detalhes split do iOS/macOS |
| Diálogo de parâmetros: overlays, localização, rota School Days, saves, gráficos | `dialog_game_parameters.xml` / `KotonohaGameParametersSheet` |
| Diálogo de ajustes: raízes da biblioteca, saves isolados, padrões gráficos | `dialog_graphics_settings.xml` / `MacSettingsSheet` |

O tema visual é **WinUI 3 / Fluent2** (Avalonia 12) com tokens escuros do
Windows 11 centralizados em `Styles/Theme.axaml` (superfície `#2C2C2C`, cartão
`#323232`, acento `#0F6CBD`, texto `#FFFFFF`, secundário `#9E9E9E`, raio 8).
Renderer, upscaler e preset são `ComboBox`; o fator de escala é um `Slider`
(1–4×). O ícone vem de `design/generated/generic/icon.ico`.
O idioma pode ser alternado em tempo real entre **Português** e **English**
pelo seletor no cabeçalho; todos os textos vêm de `Services/Localization.cs`.

## Ponte nativa

`KotonohaLauncher.cs` é vinculado a partir de `launcher/bridge/dotnet/`, de modo
que a mesma fachada C ABI dos outros frontends é compilada neste app.
`NativeLibraryResolver` localiza `KotonohaLauncherCore.dll` (ou `.dylib`/`.so`)
em `KOTONOHA_NATIVE_DIR`, na saída do Visual Studio
`buildsystems/visualstudio/out/x64/Release/` ou em uma compilação CMake.
Nenhuma biblioteca nativa é versionada.

`KotonohaLauncherRunDll` é o equivalente Windows de um fork: o jogo inicia em
um processo `rundll32.exe` na sua própria main thread (requisito do SDL). O
gerenciador recebe o PID, captura stdout/stderr no painel de detalhes, mantém o
card verde enquanto o processo está ativo e mostra uma notificação transitória
quando ele encerra. Os caminhos por jogo (overlay, tradução, estilo de legendas,
documentos, rota, scripts) usam seletores nativos de arquivo/pasta.

## Compilar e executar

```sh
dotnet build launcher/desktop/KotonohaManager.csproj -c Release
```

Para o VS Code, `Properties/launchSettings.json` define dois perfis com
`KOTONOHA_NATIVE_DIR` apontando para o output nativo do Visual Studio
(`buildsystems/visualstudio/out/x64/<Configuration>`), que contém
`KotonohaLauncherCore.dll` e as DLLs de runtime do vcpkg. O `.vscode/launch.json`
local apenas seleciona um perfil via `launchSettingsProfile`. O app define seu
diretório de trabalho como o output do dotnet, onde `assets/` é copiado.

Defina `KOTONOHA_NATIVE_DIR` para um diretório que contenha
`KotonohaLauncherCore` e suas dependências antes de iniciar:

```powershell
$env:KOTONOHA_NATIVE_DIR = 'E:\Kotonoha\buildsystems\visualstudio\out\x64\Release'
dotnet run --project launcher\desktop\KotonohaManager.csproj -c Release
```

Na primeira execução a raiz padrão é `Documents/Kotonoha/games`
(`MainWindowViewModel.DefaultGamesRoot`). Raízes, isolamento de saves e
padrões gráficos são persistidos como JSON em
`%APPDATA%/Kotonoha/KotonohaManager/settings.json` — fora do VFS somente leitura.

A importação de ZIP protege contra path traversal e aplica os mesmos limites de
10.000 entradas / 64 GiB do importador SAF do Android.

## Verificação

```sh
dotnet build launcher/desktop/KotonohaManager.csproj -c Release --no-restore
```

A varredura carrega o catálogo nativo completo e sem duplicatas em uma tarefa
assíncrona e atualiza a biblioteca na thread da interface, preservando os cartões
dos jogos em execução. A profundidade padrão é 0 (raiz e filhos diretos).
