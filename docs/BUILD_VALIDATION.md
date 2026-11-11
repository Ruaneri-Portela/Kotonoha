# Builds locais — 4 de novembro de 2026

As builds desta rodada usam a configuração Debug. Os relatórios completos
ficam em `.research/build-validation/`; essa pasta e os binários gerados são
locais e não fazem parte do código versionado.

A rodada final, após os ajustes de interface de Amagami e de entrada,
concluiu sem erros:

| Plataforma | Build final | Artefatos verificados |
| --- | --- | --- |
| Android | Oito jogos, ARM64 e x86_64 | Nove APKs; assinaturas e ABIs verificadas |
| iOS Simulator | Oito schemes, ARM64 | Oito apps; arquitetura e plataforma verificadas |
| iOS Device | Oito schemes, ARM64, sem assinatura | Oito apps; arquitetura e plataforma verificadas |

Os resultados por alvo estão em `finish-android-results.json` e
`finish-ios-results.json`, dentro de `.research/build-validation/`.
`finish-artifacts-verified.json` registra a conferência dos 25 artefatos;
`finish-android-artifacts.json` inclui os hashes SHA-256 dos APKs.

## Android

Os oito sabores são `example`, `amagami`, `zeroNoTsukaima`, `snowRadish`,
`dokiDoki`, `schoolDays`, `reallive` e `summerRadish`. Os APKs consolidados ficam
em `build/artifacts/android/`. Sete sabores incluem ARM64 e x86_64 no mesmo
APK; DDLC tem um APK separado para cada ABI, identificado no nome do arquivo.
Nesta validação foi usado `-PbundleGameAssets=false`.

Execute os comandos a partir de `buildsystems/android/`, com `JAVA_HOME`, `VCPKG_ROOT` e
`ANDROID_HOME` configurados conforme o [README Android](../buildsystems/android/README.md):

```sh
./gradlew :app:assembleExampleDebug :app:assembleAmagamiDebug \
  :app:assembleZeroNoTsukaimaDebug :app:assembleSnowRadishDebug \
  :app:assembleSchoolDaysDebug :app:assembleRealLiveDebug \
  :app:assembleSummerRadishDebug \
  -Pabis=arm64-v8a,x86_64 -PbundleGameAssets=false
./gradlew :app:assembleDokiDokiDebug -Pabis=arm64-v8a -PbundleGameAssets=false
./gradlew :app:assembleDokiDokiDebug -Pabis=x86_64 -PbundleGameAssets=false
```

Execute as invocações em sequência. Copie o APK de DDLC após cada comando,
pois as duas ABIs usam o mesmo caminho de saída do Gradle.

## iOS

Os oito schemes de aplicativos são `Kotonoha`, `Amagami`, `ZeroNoTsukaima`,
`SnowRadish`, `DokiDoki`, `SchoolDays`, `RealLive` e `SummerRadish`. A matriz local
compila cada scheme para ARM64 no simulador e em dispositivo, com Xcode 27.2.
As dependências vcpkg e o SDK Python de DDLC devem ser preparados antes,
conforme o [README principal](../README.md#native-ios-xcode-project).

```sh
xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj -scheme Amagami \
  -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' \
  -derivedDataPath build/xcode-ios ARCHS=arm64 CODE_SIGNING_ALLOWED=NO build
xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj -scheme Amagami \
  -sdk iphoneos -destination 'generic/platform=iOS' \
  -derivedDataPath build/xcode-ios ARCHS=arm64 CODE_SIGNING_ALLOWED=NO build
```

Os `.app` ficam em `build/xcode-ios/Build/Products/Debug-iphonesimulator/`
e `build/xcode-ios/Build/Products/Debug-iphoneos/`. As builds de dispositivo
desta validação não estão assinadas; instalar em um iPhone exige configurar
a equipe e o provisionamento no Xcode. Não foi gerado um IPA assinado.

O exemplo foi instalado e iniciado no simulador iPhone 18 Pro com iOS 27.2;
o menu nativo renderizou a aproximadamente 59 FPS. A captura está em
`.research/build-validation/ios-example.png`. Essa execução confirma a
inicialização e renderização, sem substituir testes de toque ou controle
em hardware real.

O target Amagami foi corrigido para usar suas próprias fontes, nome de
produto e bundle ID `me.hirameki.kotonoha.amagami`. Os demais IDs existentes
foram preservados.


## macOS e testes

Os oito aplicativos Debug foram compilados no macOS ARM64 com
`cmake --build build -j 6`. Os bundles ficam em `build/games/`, no diretório
de cada jogo; Amagami está em `build/games/amagami/Amagami.app` e ZNT em
`build/games/zero-no-tsukaima/ZeroNoTsukaima.app`. O empacotamento verificou
as dependências e aplicou assinatura ad hoc local.

A rodada completa de 66 testes encontrou cinco falhas, corrigidas e verificadas
por novas execuções dos respectivos testes: Runtime/Media (teclado do diálogo
e inspetor), Demo (fluxo de nome do save), ZNT Engine (confirmação do splash)
e School Days Launcher (busca de assets). Dois testes de RealLive foram ignorados
por falta da configuração de dados de integração. Não foi feita uma comparação
completa de todos os jogos contra seus originais.

Após os últimos ajustes, `Amagami.Engine`, `Amagami.Translation` e
`Amagami.Backlog` passaram (3/3). `ZeroNoTsukaima.Engine` e `Kotonoha.Demo`
também passaram (2/2), incluindo os novos testes de controle e diálogo nativo.
As capturas traduzidas do título e do menu de Amagami foram conferidas em
`.research/build-validation/amagami-ui/`.

Logs principais: `macos-complete.log`, `macos-tests-final.log`,
`input-regression-tests2.log`, `amagami-ui-tests.log` e
`input-validation-final.log`, em `.research/build-validation/`.
