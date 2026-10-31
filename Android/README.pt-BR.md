# Kotonoha Android

[English](README.md) | Portugues (Brasil)

Abra esta pasta no Android Studio. Requisitos: JDK 17 ou mais recente
compativel com Gradle 8.14.5 (o JDK do Android Studio funciona), SDK 36,
CMake 3.22.1, NDK 28.2.13676358, Git e um checkout vcpkg.

```sh
export VCPKG_ROOT=/caminho/para/vcpkg
export ANDROID_HOME=/caminho/para/Android/sdk
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/28.2.13676358"
export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
./gradlew :app:assembleExampleDebug
./gradlew :app:assembleZeroNoTsukaimaDebug
./gradlew :app:assembleSnowRadishDebug
./gradlew :app:assembleDokiDokiDebug
./gradlew :app:assembleSchoolDaysDebug
```

Cada flavor tem applicationId e APK proprios em `app/build/outputs/apk/`.
A ABI padrao e `arm64-v8a`; use `-Pabis=x86_64` no emulador.
Execute as invocacoes Gradle sequencialmente: builds simultaneos compartilham
os diretorios nativos `.cxx`. Uma invocacao pode receber varios targets assemble.
Compile DDLC com uma ABI por APK. As dependencias ficam em `build/vcpkg`;
DDLC habilita o recurso `android-python` do manifesto. Nenhuma biblioteca
Python macOS/iOS e usada no Android.

SDL e dependencia remota do Git, fixada na revisao
`4bb5020be2a3e3f7a963b84c95f49fe7ef5d35a4` em [app/build.gradle](app/build.gradle).
`prepareSDL` obtem o repositorio oficial em `.deps/SDL`, ignorado pelo Git.
Os fontes Java SDLActivity e a biblioteca nativa usam o mesmo checkout.
SDL_ttf e ImGui continuam nos submodulos; FFmpeg, json-c, libass, iconv, PlutoSVG e
CPython opcional sao fornecidos pelo vcpkg.
DDLC usa uma instalacao separada em `build/vcpkg-python` para que configurar
outros jogos nao remova sua dependencia opcional de CPython.

Cada APK inclui `assets/` do motor e `game-assets/` do jogo escolhido.
Gradle prepara os recursos e calcula sua identidade pelo conteudo.
BootstrapActivity extrai o APK em uma thread de trabalho antes de iniciar
SDL e mostra erros de preparo. `-PbundleGameAssets=false` exclui dados
comerciais locais. Forneca dados obtidos legitimamente em
`../games/<jogo>/game-assets/`; nao os coloque no Git.

Os dados preferidos ficam em `Android/obb/<applicationId>` e os documentos
em `Android/data/<applicationId>/files`, obtidos pelas APIs da plataforma.
Coloque dados externos em `OBB/game-assets/` ou diretamente na pasta OBB;
os jogos procuram esses caminhos antes dos dados empacotados. Configuracoes,
saves e estado persistente usam os documentos.
O armazenamento com escopo pode limitar acesso de gerenciadores de arquivos;
nao solicitamos acesso amplo nem criamos uma pasta artificial `Android/obb-data`.

Os recursos extraidos sao privados e identificados pelo conteudo do APK.
Versoes antigas permanecem privadas; desinstalar remove esses arquivos.
Dialogos nativos podem estar indisponiveis no Android; o inspetor MPV tambem
oferece selecao de shaders incluidos no pacote.
