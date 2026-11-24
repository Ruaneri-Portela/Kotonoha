# Exemplo de integração .NET (Avalonia)

App de teste manual da ponte P/Invoke de `KotonohaLauncherCore`. Fica em
`launcher/examples/dotnet/` — **não** é compilado pelo CMake/CTest; é um exemplo
de integração executado à mão. Nenhum asset comercial de jogo é incluído.

## Estrutura

```
launcher/examples/dotnet/
├── App.axaml, App.axaml.cs        # bootstrap Avalonia
├── Program.cs                     # resolver da nativa + cwd dos assets + --selftest
├── HeadlessSelfTest.cs            # exercita TODA a C ABI e lança cada jogo
├── MainWindow.axaml(.cs)          # GUI: Add folder, Scan, Launch all detected
├── Bridge/KotonohaLauncher.cs     # facade P/Invoke (cópia adaptada do repo)
├── KotonohaBridgeTest.csproj      # net10.0 + Avalonia 12.2.0
└── app.manifest
```

## Pré-requisitos

- .NET SDK 10 (`/usr/local/share/dotnet/dotnet`).
- Core construído:
  `cmake --build out/build/macos-arm64-vcpkg --target KotonohaLauncherCore`.

## Build e execução

```sh
cd launcher/examples/dotnet
DOTNET=/usr/local/share/dotnet/dotnet
"$DOTNET" build KotonohaBridgeTest.csproj -c Debug
"$DOTNET" run --project KotonohaBridgeTest.csproj --no-build -- --selftest
"$DOTNET" run --project KotonohaBridgeTest.csproj --no-build   # GUI
KOTONOHA_LAUNCHER_LAUNCH_FRAMES=1 ...  # frames por jogo (padrão: 1)
KOTONOHA_LAUNCHER_LAUNCH_DRY_RUN=1 ... # validação sem SDL
```

O repo é descoberto por `KOTONOHA_REPO` ou subindo 6 níveis a partir de
`bin/Debug/net10.0`. `Program.Main` troca o cwd para
`out/build/macos-arm64-vcpkg/bin` (assets/fonts) e pré-carrega as dylibs
`@rpath` por caminho absoluto antes do `SetDllImportResolver`.

O seletor nativo de pastas da GUI (**+ Adicionar pasta**) aponta para uma
biblioteca inteira; o scan do core é recursivo.

## Recursos exercitados × resultado

Executado em 2026-11-24 com o core Release arm64 e
`dotnet run ... -- --selftest` sobre `/Volumes/Server/data2/Games`
(`KOTONOHA_LAUNCHER_LAUNCH_FRAMES=1`):

| Recurso (C ABI) | Resultado literal (trecho) |
|---|---|
| `KotonohaLauncherScanRootsJson` | `[SCAN] Kind=Array, entries=32` |
| `KotonohaLauncherDetectPathJson` | raiz → `{}`; jogo → `engineSlug:"luca-system"` |
| `KotonohaLauncherLoadGameParamsJson` | JSON com os 11 campos de parâmetros |
| `KotonohaLauncherSaveGameParams` | `[FEATURE][SaveGameParams] True` |
| `KotonohaLauncherUpscalePresetsJson` | `tamanho=639` (15 presets) |
| `KotonohaLauncherDefaultSchoolDaysRoute` | vazio no core; fallback para `games/school-days/game-assets/school-days-hq.ktnroute` |
| `KotonohaLauncherValidateRoute` | `valid=True; info='School Days route is valid'` |
| `KotonohaLauncherResolveSaveDirectory` | `.../Kotonoha-Profiles/dotnet-bridge-profile/save` |
| `KotonohaLauncherFreeString` | exercitado por `TakeString()` em toda string nativa |
| `KotonohaLauncherLaunchGame` | **`[LAUNCH] ok=27 fail=5 total=32`** (launch real, 1 frame/jogo) |

As 5 falhas vêm de dados incompletos nos discos (CMPs sem
`BGMDATA.CMP`/`INI.CMP`/`VOICE.CMP` e `SchoolDays` sem `--ktrf`), não da ponte.
Exemplo de sucesso literal:

```
[LAUNCH] OK /Volumes/Server/data2/Games/DDLC => {"ok":true,"phase":"iterate","message":"Game launched","frames":1,"dryRun":false,"path":"/Volumes/Server/data2/Games/DDLC","engineSlug":"doki-doki-literature-club"}
```

Log completo: `$TMPDIR/TMPLOG/kotonoha-bridge-selftest.log`.
