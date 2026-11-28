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

### Windows: divergência no nome do build-python do vcpkg

O port `python3` fixado pelo vcpkg compila um CPython host em
`buildsystems/android/build/vcpkg-python/<abi>/x64-windows/tools/python3/`, mas a
etapa `configure` do Android passa `--with-build-python=.../python3.14`. No
Windows o port instala `python.exe` (e `python314.dll`), não `python3.14`, e o
primeiro build falha com `invalid or missing build python binary`. Após essa
primeira tentativa, crie o alias e rode o build novamente:

```powershell
Get-ChildItem buildsystems/android/build/vcpkg-python -Directory | ForEach-Object {
  $p = Join-Path $_.FullName 'x64-windows/tools/python3'
  if (Test-Path "$p/python.exe") { Copy-Item "$p/python.exe" "$p/python3.14.exe" -Force }
}
```

O alias fica na árvore de build ignorada pelo Git; recrie-o após remover
`build/vcpkg-python/<abi>`.

Um segundo problema upstream do CPython pode aparecer no `make altinstall`:
`Lib/sysconfig/__main__.py` procura `PC/pyconfig.h` no diretório de build
cruzado, enquanto o CPython grava `pyconfig.h` na raiz desse diretório. Copiar o
arquivo para `<abi-build-dir>/PC/pyconfig.h` satisfaz o configure, mas o vcpkg
reextrai as fontes a cada nova tentativa, então a cópia precisa ser repetida
após uma falha. Até o port fixado do vcpkg ser corrigido, flavors que exigem
CPython (`DokiDoki`, `KatawaShoujo`, `Launcher`) podem continuar falhando em
host Windows; flavors sem Python, como `Example`, compilam para todas as ABIs
com `:app:assembleExampleDebug`.

Todos os 17 flavors: `Launcher` (Hub Unificado), `Example`, `Amagami`, `ZeroNoTsukaima`, `SnowRadish`, `SummerRadish`, `MEO`, `KatawaShoujo`, `DokiDoki`, `SchoolDays`, `RealLive`, `Kirikiri`, `LucaSystem`, `Pmail`, `Ponscripter`, `N2System` e `Artemis`. Instale com `:app:install<Flavor>Debug`.

Por padrão, o Gradle **não inclui** `game-assets` comerciais (o Example continua incluído). Para testes privados com arquivos cuja utilização seja autorizada, passe explicitamente `-PbundleGameAssets=true`; a variante `dist` sempre exclui dados comerciais. Não redistribua dados proprietários sem permissão nem os versione. Consulte os [detalhes do projeto Android](../buildsystems/android/README.pt-BR.md).
