# Kotonoha Android

[English (US)](README.md) | **Português (Brasil)**

Abra a pasta no Android Studio. Requisitos: **JDK 17**, Android **SDK 36**, **NDK 28.2.13676358**, **CMake 3.22.1**, Git e vcpkg. Utilize o wrapper Gradle.

```sh
export VCPKG_ROOT=/caminho/para/vcpkg
./gradlew :app:assembleExampleDebug
./gradlew :app:assembleDokiDokiDebug -Pabis=arm64-v8a
./gradlew :app:assembleSchoolDaysDebug -Pabis=armeabi-v7a
```

Execute dentro de `buildsystems/android/` (PowerShell: defina `$env:VCPKG_ROOT` e use `.\gradlew.bat`). Outros flavors: `Amagami`, `ZeroNoTsukaima`, `SnowRadish`, `SummerRadish`, `RealLive`, `Kirikiri`, `LucaSystem` e `Pmail`.

- APKs ficam em `app/build/outputs/apk/`. ABIs padrão: `arm64-v8a,armeabi-v7a,x86_64`; compile só ARMv7 com `-Pabis=armeabi-v7a`. O ARMv7 exige NEON (`ANDROID_ARM_NEON=ON`, triplet vcpkg `arm-neon-android`), portanto o dispositivo 32 bits deve ter suporte a NEON.
- Cada flavor tem seu ID próprio e o CMake compila apenas o alvo nativo correspondente.
- O Gradle usa o submódulo SDL e dependências do vcpkg. DDLC usa CPython por ABI (`armeabi-v7a` utiliza `arm-neon-android`); prefira uma ABI por build.
- Gradle inclui `assets/` do motor, mas **exclui `game-assets/` comerciais por padrão**; o Example continua com assets próprios. `-PbundleGameAssets=true` é exclusivo para testes privados autorizados e a variante Android `dist` sempre exclui esses dados. Os recursos são extraídos no início; saves ficam separados.
- No Android, iOS e WebAssembly (WASM), dados de usuário e saves são armazenados diretamente em `pacote/**` (ignorando o nome do pacote/bundle), enquanto builds desktop standalone usam `Kotonoha/pacote/**`. No modo unificado (`UnifiedLauncher`), o nome do pacote é omitido para os saves.
- Importações de pastas e ZIP no Android utilizam cópia robusta de diretórios e persistência de permissão SAF.

Para dados externos, overlays, OBB e saves, consulte a [política de diretórios](../../docs/DIRECTORY_POLICY.pt-BR.md) e o [guia Android](../../docs/BUILD_ANDROID.pt-BR.md).
