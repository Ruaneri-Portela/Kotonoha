# KotonohaBridgeTest / DotNetApp — launch individual

App .NET 10 com interface **Avalonia UI** (multiplataforma; WinForms/WPF não
rodam no macOS) para exercitar a ponte P/Invoke de
`KotonohaLauncherCore`. Fica em `launcher/examples/dotnet/`.

A GUI faz scan recursivo da biblioteca e mostra uma **ListBox com os jogos
detectados**. O botão **“Launch game”** inicia **somente o jogo selecionado**,
um por vez, montando `KotonohaGameLaunchRequest` com `gamePath`, `engineSlug`,
`saveProfileId` (todos do JSON de scan) e todos os opcionais. O botão
**“Validate request”** reporta
`launch request validated; graphical launch requires SDL app` sem iniciar SDL.

O layout coloca os controles/opções à **esquerda** e a `ListBox` de jogos à
**direita**, com um `TextBox` somente-leitura “Log do jogo selecionado” abaixo
da lista. Enquanto o launch bloqueante está em andamento, apenas o item atual
fica **verde** (`● RUNNING / EM EXECUÇÃO`); ao fechar a janela SDL a cor volta
ao normal, e o log daquele jogo — incluindo o JSON final com
`Game exited cleanly`/`frames` — aparece ao selecioná-lo.

## Estrutura

```
launcher/examples/dotnet/
├── App.axaml, App.axaml.cs        # bootstrap Avalonia
├── Program.cs                     # resolver da nativa + modo --selftest
├── GameEntry.cs                   # uma linha do scan (todos os campos da request)
├── HeadlessSelfTest.cs            # exercita TODA a C ABI e lança cada jogo
├── MainWindow.axaml               # GUI: roots, opções, ListBox de jogos, Launch game
├── MainWindow.axaml.cs            # handlers, BuildRequest, LaunchOneAsync, auto-modo
├── Bridge/KotonohaLauncher.cs     # façade P/Invoke (cópia do repo)
└── KotonohaBridgeTest.csproj      # net10.0 + Avalonia 12.2.0
```

## Pré-requisitos

- SDK: `/usr/local/share/dotnet/dotnet` (10.0.401)
- Biblioteca nativa já construída:
  `/Users/nero/Codes/Kotonoha/out/build/macos-arm64-vcpkg/lib/libKotonohaLauncherCore.dylib`
- Dependências `@rpath` em
  `out/build/macos-arm64-vcpkg/vcpkg_installed/macos-arm64-vcpkg/lib` e
  `out/build/macos-arm64-vcpkg/lib` (SDL3, SDL3_ttf, FFmpeg, zlib, engine,
  ImGui). O `Program.cs` pré-carrega essas dylibs por caminho absoluto porque
  o core não tem `LC_RPATH`; assim não é preciso exportar
  `DYLD_LIBRARY_PATH`. Se preferir, também funciona com:

  ```sh
  export DYLD_LIBRARY_PATH=/Users/nero/Codes/Kotonoha/out/build/macos-arm64-vcpkg/vcpkg_installed/macos-arm64-vcpkg/lib:/Users/nero/Codes/Kotonoha/out/build/macos-arm64-vcpkg/lib
  ```

## Abrir a GUI e launchar um jogo por vez

```sh
DOTNET=/usr/local/share/dotnet/dotnet
APP=/Users/nero/Codes/Kotonoha/launcher/examples/dotnet/KotonohaBridgeTest.csproj

$DOTNET build "$APP" -c Debug
$DOTNET run --project "$APP" --no-build       # abre a GUI
```

Também é possível abrir o apphost diretamente:

```sh
/Users/nero/Codes/Kotonoha/launcher/examples/dotnet/bin/Debug/net10.0/KotonohaBridgeTest
```

Na interface:

1. Confirme o root `/Volumes/Server/data2/Games` na lista “Bibliotecas /
   pastas de jogos” (use **+ Adicionar pasta** para outra biblioteca; o scan é
   recursivo).
2. Clique em **Scan**. A ListBox “Jogos detectados” mostra título, engine e
   caminho de cada jogo.
3. Selecione **um** item (ex.: `DDLC`). Os campos “Parâmetros opcionais da
   request” são preenchidos com os valores do JSON daquele jogo.
4. Clique em **Launch game** (botão verde). O status mostra
   `Launched OK: DDLC` ou `Launch ERRO: ...`, e o JSON da chamada aparece na
   área “// LaunchGame”. Feche a janela SDL do jogo antes de lançar o próximo.
5. Repita com `AIR`. Cada clique inicia apenas o jogo selecionado.

Outros controles:

- **Validate request** — monta a request e reporta literalmente
  `launch request validated; graphical launch requires SDL app`, exibindo a
  request serializada.
- **Test launcher only** — checkbox que passa `DryRun = true` na C ABI; o core
  valida sem SDL/a runtime do jogo.
- **Parâmetros opcionais da request** — expander com overlay, tradução,
  estilo de legenda, documents dir, ktrf/rota, scripts, cenário inicial,
  renderer, upscaler, preset e fator.
- **Separar saves por jogo** / **Custom save root** — isolamento de saves.

Modo automático da GUI (usado na verificação, sem automação de mouse):

```sh
$DOTNET run --project "$APP" --no-build -- --auto-scan --auto-launch=DDLC
$DOTNET run --project "$APP" --no-build -- --auto-scan --auto-launch=AIR
$DOTNET run --project "$APP" --no-build -- --auto-scan --auto-launch=DDLC --validate
```

### Resolver da nativa

`Program.Main` registra, **antes de qualquer chamada P/Invoke**:

```csharp
NativeLibrary.SetDllImportResolver(
    typeof(KotonohaLauncher).Assembly, (name, asm, path) =>
        name == "KotonohaLauncherCore"
            ? NativeLibrary.Load("/Users/nero/Codes/Kotonoha/out/build/macos-arm64-vcpkg/lib/libKotonohaLauncherCore.dylib")
            : IntPtr.Zero);
```

A façade mantém `DllImport("KotonohaLauncherCore", CallingConvention.Cdecl)`,
`LPUTF8Str` e libera strings nativas via `KotonohaLauncherFreeString`.

## Harness headless

```sh
$DOTNET run --project "$APP" --no-build -- --selftest
KOTONOHA_LAUNCHER_LAUNCH_HEADLESS=1 $DOTNET run --project "$APP" --no-build -- --selftest
```

O primeiro modo usa os roots padrão; `--root <caminho>` acrescenta um root.
`KOTONOHA_LAUNCHER_LAUNCH_HEADLESS=1` seleciona SDL dummy e limita a 2 frames
por jogo. Logs: `$TMPDIR/TMPLOG/kotonoha-bridge-gui.log` e
`kotonoha-bridge-selftest.log` (nunca na Desktop).

## Teste executado (24/11/2026, `/Volumes/Server/data2/Games`)

GUI — DDLC e AIR selecionados um por vez e launchados:

```
[15:43:57] Launch OK   DDLC => {"ok":true,"phase":"iterate","message":"Game launched","frames":2,"dryRun":false,"path":"/Volumes/Server/data2/Games/DDLC","engineSlug":"doki-doki-literature-club"}
[15:42:53] Launch OK   AIR => {"ok":true,"phase":"iterate","message":"Game launched","frames":2,"dryRun":false,"path":"/Volumes/Server/data2/Games/AIR","engineSlug":"luca-system"}
```

Harness headless com o core atual:

```
[LAUNCH] OK   playable=True /Volumes/Server/data2/Games/DDLC => {"ok":true,"playable":true,"phase":"iterate","message":"Game launched","frames":2,"dryRun":false,"path":"/Volumes/Server/data2/Games/DDLC","engineSlug":"doki-doki-literature-club"}
[LAUNCH] OK   playable=True /Volumes/Server/data2/Games/AIR => {"ok":true,"playable":true,"phase":"iterate","message":"Game launched","frames":2,"dryRun":false,"path":"/Volumes/Server/data2/Games/AIR","engineSlug":"luca-system"}
```

Validação (sem SDL):

```
[15:45:23] Validate request => launch request validated; graphical launch requires SDL app
```

Capturas da GUI (janela isolada, jogo selecionado):

- `/private/var/folders/m4/75wbzb6924vfj11bz9_43f400000gn/T/opencode/dotnet-ddlc-window.png` — DDLC selecionado + `Launched OK: DDLC`.
- `/private/var/folders/m4/75wbzb6924vfj11bz9_43f400000gn/T/opencode/dotnet-ddlc-clean.png` — AIR selecionado + `Launched OK: AIR`.
- `/private/var/folders/m4/75wbzb6924vfj11bz9_43f400000gn/T/opencode/dotnet-validate.png` — request validada.

## Observações

- Nenhum asset comercial de jogo é incluído; apenas o código do app de teste.
- `SaveGameParams` grava nas configurações do usuário (fora do repo), como a
  nativa faz.
- `ScanRootsJson` na GUI com a biblioteca inteira retornou 32 jogos e JSON
  válido/formatado.
