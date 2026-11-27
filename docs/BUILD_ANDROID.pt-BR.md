# Compilação Android

[English (US)](BUILD_ANDROID.md) | **Português (Brasil)**

Android usa `buildsystems/android/`, com uma variante Gradle por jogo. Requisitos: Android Studio (ou ferramentas SDK), **JDK 17**, **SDK 36**, **NDK 30.0.16248370**, **CMake 3.22.1**, Git e vcpkg.

```sh
export VCPKG_ROOT=/caminho/para/vcpkg
cd buildsystems/android
./gradlew :app:assembleExampleDebug
./gradlew :app:assembleRealLiveDebug -Pabis=armeabi-v7a
```

No Windows, use `gradlew.bat` e defina `VCPKG_ROOT` no ambiente. O Gradle obtém o SDK pelo Android Studio/`local.properties`. As ABIs padrão são `arm64-v8a,armeabi-v7a,x86_64`. Selecione ARMv7 com NEON usando `-Pabis=armeabi-v7a`, ARM64 com `-Pabis=arm64-v8a` ou ambas as ABIs ARM com `-Pabis=arm64-v8a,armeabi-v7a`. O NDK usa `ANDROID_ARM_NEON=ON`, e o vcpkg usa `arm-neon-android` para ARMv7; o dispositivo de 32 bits precisa suportar NEON.

Exemplos de flavors: `Launcher` (Hub Unificado), `Example`, `Amagami`, `ZeroNoTsukaima`, `SnowRadish`, `SummerRadish`, `DokiDoki`, `SchoolDays`, `RealLive`, `Kirikiri`, `LucaSystem`, `Pmail`, `Ponscripter` e `N2System`. Instale com `:app:install<Flavor>Debug`.

Por padrão, o Gradle **não inclui** `game-assets` comerciais (o Example continua incluído). Para testes privados com arquivos cuja utilização seja autorizada, passe explicitamente `-PbundleGameAssets=true`; a variante `dist` sempre exclui dados comerciais. Não redistribua dados proprietários sem permissão nem os versione. Consulte os [detalhes do projeto Android](../buildsystems/android/README.pt-BR.md).
