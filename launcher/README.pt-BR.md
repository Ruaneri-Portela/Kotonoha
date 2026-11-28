# Kotonoha Launcher Unificado Multi-Motor (`KotonohaLauncher`)

[English](README.md)

A pasta `launcher/` fornece o launcher unificado multi-motor e o registro modular de bibliotecas de motores (`kotonoha_unified_launcher` / `KotonohaLauncher`), mantido fora de `games/` como um componente host de nível superior. A pasta é dividida por responsabilidade: `core/` + `bridge/` + `include/` são o `KotonohaLauncherCore` reutilizável; `app/` é o que gera o executável do launcher.

## Estrutura

| Caminho | Contém | Gera |
|---|---|---|
| `core/` | lógica do `KotonohaLauncherCore`: detecção de motores, perfis de save, persistência de parâmetros, validação de rotas e a implementação da C ABI | biblioteca compartilhada / framework Apple / arquivo estático WASM |
| `bridge/` | apenas consumidores da C ABI: `dotnet/`, `js/`, `java/`, `swift/` | linkados pelo frontend correspondente; `java/JavaBridge.cpp` também é compilado no core em plataformas nativas |
| `include/` | headers públicos da C ABI (`KotonohaLauncher/`) | ambos os targets |
| `app/` | executável `KotonohaLauncher`: entrypoint SDL `main.cpp`, hosts nativos Swift/ObjC em `ios/` e `macos/`, recursos de bundle em `Resources/` | app launcher desktop/mobile |
| `tests/` | testes de contrato de código-fonte | `KotonohaUnifiedLauncherTests` |

O Navigator do Xcode espelha essa divisão com um grupo `KotonohaLauncherCore` (`core/`, `bridge/`, `include/`) e um grupo `KotonohaLauncher` (`app/`, `Resources/`, `ios/`, `macos/`). As activities Android permanecem no módulo Gradle em `buildsystems/android/app/src/main/java/`.

## Recursos Principais

1. **Detecção Multi-Motor Kotonoha (`detectKotonohaEngine`)**:
   - Inspeciona pastas, imagens `.iso` e discos PS2 Mode 1 / Mode 2 (`.bin`) via `Kotonoha::Filesystem` VFS V2.
   - Identifica todas as famílias de motores do Kotonoha (LucaSystem, RealLive / rlBabel, KiriKiri2/Z, Ren'Py + patches VNX, N2System, PONScripter/ONScripter, Amagami PS2 SCF, School Days HQ GPK, Pure Mail ADV, Snow Radish ADV, Summer Radish / MEO CMP, Zero no Tsukaima PS2 Squirrel/KAG).
   - Associa automaticamente patches/overlays de tradução (como `DDLC_PT-BR_Patch` -> `DDLC`).

2. **Runtimes vinculados diretamente (`EngineLibraryRegistry`)**:
   - Com `KOTONOHA_BUILD_ALL=ON`, o CMake linka as bibliotecas dos **12 jogos reais** (sem Example) diretamente em `kotonoha_unified_launcher`. O arquivo `GameFactories.cpp` registra construtores concretos de `Runtime` e dos provedores de assets.
   - O `KotonohaLauncher` (`app/main.cpp`) instancia `Runtime` + `Engine` **no mesmo processo**. Ao sair do jogo, libera os recursos, restaura o ambiente de saves e reabre o hub. Não invoca os `main.cpp` nem executáveis secundários.
   - O Example permanece como aplicativo independente e nunca é registrado nem linkado no hub. O launcher não é criado quando se compila um único port.

3. **Divisão da Pasta de Save e Config por Jogo (`splitSavesByGame`)**:
   - Isola `KOTONOHA_USER_DATA_DIR` e `settings.json` em `<SaveRoot>/Kotonoha/Profiles/<saveProfileId>/save` (omitindo o nome do pacote no modo unificado e em mobile/WASM) para que múltiplos títulos do mesmo motor (ex.: `AIR_PT-BR`, `AIR-SE.ISO`, `Clannad_PT-BR`, `KANON-SE.ISO`, `PLANETARIAN.ISO` no RealLive; `SummerRadishVacation`, `SummerRadishVacation2`, `MEO` no CMP; `ZeroNoTsukaima0..2` no Zero) nunca entrem em conflito.

## Compilar com as bibliotecas dos jogos

```sh
# Todas as bibliotecas dos runtimes desktop ligadas diretamente ao hub
cmake -S . -B build -DKOTONOHA_BUILD_ALL=ON
cmake --build build --target KotonohaLauncher

# Compilar um único port NÃO cria o alvo KotonohaLauncher.
# O Example continua independente, fora do launcher.
```

A compilação completa exige as dependências de todos os ports, incluindo Python de desenvolvimento para DDLC quando necessário. O launcher é compilado somente com `KOTONOHA_BUILD_ALL=ON`, em plataformas desktop nativas, e exclui o Example. No macOS, o bundle do hub também inclui a biblioteca padrão do Python para o DDLC.

## Varredura mais rápida da biblioteca

O scanner utiliza um **mount leve e sob demanda, exclusivo da detecção**:
consulta apenas os caminhos necessários para identificar o motor sem indexar
todos os assets e texturas em subpastas. A execução do jogo continua usando o
índice completo do VFS somente-leitura. Resultados reconhecidos e negativos
são armazenados em cache, com fingerprints limitados às pastas/arquivos de
metadados, atualização periódica por hora e gravação uma vez por varredura.

A descoberta examina a pasta selecionada e suas subpastas imediatas/imagens
de disco, não o disco inteiro. Selecione a pasta que contém as pastas de jogos
ou uma pasta de jogo individual.

As sondagens nativas usam um número limitado de workers; WebAssembly usa
varredura serial. A API de callback entrega o catálogo final na thread chamadora,
após combinar overlays, aplicar parâmetros salvos e remover duplicatas.

## Uso via CLI

```bash
# Escanear uma biblioteca completa de jogos e exibir a matriz de motores e perfis de save
KotonohaLauncher --scan-library "/caminho/para/biblioteca-de-jogos"

# Detectar um único jogo (pasta ou imagem de disco)
KotonohaLauncher --detect "/caminho/para/biblioteca-de-jogos/SayaNoUta_PT-BR"

# Abrir o Launcher Unificado ImGui pré-carregado com uma biblioteca
KotonohaLauncher --library "/caminho/para/biblioteca-de-jogos"
```

O aplicativo launcher unificado é compilado em desktop (Linux, macOS, Windows) e mobile (Android, iOS), não em WebAssembly. O núcleo reutilizável, porém, está disponível em todas as plataformas. Detecção de fontes e testes sintéticos não comprovam compatibilidade integral com os jogos originais.

## `KotonohaLauncherCore` e `bridge/`

`KotonohaLauncherCore` é a biblioteca reutilizável que concentra toda a lógica do launcher: detecção de motores, isolamento de perfis de save, persistência de parâmetros, validação de rotas e a C ABI compartilhada em `include/KotonohaLauncher/LauncherBridge.h`.

- **CMake / desktop**: biblioteca **SHARED** (`libKotonohaLauncherCore.dylib` / `.so` / `KotonohaLauncherCore.dll`); o `KotonohaLauncher` linka nela. Emscripten é a única exceção — wasm32 não tem modelo de biblioteca compartilhada via `dlopen`, então o core vira arquivo estático linkado na página do jogo.
- **Xcode (macOS/iOS)**: o target é um **framework** real (`KotonohaLauncherCore.framework`, `com.apple.product-type.framework`) com `DEFINES_MODULE = YES` e o umbrella header `bridge/swift/KotonohaLauncherCore.h`. Os dois apps Apple linkam e embebem o framework com `CodeSignOnCopy`; clientes Swift usam `import KotonohaLauncherCore`.
- **Android**: gerado como `libKotonohaLauncherCore.so`; os exports JNI ficam em `bridge/java/JavaBridge.cpp` e são carregados com `System.loadLibrary("KotonohaLauncherCore")`. Builds desktop exportam os mesmos símbolos JNI do mesmo arquivo (veja abaixo).
- O nome CMake legado `kotonoha_unified_launcher` permanece como alias.

Todos os pontos da C ABI usam `KOTONOHA_LAUNCHER_EXPORT`, garantindo visibilidade padrão na biblioteca compartilhada (ou `__declspec(dllexport)` no Windows).

### Bindings JNI (`KOTONOHA_JNI_BINDINGS`)

`bridge/java/JavaBridge.cpp` é compilado dentro do `KotonohaLauncherCore` em todas as plataformas nativas (macOS, Linux, Windows e Android). No desktop, os headers vêm de `find_package(JNI)` (com `JAVA_HOME` como fallback para instalações JBR/JDK); no Android, `jni.h` vem da NDK. A opção `KOTONOHA_JNI_BINDINGS` tem padrão `ON` quando JNI é encontrado e `OFF` caso contrário, para que um build sem JDK continue funcionando. Use `-DKOTONOHA_ENABLE_JNI=ON` (alias de `KOTONOHA_JNI_BINDINGS`) para forçar os bindings; a configuração então falha com mensagem clara se os headers estiverem ausentes.

O core exporta os 15 símbolos `Java_me_hirameki_*` (7 para `me.hirameki.kotonoha.android.LauncherBridge` e 8 para `me.hirameki.kotonoha.launcher.KotonohaLauncherBridge`), dispensando um shim JNI separado:

```sh
nm -gU libKotonohaLauncherCore.dylib | grep Java_me_hirameki
```

### Pontes

| Diretório | Consumidor | Ponto de entrada |
|---|---|---|
| `bridge/dotnet/` | .NET / P/Invoke | `KotonohaLauncher.cs`, `DllImport("KotonohaLauncherCore")`, `KotonohaGameLaunchRequest`, marshalling UTF-8, `FreeString` |
| `bridge/js/` | Emscripten / navegador | `JsBridge.cpp` (`EMSCRIPTEN_KEEPALIVE`) + módulo ES `bridge.js` (`createLauncherBridge`) com `Module.ccall` / `UTF8ToString` |
| `bridge/java/` | Java / JNI (todas as plataformas nativas) | `JavaBridge.cpp` + `KotonohaLauncherBridge.java` (pacote `me.hirameki.kotonoha.launcher`) |
| `bridge/swift/` | Swift / Xcode | `KotonohaLauncherCore.swift` (`LaunchRequest`, `scanRoots`, variante async), header umbrella `KotonohaLauncherCore.h`, `module.modulemap` |

Cada ponte chama a mesma C ABI; nenhuma duplica lógica do launcher. Strings alocadas pelo C devem ser liberadas com `KotonohaLauncherFreeString` (as facades já fazem isso).

**Nota sobre o namespace JNI:** o app distribuído usa `me.hirameki.kotonoha.android` e seus símbolos JNI são `Java_me_hirameki_kotonoha_android_LauncherBridge_*`. A ponte reutilizável exporta `Java_me_hirameki_kotonoha_launcher_KotonohaLauncherBridge_*`. Os dois conjuntos também são exportados pelo core no desktop; não restam símbolos `org.kotonoha.android`.

O gerenciador desktop Avalonia fica em
[`desktop/`](desktop/README.pt-BR.md) e usa a
mesma ponte e os mesmos tokens de design.

## Design system unificado e paridade de recursos

`design/README.md` e `design/tokens.json` são a fonte da verdade. Todas as
quatro superfícies nativas usam a mesma derivação **Soft** (fundo slate
`#1B2740`, superfície elevada `#24304B`, acento lilás `#A79BD4`, texto muted
`#A6B2C8`, raios 8/12/20 dp e espaçamentos 4/8/16/24 dp). O ImGui define os
valores em `lib/src/debug/ImGuiTheme.cpp`; o SwiftUI de macOS/iOS repete em
`KotonohaBrand` / `KotonohaMetrics`; o Android Material 3 usa
`res/values/colors.xml`, `themes.xml` e `styles.xml`. A paleta oficial v2
permanece intacta nos assets de marca; apenas as superfícies de UI foram
suavizadas.

| Recurso (C ABI / equivalente nativo) | ImGui (desktop) | SwiftUI macOS | SwiftUI iOS | Android Material 3 |
| --- | --- | --- | --- | --- |
| Adicionar pasta/biblioteca e imagem de disco | Library / Folder… + Disc Image… | Add Folder/Library + Add ISO/BIN (NSOpenPanel) | Add Folder/Library + Add ISO/BIN (Files.app) + ZIP | Add Folder/Library (SAF) + Add ISO/BIN + ZIP |
| `KotonohaLauncherScanRootsJson` | caminho da biblioteca + Rescan | `addRoots` + rescan | roots com bookmark + rescan | roots SAF + rescan |
| `KotonohaLauncherDetectPathJson` | tabela de mecanismos detectados / `--detect` | Detect Folder… / Detect Disc/File… | Detect Folder / Disc… | Detect folder / disc |
| `KotonohaLauncherLoadGameParamsJson` | mapa de parâmetros salvos | `params` do JSON do catálogo | `params` do JSON do catálogo | `params` do JSON do catálogo |
| `KotonohaLauncherSaveGameParams` | seção Parameters, salva ao jogar | Parameters → Save | Parameters → Save | Parameters → Save |
| `KotonohaLauncherUpscalePresetsJson` | Upscaling → Shader (OpenGL) | seletor Shader Preset | seletor Shader Preset | dropdown Shader Preset |
| `KotonohaLauncherValidateRoute` | status abaixo de `--ktrf` | mensagem de validação | mensagem de validação | status abaixo de `--ktrf` |
| `KotonohaLauncherDefaultSchoolDaysRoute` | exibido abaixo da rota | exibido em Parameters | usado como placeholder | exibido em Parameters |
| `KotonohaLauncherResolveSaveDirectory` | linha Isolated Save Directory | diretório resolvido + save root customizado | diretório resolvido + save root customizado | save root + diretório de save em Parameters |
| `KotonohaLauncherFreeString` | interno nos wrappers da C ABI | interno nas facades da store | interno nas facades da store | interno nas facades JNI |
| Gráficos, upscaling e overrides por jogo | abas + parâmetros opcionais | sheets Parameters/Settings | sheets Parameters/Settings | sheets de gráficos e parâmetros |

`FreeString` não tem controle de UI direto por projeto: é o contraparte de
gerenciamento de memória dos pontos que retornam JSON e é chamado por todas as
facades.
