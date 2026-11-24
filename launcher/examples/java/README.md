# Exemplo de integração JNI (JavaFX)

App de teste manual da ponte Java/JNI de `KotonohaLauncherCore`. Fica em
`launcher/examples/java/` — **não** é compilado pelo CMake/CTest; é um exemplo
de integração executado à mão. Nenhum asset comercial de jogo é incluído.

## Por que não há mais shim JNI

`launcher/bridge/java/JavaBridge.cpp` é compilado para o core em **todas** as
plataformas (macOS/Linux/Windows via `KOTONOHA_JNI_BINDINGS`, Android via NDK).
O próprio `libKotonohaLauncherCore.dylib` exporta os símbolos
`Java_me_hirameki_kotonoha_launcher_KotonohaLauncherBridge_*`. O app carrega o
core diretamente com `System.load(...)`; não há cópia de `JavaBridge.cpp`, nem
`libKotonohaJniBridge.dylib`.

`NativeLibraries.java` pré-carrega as dependências `@rpath` por caminho
absoluto antes do core (a JVM ignora `DYLD_LIBRARY_PATH` com hardened runtime).

## Estrutura

```
launcher/examples/java/
├── build.sh                 # compila as classes; baixa os jars JavaFX se faltarem
├── run.sh                   # GUI JavaFX
├── run-cli.sh               # harness headless (todos os recursos + launch por jogo)
├── lib/                     # jars JavaFX (ignorados pelo git)
├── out/                     # classes .class (ignoradas pelo git)
└── src/me/hirameki/kotonoha/
    ├── launcher/
    │   ├── KotonohaLauncherBridge.java  # facade JNI (cópia adaptada do repo)
    │   └── NativeLibraries.java         # resolve e carrega a nativa
    └── bridgetest/
        ├── BridgeTestApp.java           # GUI: Add folder, Scan, Detect, Launch all
        ├── BridgeTestCli.java           # harness headless
        └── JsonUtil.java                # parse minimal do JSON
```

## Pré-requisitos

- JDK 21+ (`/usr/libexec/java_home -v 21`) e JavaFX 21.0.2 para `mac-aarch64`
  (baixado do Maven Central para `lib/`, sem Maven/Gradle).
- Core construído:
  `cmake --build out/build/macos-arm64-vcpkg --target KotonohaLauncherCore`.

## Build e execução

```sh
cd launcher/examples/java
./build.sh
./run-cli.sh                                   # varre /Volumes/Server/data2/Games
./run-cli.sh /Volumes/Server/data2/Games/DDLC  # roots explícitos
./run-cli.sh --dry-run                         # valida requests sem iniciar SDL
./run.sh --auto-scan                           # GUI
```

`KOTONOHA_REPO` (opcional) aponta para a raiz do repo; o padrão é derivado da
posição do script (`launcher/examples/java/../../..`).
`KOTONOHA_LAUNCHER_LAUNCH_FRAMES` controla quantos frames cada jogo roda
(padrão: 1). O working directory é trocado para `out/build/.../bin` para que o
motor encontre `assets/fonts`.

## Recursos exercitados × resultado

Executado em 2026-11-24 com o core Release arm64 e
`./run-cli.sh /Volumes/Server/data2/Games` (`KOTONOHA_LAUNCHER_LAUNCH_FRAMES=1`,
 SDL dummy automático no core):

| Recurso (C ABI / JNI) | Resultado literal (trecho) |
|---|---|
| `KotonohaLauncherScanRootsJson` / `nativeScanRootsJson` | `SCAN count=32` (pasta com subpastas e ISOs aninhados) |
| `KotonohaLauncherDetectPathJson` / `nativeDetectPathJson` | raiz → `{}`; jogo → `engineSlug:"luca-system"` |
| `KotonohaLauncherLoadGameParamsJson` | JSON com os 11 campos de parâmetros |
| `KotonohaLauncherSaveGameParams` | `FEATURE SaveGameParams => true` |
| `KotonohaLauncherUpscalePresetsJson` | 15 presets (`index 0..14`) |
| `KotonohaLauncherDefaultSchoolDaysRoute` | vazio no core; fallback para `games/school-days/game-assets/school-days-hq.ktnroute` |
| `KotonohaLauncherValidateRoute` | `OK\|School Days route is valid` |
| `KotonohaLauncherResolveSaveDirectory` | `.../Kotonoha-Profiles/bridge-test-profile/save` |
| `KotonohaLauncherFreeString` | exercitado por `newOwnedString()` em toda string nativa |
| `KotonohaLauncherLaunchGame` / `nativeLaunchJson` | **`LAUNCH ok=27 fail=5 total=32`** (launch real, 1 frame/jogo) |

As 5 falhas são dados incompletos nos discos da biblioteca, não falhas da
ponte: 4 imagens CMP sem `BGMDATA.CMP`/`INI.CMP`/`VOICE.CMP` e `SchoolDays`
sem `--ktrf` configurado. Exemplo de sucesso literal:

```
LAUNCH OK   /Volumes/Server/data2/Games/DDLC => {"ok":true,"phase":"iterate","message":"Game launched","frames":1,"dryRun":false,"path":"/Volumes/Server/data2/Games/DDLC","engineSlug":"doki-doki-literature-club"}
```

Log completo: `$TMPDIR/kotonoha-java-bridge/selftest.log`.

## Dry-run

Com `--dry-run`, o core responde
`{"ok":true,"phase":"validate","dryRun":true,...}` para cada jogo: útil para
validar todos os requests sem abrir janelas SDL.
