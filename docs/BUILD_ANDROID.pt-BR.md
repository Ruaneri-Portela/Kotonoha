# Compilação Android

[English (US)](BUILD_ANDROID.md) | **Português (Brasil)**

Android usa `buildsystems/android/`, com uma variante Gradle por jogo. Requisitos: Android Studio (ou ferramentas SDK), **JDK 17**, **SDK 36**, **NDK 28.2.13676358**, **CMake 3.22.1**, Git e vcpkg.

```sh
export VCPKG_ROOT=/caminho/para/vcpkg
cd buildsystems/android
./gradlew :app:assembleExampleDebug
./gradlew :app:assembleRealLiveDebug -Pabis=armeabi-v7a
```

No Windows, use `gradlew.bat` e defina `VCPKG_ROOT` no ambiente. O Gradle obtém o SDK pelo Android Studio/`local.properties`. As ABIs padrão são `arm64-v8a,armeabi-v7a,x86_64`. Selecione ARMv7 com NEON usando `-Pabis=armeabi-v7a`, ARM64 com `-Pabis=arm64-v8a` ou ambas as ABIs ARM com `-Pabis=arm64-v8a,armeabi-v7a`. O NDK usa `ANDROID_ARM_NEON=ON`, e o vcpkg usa `arm-neon-android` para ARMv7; o dispositivo de 32 bits precisa suportar NEON.

Exemplos de flavors: `Example`, `Amagami`, `ZeroNoTsukaima`, `SnowRadish`, `SummerRadish`, `DokiDoki`, `SchoolDays`, `RealLive`, `Kirikiri`, `LucaSystem` e `Pmail`. Instale com `:app:install<Flavor>Debug`.

Por padrão, o Gradle inclui `game-assets` da variante. Use `-PbundleGameAssets=false` para dados externos (o exemplo continua incluído). Não versionar assets licenciados. Consulte os [detalhes do projeto Android](../buildsystems/android/README.pt-BR.md).
