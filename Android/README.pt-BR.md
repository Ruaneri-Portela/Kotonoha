# Kotonoha Android

[English](README.md) | Portugues (Brasil)

Abra esta pasta no Android Studio. Requisitos: JDK 17 ou mais recente
compativel com Gradle 8.14.6 (o JDK do Android Studio funciona), SDK 36,
CMake 3.22.1, NDK 28.2.13676358, Git e um checkout vcpkg.

```sh
export VCPKG_ROOT=/caminho/para/vcpkg
export ANDROID_HOME=/caminho/para/Android/sdk
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/28.2.13676358"
export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
./gradlew :app:assembleExampleDebug
./gradlew :app:assembleAmagamiDebug
./gradlew :app:assembleZeroNoTsukaimaDebug
./gradlew :app:assembleSnowRadishDebug
./gradlew :app:assembleDokiDokiDebug -Pabis=arm64-v8a
./gradlew :app:assembleSchoolDaysDebug
./gradlew :app:assembleRealLiveDebug
./gradlew :app:assembleSummerRadishDebug
```

No Windows, use PowerShell na pasta `Android`:

```powershell
$env:VCPKG_ROOT = 'C:\caminho\para\vcpkg'
$env:JAVA_HOME = 'C:\Program Files\Android\Android Studio\jbr'
.\gradlew.bat :app:assembleExampleDebug
```

O vcpkg instalado com o Visual Studio tambem pode ser usado. Ao abrir o
Android Studio pelo menu Iniciar, configure `VCPKG_ROOT` nas variaveis de
ambiente do usuario e reinicie o Studio. O SDK vem de `local.properties`
(gerado pelo Studio); o NDK selecionado pelo Gradle e repassado ao vcpkg.
Nao e necessario definir `ANDROID_NDK_HOME` separadamente.

Cada flavor tem applicationId e APK proprios em `app/build/outputs/apk/`.
As ABIs padrão são `arm64-v8a` e `x86_64`; use `-Pabis=x86_64` para compilar somente para o emulador.
Execute as invocacoes Gradle sequencialmente: builds simultaneos compartilham
os diretorios nativos `.cxx`. Uma invocacao pode receber varios targets assemble.
Compile DDLC com uma ABI por APK. As dependencias ficam em `build/vcpkg/<ABI>`; DDLC usa CPython fornecido pelo vcpkg. Nenhuma biblioteca Python macOS/iOS e usada no Android.

Os fontes Java SDLActivity e a biblioteca nativa SDL usam o mesmo checkout
em `lib/submodules/SDL`.
A engine e empacotada como `libkotonoha_engine.so`, com nome distinto de
`libKotonoha.so` do exemplo mesmo em sistemas de arquivos sem distincao de caixa.
SDL_ttf e ImGui continuam nos submodulos; FFmpeg, json-c, libass, iconv, PlutoSVG e
CPython opcional sao fornecidos pelo vcpkg.
DDLC usa uma instalacao separada em `build/vcpkg-python/<ABI>` para que configurar
outros jogos nao remova sua dependencia opcional de CPython.
Cada ABI possui sua propria instalacao para que o vcpkg nao remova as
bibliotecas da outra arquitetura ao configurar um APK com duas ABIs.
O manifesto fixa `vcpkg-tool-meson` em 1.9.0#10: o Meson 1.12.1 rejeita
prefixos de instalacao Windows (`C:/...`) ao compilar para Android.

Cada APK inclui `assets/` do motor e `game-assets/` do jogo escolhido.
Gradle prepara os recursos e calcula sua identidade pelo conteudo.
BootstrapActivity extrai o APK em uma thread de trabalho antes de iniciar
SDL e mostra erros de preparo. `-PbundleGameAssets=false` exclui dados
comerciais locais. Forneca dados obtidos legitimamente em
`../games/<jogo>/game-assets/`; nao os coloque no Git.

Os assets da engine sao extraidos em `<dataDir>/files/assets`; os do jogo em
`<dataDir>/game_assets`. Dados externos podem ficar em
`Android/obb/<applicationId>/game_assets`, `game-assets` ou diretamente no OBB,
com prioridade sobre os dados empacotados. Saves e configuracoes usam
`Android/data/<applicationId>/files/Kotonoha/<applicationId>`.
O launcher salva nomes de recursos e resolve seus caminhos em cada execucao.
`overlay` e descoberto automaticamente; `overlay2` selecionado no launcher
pode estar em OBB, dados externos ou dados privados da aplicacao.
Veja a [politica completa de diretorios](../docs/Directories.pt-BR.md).
O armazenamento com escopo pode limitar acesso de gerenciadores de arquivos;
nao solicitamos acesso amplo nem criamos uma pasta artificial `Android/obb-data`.

Os recursos extraidos sao privados, em caminhos estaveis; seu fingerprint
determina quando atualizar os arquivos a partir do APK.
Dialogos nativos podem estar indisponiveis no Android; o inspetor MPV tambem
oferece selecao de shaders incluidos no pacote.
