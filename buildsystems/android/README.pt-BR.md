# Kotonoha Android

[English (US)](README.md) | **Português (Brasil)**

Abra a pasta no Android Studio. Requisitos: **JDK 17**, Android **SDK 36**, **NDK 30.0.16248370**, **CMake 3.22.1**, Git e vcpkg. Utilize o wrapper Gradle.

```sh
export VCPKG_ROOT=/caminho/para/vcpkg
./gradlew :app:assembleExampleDebug
./gradlew :app:assembleDokiDokiDebug -Pabis=arm64-v8a
./gradlew :app:assembleSchoolDaysDebug -Pabis=armeabi-v7a -PbundleGameAssets=false
```

Execute dentro de `buildsystems/android/` (PowerShell: defina `$env:VCPKG_ROOT` e use `.\gradlew.bat`). Todos os 17 flavors: `Launcher` (hub unificado), `Example`, `Amagami`, `ZeroNoTsukaima`, `SnowRadish`, `SummerRadish`, `MEO`, `KatawaShoujo`, `DokiDoki`, `SchoolDays`, `RealLive`, `Kirikiri`, `LucaSystem`, `Pmail`, `Ponscripter`, `N2System` e `Artemis`.

- APKs ficam em `app/build/outputs/apk/`. ABIs padrão: `arm64-v8a,armeabi-v7a,x86_64`; compile só ARMv7 com `-Pabis=armeabi-v7a`. O ARMv7 exige NEON (`ANDROID_ARM_NEON=ON`, triplet vcpkg `arm-neon-android`), portanto o dispositivo 32 bits deve ter suporte a NEON.
- Cada flavor tem seu ID próprio e o CMake compila apenas o alvo nativo correspondente (`KotonohaLauncherCore` no flavor `Launcher`).
- O Gradle usa o submódulo SDL e dependências do vcpkg. DDLC usa CPython por ABI (`armeabi-v7a` utiliza `arm-neon-android`); prefira uma ABI por build.
- Gradle inclui `assets/` do motor, mas **exclui `game-assets/` comerciais por padrão**; o Example continua com assets próprios. `-PbundleGameAssets=true` é exclusivo para testes privados autorizados e a variante Android `dist` sempre exclui esses dados. Os recursos são extraídos no início; saves ficam separados.
- Dados de usuário e saves ficam isolados em `<userData>/Kotonoha/<app-id>/` no desktop e dispositivos móveis (`Android/data/<app-id>/files/Kotonoha/<app-id>/` no Android), enquanto o `UnifiedLauncher` isola os perfis por slug de jogo.
- Importações de pastas e ZIP no Android utilizam cópia robusta de diretórios e persistência de permissão SAF.
- O School Days sempre carrega seus 29 GPKs retail externos via SAF;
  -PbundleGameAssets não os incorpora. -PincludeLauncherArt=false desliga
  a cópia da arte opcional do launcher na compilação privada.

Para dados externos, overlays, OBB e saves, consulte a [política de diretórios](../../docs/DIRECTORY_POLICY.pt-BR.md) e o [guia Android](../../docs/BUILD_ANDROID.pt-BR.md).
