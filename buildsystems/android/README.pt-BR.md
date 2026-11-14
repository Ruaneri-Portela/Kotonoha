# Kotonoha Android

[English (US)](README.md) | **Português (Brasil)**

Abra a pasta no Android Studio. Requisitos: **JDK 17**, Android **SDK 36**, **NDK 28.2.13676358**, **CMake 3.22.1**, Git e vcpkg. Utilize o wrapper Gradle.

```sh
export VCPKG_ROOT=/caminho/para/vcpkg
./gradlew :app:assembleExampleDebug
./gradlew :app:assembleDokiDokiDebug -Pabis=arm64-v8a
./gradlew :app:assembleSchoolDaysDebug -Pabis=arm64-v8a
```

Execute dentro de `buildsystems/android/` (PowerShell: defina `$env:VCPKG_ROOT` e use `.\gradlew.bat`). Outros flavors: `Amagami`, `ZeroNoTsukaima`, `SnowRadish`, `SummerRadish`, `RealLive`, `Kirikiri`, `LucaSystem` e `Pmail`.

- APKs ficam em `app/build/outputs/apk/`. ABIs padrão: `arm64-v8a,x86_64`; restrinja com `-Pabis=...`.
- Cada flavor tem seu ID próprio e o CMake compila apenas o alvo nativo correspondente.
- O Gradle usa o submódulo SDL e dependências do vcpkg. DDLC usa CPython por ABI; prefira uma ABI por build.
- Gradle inclui `assets/` do motor e `game-assets/` do jogo; `-PbundleGameAssets=false` exclui os dados comerciais. Resources empacotados são extraídos no início; saves ficam separados.
- Não execute dois Gradles simultaneamente no mesmo diretório nativo `.cxx`.

Para dados externos, overlays, OBB e saves, consulte a [política de diretórios](../../docs/DIRECTORY_POLICY.pt-BR.md) e o [guia Android](../../docs/BUILD_ANDROID.pt-BR.md).
