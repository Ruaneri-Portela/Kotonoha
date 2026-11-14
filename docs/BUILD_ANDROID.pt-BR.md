# Compilação Android

[English (US)](BUILD_ANDROID.md) | **Português (Brasil)**

Android usa `buildsystems/android/`, com uma variante Gradle por jogo. Requisitos: Android Studio (ou ferramentas SDK), **JDK 17**, **SDK 36**, **NDK 28.2.13676358**, **CMake 3.22.1**, Git e vcpkg.

```sh
export VCPKG_ROOT=/caminho/para/vcpkg
cd buildsystems/android
./gradlew :app:assembleExampleDebug
./gradlew :app:assembleRealLiveDebug -Pabis=arm64-v8a
```

No Windows, use `gradlew.bat` e defina `VCPKG_ROOT` no ambiente. O Gradle obtém o SDK pelo Android Studio/`local.properties`. As ABIs padrão são `arm64-v8a,x86_64`; restrinja com `-Pabis=arm64-v8a`.

Exemplos de flavors: `Example`, `Amagami`, `ZeroNoTsukaima`, `SnowRadish`, `SummerRadish`, `DokiDoki`, `SchoolDays`, `RealLive`, `Kirikiri`, `LucaSystem` e `Pmail`. Instale com `:app:install<Flavor>Debug`.

Por padrão, o Gradle inclui `game-assets` da variante. Use `-PbundleGameAssets=false` para dados externos (o exemplo continua incluído). Não versionar assets licenciados. Consulte os [detalhes do projeto Android](../buildsystems/android/README.pt-BR.md).
