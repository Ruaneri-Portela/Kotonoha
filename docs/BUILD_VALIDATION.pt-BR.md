# Validação de build

[English (US)](BUILD_VALIDATION.md) | **Português (Brasil)**

Após mudar o CMake ou o projeto Xcode, verifique os metadados:

```sh
python3 tools/ValidateBuildMetadata.py
python3 tools/check_xcode_sources.py
python3 tools/check_visual_studio_sources.py
```

`check_visual_studio_sources.py` verifica a solução independente
`buildsystems/visualstudio/Kotonoha.sln`: 75 projetos (16 jogos, 39 módulos,
1 DLL do motor e 3 projetos do launcher), cobertura das fontes em `lib/`, todos
os diretórios de módulos, os 16 jogos e o launcher, além das seis combinações
Debug/Release × Win32/x64/ARM64.

A verificação, que não modifica arquivos, confere **16 aplicações de jogo no CMake** (15 ports de jogos mais o Example), **34 schemes Xcode** (16 jogos mais `KotonohaLauncher` para macOS e iOS), ausência de pendências `xcode_pending` sem registro, IDs dos alvos, preparação de dependências e includes `.xcconfig`. O Android possui 16 flavors de jogos e um flavor `launcher` separado (`KOTONOHA_BUILD_GAME=all`); execute `python3 tools/check_release_matrix.py` para validar ambas as matrizes. Exige apenas Python 3 e não compila código.

Para testar a compilação nativa, siga os [guias de build](BUILD_GUIDES.pt-BR.md), usando SDKs/vcpkg e dados locais quando necessários. A verificação de metadados não substitui build, testes nem validação de empacotamento.


## Comandos para conferir a compilação

Execute na raiz do repositório após instalar os SDKs, dependências do vcpkg
e submódulos Git descritos nos guias de cada plataforma:

```sh
# CMake nativo: todos os ports e o launcher no mesmo processo
cmake -S . -B build -DKOTONOHA_BUILD_ALL=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# Android Studio / Gradle: flavor dedicado ao launcher
cd buildsystems/android
./gradlew :app:assembleLauncherDebug -Pabis=arm64-v8a
cd ../..

# Xcode: launchers nativos macOS e iOS Simulator
xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj \
  -scheme 'KotonohaLauncher (macOS)' -configuration Debug build
xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj \
  -scheme 'KotonohaLauncher (iOS)' -configuration Debug \
  -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' build
```

Esses comandos são um roteiro de validação, **não uma comprovação de que
todos os builds passaram**. Xcode exige SDK Apple e frameworks SDL preparados
para o destino correto; Gradle exige SDK/NDK e dependências Android do vcpkg.
A matriz de fontes não substitui compilar nem inspecionar os APKs. Os testes
de regressão do scanner são `Kotonoha.FilesystemV2` e
`Kotonoha.UnifiedLauncher`.
