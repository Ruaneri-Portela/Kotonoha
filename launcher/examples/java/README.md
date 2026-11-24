# Exemplo de integração JNI (JavaFX) — launch individual

App de teste manual da ponte Java/JNI de `KotonohaLauncherCore`. Fica em
`launcher/examples/java/` — **não** é compilado pelo CMake/CTest; é um exemplo
de integração executado à mão. Nenhum asset comercial de jogo é incluído.

A GUI faz scan recursivo da biblioteca, lista **cada jogo detectado** numa
`TableView` e contém um botão **“Launch game”** que inicia **somente a linha
selecionada**, um jogo por vez. Não há mais “launch all”.

## Por que não há mais shim JNI

`launcher/bridge/java/JavaBridge.cpp` é compilado para o core em **todas** as
plataformas (macOS/Linux/Windows via `KOTONOHA_JNI_BINDINGS`, Android via NDK).
O próprio `libKotonohaLauncherCore.dylib` exporta os símbolos
`Java_me_hirameki_kotonoha_launcher_KotonohaLauncherBridge_*`. O app carrega o
core diretamente com `System.load(...)`; não há cópia de `JavaBridge.cpp`, nem
`libKotonohaJniBridge.dylib`. O antigo `build.sh` com shim foi removido.

`NativeLibraries.java` pré-carrega as dependências `@rpath` por caminho
absoluto antes do core (a JVM ignora `DYLD_LIBRARY_PATH` com hardened runtime).

## Estrutura

```
launcher/examples/java/
├── build.sh                 # compila as classes; baixa os jars JavaFX se faltarem
├── run.sh                   # GUI JavaFX (launch real: abre a janela do jogo)
├── run-cli.sh               # harness headless (todos os recursos + launch por jogo)
├── lib/                     # jars JavaFX (ignorados pelo git)
├── out/                     # classes .class (ignoradas pelo git)
└── src/me/hirameki/kotonoha/
    ├── launcher/
    │   ├── KotonohaLauncherBridge.java  # facade JNI (cópia adaptada do repo)
    │   └── NativeLibraries.java         # resolve e carrega a nativa
    └── bridgetest/
        ├── BridgeTestApp.java           # GUI: Add folder, Scan, tabela, Launch game,
        │                                # Validate request, "Test launcher only"
        ├── BridgeTestCli.java           # harness headless
        ├── JsonUtil.java                # parse minimal do JSON (GameInfo completo)
        └── LaunchRequest.java           # KotonohaGameLaunchRequest único, com todos
                                         # os 16 campos + dryRun
```

## Pré-requisitos

- JDK 21+ (`/usr/libexec/java_home -v 21`) e JavaFX 21.0.2 para `mac-aarch64`
  (baixado do Maven Central para `lib/`, sem Maven/Gradle).
- Core construído:
  `cmake --build out/build/macos-arm64-vcpkg --target KotonohaLauncherCore`.

## Abrir a GUI e launchar um jogo por vez

```sh
cd launcher/examples/java
./build.sh
./run.sh                  # abre a interface JavaFX
```

Na interface:

1. Confirme o root `/Volumes/Server/data2/Games` na lista (use **Add folder**
   para acrescentar outra biblioteca; o scan é recursivo).
2. Clique em **Scan**. A tabela “Jogos detectados” mostra `Título | Engine |
   Save profile | Caminho`, com os dados vindos do JSON de scan.
3. Selecione **uma** linha (ex.: `DDLC`).
4. Clique em **Launch game** (botão verde). A call nativa
   `nativeLaunchJson` recebe `gamePath`, `engineSlug`, `saveProfileId` e todos
   os opcionais: overlay, tradução, legenda, save root, ktrf, scripts, cenário
   inicial, renderer e upscaler (preset/fator). O status mostra
   `Launched OK: <jogo> (playable=...)` ou `Launch ERRO: <jogo>`, e o JSON
   completo aparece em “Saída JSON” e no log.
5. Repita para outro jogo (ex.: `AIR`). Cada clique inicia apenas o jogo
   selecionado. Feche a janela do jogo antes de lançar o próximo (no launch
   real a janela SDL permanece aberta até o usuário sair).

Opções úteis da GUI:

- **Validate request**: monta a request e reporta literalmente
  `launch request validated; graphical launch requires SDL app`, exibindo o
  JSON da request (modo de validação, sem SDL).
- **Test launcher only**: checkbox que passa `dryRun=true` na própria C ABI; o
  core valida detecção/parâmetros/registry sem abrir janela.
- **Parâmetros opcionais do launch**: `TitledPane` com todos os campos da
  request; ao selecionar um jogo eles são preenchidos com os valores do JSON.
- **Split saves by game** / **Custom save root**: isolamento de saves.

## Harness headless (sem GUI)

```sh
./run-cli.sh /Volumes/Server/data2/Games            # scan recursivo + 1 launch/jogo
./run-cli.sh --headless /Volumes/Server/data2/Games # SDL dummy + limite de frames
./run-cli.sh --dry-run /Volumes/Server/data2/Games  # valida requests, sem SDL
```

`KOTONOHA_REPO` (opcional) aponta para a raiz do repo; o padrão é derivado da
posição do script (`launcher/examples/java/../../..`).
`KOTONOHA_LAUNCHER_LAUNCH_FRAMES` controla quantos frames cada jogo roda
(padrão: 2 em `--headless`). O working directory é trocado para
`out/build/.../bin` para que o motor encontre `assets/fonts`.

Modo automático da GUI (usado na verificação, sem automação de mouse):

```sh
./run.sh --auto-scan --auto-launch=DDLC             # seleciona DDLC e lança
./run.sh --auto-scan --auto-launch=AIR              # seleciona AIR e lança
./run.sh --auto-scan --auto-launch=DDLC --validate  # valida a request
```

## Teste executado (24/11/2026, core Release arm64, `/Volumes/Server/data2/Games`)

DDLC — GUI, linha selecionada e launch individual:

```
[15:41:19] Launch OK  DDLC => {"ok":true,"phase":"iterate","message":"Game launched","frames":2,"dryRun":false,"path":"/Volumes/Server/data2/Games/DDLC","engineSlug":"doki-doki-literature-club"}
```

AIR — GUI, linha selecionada e launch individual:

```
[15:41:56] Launch OK  AIR => {"ok":true,"phase":"iterate","message":"Game launched","frames":2,"dryRun":false,"path":"/Volumes/Server/data2/Games/AIR","engineSlug":"luca-system"}
```

Harness headless com o core atual (`playable` incluído):

```
LAUNCH OK   /Volumes/Server/data2/Games/DDLC playable=true => {"ok":true,"playable":true,"phase":"iterate","message":"Game launched","frames":2,"dryRun":false,"path":"/Volumes/Server/data2/Games/DDLC","engineSlug":"doki-doki-literature-club"}
LAUNCH OK   /Volumes/Server/data2/Games/AIR playable=true => {"ok":true,"playable":true,"phase":"iterate","message":"Game launched","frames":2,"dryRun":false,"path":"/Volumes/Server/data2/Games/AIR","engineSlug":"luca-system"}
```

Validação (sem SDL):

```
[15:44:40] Validate request => launch request validated; graphical launch requires SDL app
```

Capturas da GUI (janela isolada, jogo selecionado):

- `/private/var/folders/m4/75wbzb6924vfj11bz9_43f400000gn/T/opencode/java-ddlc-clean.png` — DDLC selecionado + `Launched OK: DDLC`.
- `/private/var/folders/m4/75wbzb6924vfj11bz9_43f400000gn/T/opencode/java-air-clean.png` — AIR selecionado + `Launched OK: AIR`.
- `/private/var/folders/m4/75wbzb6924vfj11bz9_43f400000gn/T/opencode/java-validate.png` — request validada.

Log completo do harness: `$TMPDIR/kotonoha-java-bridge/selftest.log`.

## Recursos exercitados × resultado

| Recurso (C ABI / JNI) | Resultado literal (trecho) |
|---|---|
| `KotonohaLauncherScanRootsJson` / `nativeScanRootsJson` | `SCAN count=32` (pastas/ISOs aninhados) |
| `KotonohaLauncherDetectPathJson` / `nativeDetectPathJson` | jogo → `engineSlug:"luca-system"` (AIR) |
| `KotonohaLauncherLoadGameParamsJson` | JSON com os 11 campos de parâmetros |
| `KotonohaLauncherSaveGameParams` | `FEATURE SaveGameParams => true` |
| `KotonohaLauncherUpscalePresetsJson` | 15 presets (`index 0..14`) |
| `KotonohaLauncherDefaultSchoolDaysRoute` | vazio no core; fallback para `games/school-days/game-assets/school-days-hq.ktnroute` |
| `KotonohaLauncherValidateRoute` | `OK\|School Days route is valid` |
| `KotonohaLauncherResolveSaveDirectory` | `.../Kotonoha-Profiles/bridge-test-profile/save` |
| `KotonohaLauncherFreeString` | exercitado por `newOwnedString()` em toda string nativa |
| `KotonohaLauncherLaunchGame` / `nativeLaunchJson` | `LAUNCH ok=27 fail=5 total=32` (launch real,Games completos) |

As falhas são dados incompletos nos discos da biblioteca (CMPs sem
`BGMDATA.CMP`/`INI.CMP`/`VOICE.CMP`, SchoolDays sem `--ktrf`), não da ponte.
