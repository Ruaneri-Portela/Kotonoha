# Kotonoha

[English](README.md) | Portugues (Brasil)

Motor de visual novel em C++17, SDL3 e SDL3_ttf. A interface do jogo usa SDL_ttf:
caixa de dialogo com revelacao UTF-8, escolhas, historico rolavel, menus e
entrada de texto. FFmpeg decodifica video e audio; libass desenha legendas ASS.
Dear ImGui atende o inspetor do motor (F10) e paineis auxiliares de School Days;
as cenas de dialogo, escolhas e menu de jogo usam SDL.

## Organizacao

`KotonohaEngine` e o motor; `Kotonoha` e o jogo demonstrativo. Os arquivos publicos
de `lib/include/Kotonoha` espelham os modulos de `lib/src`:

```text
CMakeLists.txt               Agrega motor e jogos; pode desativar o exemplo
vcpkg.json                   Dependencias externas, incluindo PlutoSVG
assets/                      Splash, shaders e fonte compartilhados
Xcode/Kotonoha.xcodeproj     Projeto iOS nativo e schemes compartilhados
Android/                    Projeto Gradle; SDL remoto do Git
lib/
  CMakeLists.txt             Biblioteca independente Kotonoha::Engine
  include/Kotonoha/
    core/                    Engine, Runtime, Scene, Clock, UI, Settings, Log,
                             AssetProvider e DiscImage (.iso/.bin)
    componentes/             Componentes SDL_ttf, VNManager e VNSave
    debug/                   HUD e inspetor ImGui mantidos pelo motor
    localization/            Dicionario JSON, exportacao e Shift-JIS (iconv)
    media/                   FFmpeg (MediaPlayer, MediaDecoder), saida SDL
      upscaling/             Base e implementacoes OpenGL/MetalFX
    mcp/                     Servidor interno stdio para agentes
    script/                  Nos, modos mestre e player temporal
  src/                       Implementacoes dos mesmos modulos
  submodules/
    imgui/                   Dear ImGui e seus backends
    SDL3/                    SDL compilado localmente
    SDL-ttf/                 SDL_ttf compilado localmente
  tests/                     Regressao de Settings, Log, audio, janela, upscaling,
                             assets, disco, encoding e midia
games/example/
  CMakeLists.txt             Jogo independente; inclui a lib quando necessario
  src/
    main.cpp                 Callbacks SDL e ciclo de vida da aplicacao
    DemoGame.*               Runtime, entrada de nome, save/load
    DemoStory.*              Grafo da historia, cenas e rotas nomeadas
    DemoStage.*              Cenario procedural e animacao
  assets/                    Scripts e traducoes de exemplo
  tests/                     Regressao do motor, integracao do jogo e MCP
games/zero-no-tsukaima/
  CMakeLists.txt             Formatos do PS2, jogo, ferramentas e testes
  include/ZeroNoTsukaima/    Arquivos .HD/.BIN, LZ, TIM2, VAG, PSS, catalogo e KAG
  src/
    main.cpp                 Callbacks SDL; --zero / ZERO_DISC
    DiscAssetProvider.*      DiscImageAssetProvider do motor + formatos do jogo
    ZeroSession.*            Interprete, audio nos canais, saves e desenho
    ZeroComponents.*         Subclasses dos componentes do motor
    ZeroRuntime.*            Runtime: estados, 640x448 dentro da novel
    Frontend.*, Media.*      Telas originais e PCM (filmes: MediaPlayer do motor)
  tools/                     ZeroExtract e ZeroRun (linha de comando)
  tests/                     Formatos e fluxo completo com o disco
games/snow-radish-vacation/
  CMakeLists.txt             Formatos ADV3, jogo, ferramentas e testes
  include/SnowRadish/        Pacotes .DAT (LZSS), VM .obj e estado do jogo
  src/
    main.cpp                 Callbacks SDL; --snow / SNOW_DIR
    SnowAssets.*             AssetProvider dos DAT (EVD/CHD/ADPS)
    SnowSession.*            VM, audio nos canais, transicoes, logo e titulo
    SnowComponents.*         Subclasses dos componentes do motor
    SnowRuntime.*            Runtime: estados, 800x600 dentro da novel
  tools/                     SnowExtract e SnowRun (linha de comando)
  tests/                     Formatos e fluxo completo com os DAT
games/doki-doki-literature-club/
  include/Ddlc/, src/        Leitores RPA/RPYC, CPython, runtime e apresentacao
  assets/                    Dados locais do usuario, ignorados pelo Git
  tests/                     Ponte Python e scripts com dados opcionais
build/                       Saida gerada (nao faz parte do codigo-fonte)
```

Pastas de modulos usam minusculas; arquivos/tipos C++ usam PascalCase.
`core` e `componentes` preservam os nomes escolhidos para o projeto. Nomes
como `DemoGame`, `DemoStory` e `DebugOverlay` indicam a responsabilidade.
A API CMake para integrar o motor e `Kotonoha::Engine`; o executavel continua
`Kotonoha`. ImGui e uma dependencia privada do motor; cabecalhos publicos nao
expoem tipos ImGui. A biblioteca do jogo e separada da biblioteca do motor.
Os callbacks SDL pertencem a aplicacao, nao a um header publico da engine.

## Compilar

Dependencias externas: CMake, pkg-config, json-c, libass >= 0.17, FreeType, HarfBuzz,
PlutoSVG com suporte a FreeType, OpenGL e FFmpeg (`libavformat`, `libavcodec`,
`libavutil`, `libswscale`, `libswresample`). Doki Doki usa tambem a biblioteca
de desenvolvimento CPython 3 e C++20. SDL3, SDL_ttf e ImGui sao compilados
dos submodulos em `lib/submodules`; nao sao dependencias vcpkg.
MetalFX e habilitado quando o SDK Apple fornece o framework.

```sh
git submodule update --init --recursive
# Exemplo macOS Apple Silicon; escolha outro triplet para outra plataforma.
export VCPKG_ROOT=/caminho/para/vcpkg
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=arm64-osx-dynamic \
  -DVCPKG_INSTALLED_DIR="$PWD/build/ios-deps/macos"
cmake --build build
ctest --test-dir build --output-on-failure
./build/games/example/Kotonoha
```

O manifesto [vcpkg.json](vcpkg.json) instala `plutosvg[freetype]` e sua
dependencia PlutoVG. SDL_ttf usa `SDLTTF_PLUTOSVG=ON` por padrao e falha na
configuracao se uma dependencia habilitada nao for encontrada. Para um build
sem suporte a glifos SVG, escolha explicitamente `-DSDLTTF_PLUTOSVG=OFF`;
um diretorio CMake ja configurado como `OFF` precisa de
`-DSDLTTF_PLUTOSVG=ON` para ativar o recurso. A renderizacao de emoji SVG
requer uma fonte que contenha esses glifos; a fonte japonesa incluida nao e
uma fonte de emoji. `Kotonoha.PlatformSmoke` verifica a rasterizacao de um
glifo SVG colorido e a conversao CP932. O teste de midia usa o executavel
`ffmpeg`.

Tambem e possivel compilar cada projeto isoladamente:

```sh
# Somente a biblioteca, sem jogo nem testes do exemplo:
cmake -S lib -B build/engine
cmake --build build/engine
# Ou pelo agregador: -DKOTONOHA_BUILD_EXAMPLE=OFF -DKOTONOHA_BUILD_ZERO=OFF -DKOTONOHA_BUILD_SNOW=OFF

# Jogo e sua dependencia do motor:
cmake -S games/example -B build/example -DBUILD_TESTING=ON
cmake --build build/example
ctest --test-dir build/example --output-on-failure
./build/example/Kotonoha
```

SDL_ttf, ImGui e libass usam somente as fontes de `assets/fonts`
(`NotoSansJP-Regular.ttf`, distribuida com sua licenca OFL); nao ha fallback
para fontes do sistema.

Todo caminho empacotado ou do usuario passa pelo
[FileResolver](lib/include/Kotonoha/core/FileSystem.hpp), que procura, nesta
ordem: bundle, pasta de dados preferida (OBB no Android), diretorio de
trabalho, diretorio do executavel e pasta de documentos do jogo.
`engineAsset("fonts/...")` procura em `assets/`; `gameAsset(jogo, ...)` procura
em `game-assets/` e em `games/<jogo>/game-assets/`. `resolve` lanca um erro
listando todos os locais procurados.

O fundo padrao do motor e preto transparente (`0,0,0,0`).

Os fontes `VN*.cpp` e seus headers ficam em `componentes`. Inclua, por exemplo,
`<Kotonoha/componentes/VNManager.hpp>` ou `<Kotonoha/core/Engine.hpp>`.
`clang-format` usa a configuracao versionada em `.clang-format`.

### Depuracao nativa de cenas e rotas editaveis

F10 apresenta a lista nativa de cenas/nos e permite saltar para um no em
cada jogo. As previas usam os provedores do jogo: imagens, quadros de video
no instante selecionado e audio decodificado. Apos um salto de depuracao em
Zero no Tsukaima ou Snow Radish, saves de replay ficam desabilitados ate um
inicio normal ou carregamento de save.

O inspetor F10 e o MCP exportam/importam rotas editaveis `.json` e rotas
binarias `.ktnroute`. As rotas genericas do motor usam o perfil `kotonoha.vn`
e a secao KTRF `VNSC`. Callbacks nativos registrados sao reassociados durante
o carregamento; codigo executavel nativo nao e serializado. School Days usa
seu perfil binario KTRF original full-core pelo adaptador do runtime. Os
perfis nao sao intercambiaveis. O parser ORS e o roteamento compartilhado
ficam em `lib/`; o comportamento especifico permanece nos jogos, sem
incorporar um segundo motor legado.

### Legendas ASS e argumentos extensivos

Todos os jogos aceitam `--help` ou `-h`, com opcoes do motor e do proprio jogo.
Argumentos desconhecidos e valores ausentes produzem erros explicitos.

```sh
build/macos/games/school-days/SchoolDays.app/Contents/MacOS/SchoolDays --help
build/macos/games/school-days/SchoolDays.app/Contents/MacOS/SchoolDays \
  --subtitle-style games/school-days/game-assets/styles.ass
```

`--subtitle-style ARQUIVO.ass` (alias `--ass-style`) importa apenas `[Script Info]` e `[V4+ Styles]`.
Os textos/eventos e anexos originais sao descartados; o jogo fornece as novas
falas. School Days usa por padrao seu `styles.ass` empacotado, portado do recurso
original com a extensao padrao `.ass` e a fonte incluida Noto Sans JP.
O nome do personagem seleciona o estilo de mesmo nome; sem correspondencia,
usa `Default` ou o primeiro estilo importado.

A cena compartilhada [SubtitleScene](lib/include/Kotonoha/componentes/SubtitleScene.hpp)
usa libass e herda o estilo configurado no motor. `setCues` recebe as falas;
`setPosition` atualiza o tempo e `setStyle` permite substituir o estilo.
As legendas das timelines tambem usam essa cena. School Days a estende
para apresentar as falas ORS.

A cena [FadeScene](lib/include/Kotonoha/componentes/FadeScene.hpp), portada do
Kotonoha, faz fade in/out para uma cor. Use eventos numa timeline externa
(`add` + `setPosition`) ou o relogio proprio (`play`); `hold` mantem a cor ao
fim de um fade out. Os jogos a reutilizam em vez de fades proprios.

O [CommandLine](lib/include/Kotonoha/core/CommandLine.hpp) registra os argumentos
comuns. Cada jogo acrescenta os seus antes de chamar `parse`:

```cpp
Kotonoha::CommandLine cli("MeuJogo", options);
cli.option("--my-game", "PATH", "Game data", gamePath);
cli.alias("--my-game", "--data");
cli.flag("--my-launcher", "Open the game launcher", launcher);
cli.parse(argc, argv);
// Trate cli.wantsHelp() antes de inicializar SDL ou carregar o jogo.
std::cout << cli.help();
```

Opcoes e flags tambem aceitam callbacks para validacao especifica do jogo.
Descricoes e aliases registrados aparecem automaticamente no help.

### School Days, volume global e tamanho do debug

School Days usa cenas SDL personalizadas para o menu original no topo,
escolhas temporizadas no topo/rodape e legendas libass, sem janela permanente
de controles de depuracao durante a partida. Mova o ponteiro ao topo para
revelar o menu. Escolhas aceitam mouse, setas, Enter/Espaco e os numeros
exibidos. Configuracao, slots de save, backlog e mapa de rotas ainda usam
paineis ImGui; os controles auto/velocidade nao implementados ficam desativados.
F5 salva rapidamente e F6 carrega.

A decodificacao agora abastece os buffers de video e audio antes de ceder
o quadro, com preroll de audio de 350 ms. Pedidos repetidos de pausa nao
reiniciam o stream SDL. O teste de regressao mede a duracao minima enfileirada
durante reproducao em tempo real e atrasos simulados de quadros.

O menu generico, launcher e inspetor possuem volume global e mute.
O ganho global combina com volume/mute do canal e ganho individual do stream;
novos canais herdam a configuracao. Esquerda/direita no menu generico ajusta
o volume em 5%. As configuracoes persistem, exceto com persistencia desativada.

F8 inclui o tempo do no atual, inclusive nos runtimes personalizados. F9
mostra FPS e milissegundos por quadro. **Debug UI scale**, no inspetor e no
launcher, amplia os controles de 1x a 3x alem da escala de DPI e tambem persiste.

### iOS: projeto Xcode nativo

Abra `Xcode/Kotonoha.xcodeproj`: o projeto e seus targets sao mantidos no
Xcode e **nao exigem CMake para compilar**. `KotonohaEngine` e um framework
estatico; SDL3, SDL3_ttf e Dear ImGui sao frameworks dinamicos. O target SDL3 usa o projeto
Xcode do proprio submodulo; SDL_ttf compila com HarfBuzz e PlutoSVG.
Os schemes `Kotonoha`, `ZeroNoTsukaima`, `SnowRadish`, `DokiDoki` e `SchoolDays` geram
apps separados. `KotonohaIOSSmoke` verifica CP932 e glifos SVG no simulador.
O executavel demonstrativo chama-se `Kotonoha`; seu namespace C++ e
`KotonohaExample`, separado do motor. A opcao CMake
`KOTONOHA_BUILD_SCHOOL_DAYS` controla o port School Days.
Depois de alterar os fontes da biblioteca School Days, execute manualmente
`python3 Xcode/UpdateGameTargets.py` para sincronizar o target nativo sem
remover os recursos e modulos Python existentes.
`DdlcIOSScriptSmoke` exercita CPython e os scripts reais no simulador.
O deployment target e iOS 15; MetalFX exige iOS 16 ou superior e nao esta
disponivel no SDK do simulador.

Com Xcode completo, seus SDKs iOS, vcpkg e os submodulos inicializados,
prepare os pacotes estaticos para dispositivo e simulador Apple Silicon:

```sh
export VCPKG_ROOT=/caminho/para/vcpkg
"$VCPKG_ROOT/vcpkg" install --x-manifest-root="$PWD" \
  --x-install-root="$PWD/vcpkg_installed" --triplet=arm64-ios
"$VCPKG_ROOT/vcpkg" install --x-manifest-root="$PWD" \
  --x-install-root="$PWD/build/ios-deps/simulator" --triplet=arm64-ios-simulator
open Xcode/Kotonoha.xcodeproj
# Ou, sem assinatura, apenas para verificar a compilacao:
xcodebuild -project Xcode/Kotonoha.xcodeproj -scheme Kotonoha \
  -destination 'generic/platform=iOS' CODE_SIGNING_ALLOWED=NO build
```

Use raizes de instalacao separadas: o vcpkg em modo manifesto pode remover os
pacotes de outro triplet na mesma raiz. O projeto esta configurado para
simulador `arm64`; Macs Intel precisam de um triplet `x64-ios` e configuracoes
Xcode correspondentes. Se o Xcode informar que o compilador Metal esta
ausente, instale-o com `xcodebuild -downloadComponent MetalToolchain`.
Para instalar em dispositivo, selecione sua equipe de assinatura e altere os
identificadores de bundle quando necessario.

Os IDs nativos dos jogos correspondem aos applicationIds Android:
`me.hirameki.kotonoha.zeronotsukaima`, `.snowradish`, `.ddlc` e `.schooldays`.
O exemplo usa `org.kotonoha.example`. O bundle ID/applicationId selecionado
na plataforma determina a identidade da instalacao, sandbox, preferencias
e armazenamento externo; metadados SDL em C++ nao substituem Xcode/Gradle.
IDs antigos, como `org.kotonoha.neostart`, `me.hirameki.neostart.*` e os
antigos IDs iOS `me.kotonoha.*`, representam instalacoes diferentes.
Preferencias e saves anteriores nao sao migrados automaticamente. Exporte
os saves necessarios antes de desinstalar o app antigo e atualize os perfis
de provisionamento para os novos IDs.

O target iOS de Doki Doki tambem requer o XCFramework CPython precompilado
do projeto BeeWare, com as bibliotecas padrao e extensoes para iOS. Exemplo
da versao testada (o SHA-256 e publicado na release):

```sh
mkdir -p build/ios-deps/python
curl -fL https://github.com/beeware/Python-Apple-support/releases/download/3.15-b3/Python-3.15-iOS-support.b3.tar.gz \
  -o build/ios-deps/python/Python-iOS.tar.gz
echo '429080dcfab3778b75bbc6548cbfd754dc943cfc6092a92b72022541ff3212a0  build/ios-deps/python/Python-iOS.tar.gz' | shasum -a 256 -c -
python3 tools/PrepareApplePython.py --archive build/ios-deps/python/Python-iOS.tar.gz
```

O preparo externo converte as extensoes em frameworks e cria os marcadores
`.fwork`. O projeto incorpora `Python.xcframework`, a biblioteca padrao e as
extensoes com fases nativas Resources e Embed Frameworks, sem shell scripts
nos targets Xcode. Nenhuma biblioteca Python
do macOS e usada no iOS. Os arquivos baixados ficam em `build/`, fora do Git.

Cada app inclui `assets/` do motor e `game-assets/` do seu jogo. Os fontes
permanecem em `assets/` e `games/<jogo>/game-assets/`; o Xcode copia
diretamente as pastas com o nome correto, sem scripts. Nao coloque dados
comerciais no Git.
Inclua somente dados que voce tem direito de empacotar; a configuracao nao
fornece os jogos comerciais. No CMake, a copia e habilitada por padrao;
`-DKOTONOHA_BUNDLE_LOCAL_GAME_ASSETS=OFF` cria uma pasta vazia e rejeita
dados residuais de builds anteriores. No Xcode, controle a inclusao pela
associacao de `game-assets` ao target na fase Resources.

O layout dentro do pacote e `game-assets/ZeroNoTsukaima.bin`,
`game-assets/SCRIPT.DAT` e demais DAT, ou `game-assets/game/*.rpa` e
`game-assets/characters/*.chr`, conforme o jogo.

### Dados e documentos

A biblioteca expoe `resourceDirectory()`, `engineAssetDirectory()`,
`gameAssetDirectory(game)`, `preferredDataDirectory(game)` e
`preferredDocumentsDirectory(game)` em `Kotonoha/core/FileSystem.hpp`.
No macOS, os recursos ficam em `App.app/Contents/Resources`; no iOS, no
diretorio de recursos do `.app`. Os dados preferidos sao esses recursos
(somente leitura). Os documentos preferidos sao `Documents` no iOS e o
diretorio SDL_GetPrefPath por jogo no desktop.

No Android, dados preferidos ficam em `Android/obb/<applicationId>` e
documentos em `Android/data/<applicationId>/files`. Sao caminhos diferentes,
obtidos pelas APIs Android, nao por substituicao de texto. Os assets do APK
sao extraidos em armazenamento privado antes de iniciar SDL, enquanto os
defaults dos jogos procuram primeiro dados externos em OBB. Configuracoes,
saves e estado persistente usam os documentos, nunca o pacote somente leitura.

### Android

Abra [Android/](Android/) no Android Studio. Ha APKs separados para exemplo,
Zero, Snow e DDLC. SDL3 vem de um checkout remoto do Git com revisao fixa;
SDL_ttf e ImGui continuam nos submodulos. O NDK compila o motor e o jogo como
shared libraries carregadas pela SDLActivity. FFmpeg, json-c, PlutoSVG e
CPython de DDLC sao dependencias vcpkg para Android.
Veja [o guia Android em portugues](Android/README.pt-BR.md).

### Shaders MPV e MetalFX

Com backend MPV, o launcher e o inspetor F10 exibem presets e uma lista
ordenada de arquivos `.glsl`: escolha varios arquivos, selecione shaders
incluidos, remova ou mova itens. Lista vazia usa o preset. `shaderFiles` salva
um caminho por linha; listas antigas separadas por `:` continuam aceitas.
Caminhos completos, espacos e letras de unidade Windows sao preservados.
MetalFX nao exibe seletor de metodo/preset ou arquivos GLSL. A escala continua
configuravel: 1.0 desliga; acima de 1.0 liga, ate 4.0.

Para testar DDLC no simulador sem incluir dados comerciais no bundle, forneca
uma instalacao local confiavel com `game/` e `characters/` e copie os arquivos
para o container isolado do teste:

```sh
xcodebuild -project Xcode/Kotonoha.xcodeproj -scheme DdlcIOSScriptSmoke \
  -configuration Debug -sdk iphonesimulator \
  -destination 'platform=iOS Simulator,name=iPhone 18 Pro Max' \
  -derivedDataPath build/xcode-ios-simulator CODE_SIGNING_ALLOWED=NO build
xcrun simctl install booted \
  build/xcode-ios-simulator/Build/Products/Debug-iphonesimulator/DdlcIOSScriptSmoke.app
container="$(xcrun simctl get_app_container booted org.kotonoha.ddlc-ios-smoke data)"
mkdir -p "$container/Documents/ddlc-test/game" "$container/Documents/ddlc-test/characters"
cp /caminho/DDLC/game/*.rpa "$container/Documents/ddlc-test/game/"
cp /caminho/DDLC/characters/*.chr "$container/Documents/ddlc-test/characters/"
SIMCTL_CHILD_DDLC_DIR="$container/Documents/ddlc-test/game" \
  xcrun simctl launch --console-pty booted org.kotonoha.ddlc-ios-smoke
```

O teste deve registrar `KOTONOHA_DDLC_IOS_SCRIPT_OK`; `simctl launch` por si so
nao garante que as assercoes do app passaram. Ajuste o nome do simulador ao
dispositivo disponivel e nao distribua os arquivos copiados.

Para criar bundles macOS pelo fluxo CMake separado, use
`-DKOTONOHA_BUILD_MACOS_BUNDLES=ON`; esse fluxo nao gera o projeto iOS.
Somente o bundle de DDLC inclui CPython, em `Contents/Frameworks`. As unicas
pastas em `Contents/Resources` sao `assets/` e `game-assets/`.

As configuracoes de depuracao macOS do VS Code configuram automaticamente um
build de bundles Debug com Ninja em `build/macos` antes de compilar o jogo.
As dependencias descritas acima e o Ninja precisam estar instalados. Para
configurar manualmente, por exemplo para definir outras opcoes do CMake:

```sh
cmake -S . -B build/macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DKOTONOHA_BUILD_MACOS_BUNDLES=ON
```

Cada configuracao compila seu alvo e depura o executavel dentro do `.app`.
Os caminhos dos dados em `.vscode/launch.json` sao locais e devem ser ajustados
conforme a instalacao. School Days usa `--school-days` para a pasta Packs e
`--scripts` para scripts externos opcionais. Seu launcher permite selecionar a
pasta do jogo e a rota KTRF, lembrando a rota escolhida e usando a do bundle
como padrao. `--ktrf` sobrescreve explicitamente a rota lembrada.

## Controles

- Clique esquerdo, Espaco ou Enter: completar a fala / avancar.
- Escolhas: clique, setas e Enter/Espaco; roda do mouse para listas longas.
- B: historico. Roda do mouse ou setas: rolar. B/ESC: fechar.
- ESC: menu de pausa; setas/Enter ou mouse nos menus.
- Entrada de nome: texto UTF-8, Backspace, Enter e ESC.
- F10: abrir/fechar o inspetor do motor; ESC tambem o fecha.
- F11 ou Alt+Enter: alternar janela / tela cheia sem bordas.
- F9: ativar/desativar FPS.
- F8: ativar/desativar no e clock temporal.
- F5/F6: salvar/carregar o quicksave (separado dos saves manuais).
- Menus Salvar/Carregar: lista de saves, sem limite fixo de slots.

## Jogo demonstrativo

O exemplo executa uma historia completa, sem arquivos externos obrigatorios:

| Cena | Nos | Modo |
|------|-----|------|
| Chegada noturna | 100 | Abertura temporal de 6 segundos |
| Acampamento | 0-8 | Falas, nome, inventario e escolha tradicionais |
| Tempestade | 30-32 | Texto/legenda temporal e pergunta que pausa no prazo |
| Bifurcacao | 10 | Escolha tradicional |
| Caverna / montanha | 11-18 | Cinematica temporal, condicao de inventario e resposta por clique |
| Ponte | 40-44 | Pergunta de 4 segundos com rota automatica sem resposta |
| Desfecho | 50, 20, 21 | Condicao de confianca e finais tradicionais |

O cenario e desenhado pelo SDL, com montanhas, estrelas e chuva animada.
As cenas declaram nos com nomes e IDs estaveis; o VNManager conduz o fluxo.
Nao ha FPS, clock de debug ou inicializacao ImGui no codigo do jogo.
`KOTONOHA_TIMELINE` substitui a abertura. Opcionalmente, defina
`KOTONOHA_DEMO_VIDEO`/`KOTONOHA_DEMO_AUDIO` com arquivos locais para adicionar
midia a abertura; a duracao continua sendo definida pela timeline.

## Componentes extensiveis

`VNTextBox`, `ChoiceQuestionScene`, `GameMenu`, `InGameMenu` e `VNBacklog`
mantem entrada, pausa da historia e troca de estado na classe base e chamam
hooks `protected` virtuais que um jogo sobrescreve:

| Componente | Hooks |
|------------|-------|
| `VNTextBox` | `charactersPerSecond`, `recordDialogue`, `openMenu`, `openBacklog`, `renderFrame`, `renderSpeaker`, `renderDialogue`, `renderIndicator` |
| `ChoiceQuestionScene` | `listArea`, `layout`, `select`, `openMenu`, `openBacklog`, `renderPanel`, `renderOption` |
| `GameMenu` | `itemCount`, `itemLabel`, `itemRect`, `activate`, `renderTitle`, `renderItem`, `open`/`close`/`isVisible` |
| `InGameMenu` | `itemCount`, `itemLabel`, `itemRect`, `activate`, `openBacklog`, `renderPanel`, `renderItem`, `open`/`close`/`IsOpen` |
| `VNBacklog` | `addEntry`, `clear`, `open`/`close`/`isVisible`, `renderFrame`, `renderEntries` |

`lib/tests/ComponentHooksTests.cpp` verifica o despacho de cada hook.

## Zero no Tsukaima (PS2)

Porta de *Zero no Tsukaima: Muma ga Tsumugu Yokaze no Nocturne* (SLPS 258.30)
sobre o motor.
O disco do usuario nunca e copiado para o repositorio:

```sh
./build/games/zero-no-tsukaima/ZeroNoTsukaima --zero /caminho/ZeroNoTsukaima.bin
./build/games/zero-no-tsukaima/ZeroNoTsukaima --launcher   # abre o launcher
# Sem --launcher inicia direto: disco de --zero, ZERO_DISC ou o ultimo
# escolhido no launcher (disc.txt); aceita --config, --no-config, --log,
# --renderer, --mcp e --frames N (sai apos N quadros)
# --translation arquivo.json carrega uma traducao; --dump-translation
# arquivo.json exporta todas as rotas (preservando a traducao carregada) e sai
cmake -S . -B build -DZERO_DISC_IMAGE=/caminho/ZeroNoTsukaima.bin  # habilita os testes com disco
```

O **launcher** so abre quando pedido (`--launcher`). `ZeroLauncher` estende o
`Kotonoha::GameLauncher` generico do motor: aba *Game* com o caminho do disco,
botao "Browse..." (seletor nativo) e validacao (`SLPS_258.30` no ISO 9660);
abas *Video* (modo, monitor, tamanho, fullscreen exclusivo, VSync, renderer),
*Upscaling* (escala 1x..4x, backend, preset) e *Advanced* (log, shaders MPV).
**Play** grava `settings.json` e `disc.txt` na pasta de preferencias
(`EngineSettings::defaultPath("zero-no-tsukaima")`), fecha a janela do launcher
e inicia o motor com essas configuracoes.

### Launcher generico

`Kotonoha::Launcher` e a janela ImGui de configuracao (video, upscaling,
logs) com `drawGameOptions()`, `validate(error)` e `save()` virtuais,
`browseFile()` e `browseFolder()` (seletores nativos). `Kotonoha::GameLauncher`
estende-o para jogos que leem os dados do usuario de um caminho: descreve o
campo com `PathOptions` (rotulo, `PathKind::File` com filtros ou
`PathKind::Folder`, arquivo lembrado como `disc.txt`/`dir.txt` e texto de
ajuda) e o jogo implementa apenas `checkPath(path, info)`.
`GameLauncher::rememberedPath(settings, arquivo)` devolve o ultimo caminho,
usado quando o jogo inicia sem launcher. Toda a interface do motor e dos jogos
(launcher, menus, configuracoes, F10) e em ingles.

`DiscAssetProvider` e o `AssetProvider` do motor (`EngineOptions::assets`).
Ids: `scenedat/<n>`, `normal/<NOME>`, `voice/<n>`, `sound/<NOME>`,
`movie/<n>` (PSS bruto), `movie/<n>.wav` (PCM SShd/SSbd do PSS),
`script/<NOME.KS>` e qualquer caminho ISO (`SYSTEM.CNF`, `MOVIE/MOVE00.PSS`);
`loadSurface` decodifica TIM2, entao `engine.GetAssets().texture(...)` funciona
para qualquer imagem do disco. A leitura do disco e o `DiscImage` do motor, os
textos usam `Kotonoha::ShiftJisToUtf8` e os filmes tocam no
`Kotonoha::MediaPlayer` (video pelo FFmpeg, PCM como `openSoundtrack`, canal
`bgm`, esticado no 640x448 como no original).
Voz, BGM e efeitos tocam nos canais `voice`, `bgm` e `se` do mixer (volume e
mudo no F10). O viewport do jogo e 960x672 (`ZeroRuntime::viewport()`, 640x448
x1.5); as cenas do jogo desenham em 640x448 e os componentes do motor no
espaco de design 1280x720, ambos ajustados ao viewport. As imagens passam pelo
upscaler do motor no fator configurado (texturas 2x com o tamanho logico
640x448 preservado; mudar o fator recarrega as imagens).

Rotas de traducao: cada cena `.KS` e uma rota com o indice de `SCENE_ID`
(`"0"`, `"1"`...), contendo as falas `[ch]`, escolhas `[seladd]` e nomes
`[dispname]`; o historico e as escolhas usam o mesmo dicionario.

| Elemento original | Componente do motor |
|-------------------|---------------------|
| Logos, titulo, modos e galerias | `ZeroTitleMenu : GameMenu` |
| Janela de mensagem, auto e skip | `ZeroTextBox : VNTextBox` |
| SelectWindow / mapa (MoveState) | `ZeroSelectChoice`, `ZeroMoveChoice : ChoiceQuestionScene` |
| CommandState (personagem, save, load, opcoes) | `ZeroCommandMenu : InGameMenu` |
| HistoryState (com vozes) | `ZeroBacklog : VNBacklog` |
| Cenario, sprites, CGs, transicoes e filmes | `ZeroStage : Scene` |
| Roleta (tndlslot) | `ZeroRoulette : Scene` |

Controles: Enter/Espaco/Z/clique avancam; A alterna o auto; Ctrl pula texto
lido; ESC ou clique direito abre o CommandState; B, PgUp ou roda para cima
abrem o historico; F2/F3 e F4/F5 trocam a taxa de voz e BGM. Os saves (64
slots, rotulo + entradas reproduzidas) e `system.txt`/`read.txt` ficam no
diretorio de preferencias `Kotonoha/zero-no-tsukaima`; `saveGame`/`loadGame`
do runtime usam o mesmo formato.

Variaveis: `ZERO_FONT` (fonte japonesa; sem ela procura as do sistema),
`ZERO_START` (comeca o New Game em outro script), `ZERO_VOICE_RATE`,
`ZERO_BGM_RATE`, `ZERO_SE_RATE` e, nos testes, `ZERO_CAPTURE_DIR` (salva BMPs
de cada etapa).

## Snow Radish Vacation!! (PC)

Porta de *Snow Radish Vacation!!* (Overflow, ADV3, 800x600) sobre o motor. Os
`DATA/PACK/*.DAT` do usuario nunca sao copiados para o repositorio:

```sh
./build/games/snow-radish-vacation/SnowRadish --snow /caminho/DATA/PACK
./build/games/snow-radish-vacation/SnowRadish --launcher   # abre o launcher
# Sem --launcher inicia direto: pasta de --snow, SNOW_DIR ou a ultima
# escolhida no launcher (dir.txt); aceita --config, --no-config, --log,
# --renderer, --mcp e --frames N (sai apos N quadros)
# --translation arquivo.json carrega uma traducao; --dump-translation
# arquivo.json exporta todas as rotas (preservando a traducao carregada) e sai
cmake -S . -B build -DSNOW_DATA_DIR=/caminho/DATA/PACK  # habilita os testes com os DAT
```

`SnowAssets` e o `AssetProvider` do motor: ids `script`, `graphic/<nome>`,
`sound/<nome>` e `voice/<nome>`; `loadSurface` decodifica EVD/IMG/CHD. O
viewport do jogo e 960x720 (800x600 x1.2); o `SnowRuntime` converte as
coordenadas do mouse. Voz, BGM e efeitos usam `Kotonoha::PcmPlayer` nos canais
`voice`, `bgm` e `se` do mixer. `SnowLauncher : Kotonoha::GameLauncher` escolhe
a pasta (exige SCRIPT e GRAPHIC.DAT; SOUND/VOICE opcionais).

Os textos Shift-JIS usam `Kotonoha::ShiftJisToUtf8`. Rotas de traducao: cada
objeto do SCRIPT.DAT e uma rota com o id de script de 12 bits (`"0"`..`"4095"`)
e os nomes de lugares do mapa ficam na rota `"4096"`; falas, nomes e escolhas
sao traduzidos pelo dicionario do motor.

| Elemento original | Componente do motor |
|-------------------|---------------------|
| Logo Overflow e titulo (nativos 221/213) | `SnowTitleMenu : GameMenu` |
| Janela de mensagem, auto, skip e botoes | `SnowTextBox : VNTextBox` |
| Escolhas em texto, mapa (219) e uniformes (220) | `SnowTextChoice`, `SnowMapChoice`, `SnowUniformChoice : SnowChoice : ChoiceQuestionScene` |
| Pausa (continuar, historico, config., salvar, carregar, titulo) | `SnowPauseMenu : InGameMenu` |
| Historico | `SnowBacklog : VNBacklog` |
| Cenario, sprites, bocas e transicoes | `SnowStage : Scene` |

Controles: Enter/Espaco/clique avancam; Ctrl pula; ESC ou clique direito abre
a pausa; B, PgUp ou roda para cima abrem o historico; nas escolhas setas
movem o foco, a..i escolhem a opcao e Enter confirma. Saves sao `VNSave` do
motor (menu `VNSaveMenu`) com as entradas reproduzidas em `gameData`.

Variaveis: `SNOW_FONT` (fonte japonesa; sem ela procura as do sistema),
`SNOW_SAVE_DIR` (pasta dos saves), `SNOW_TRACE=1` (loga os nativos da VM) e,
nos testes, `SNOW_CAPTURE_DIR` (salva BMPs de cada etapa). Detalhes da
engenharia reversa em `games/snow-radish-vacation/PORT_STATUS.md`.

## Doki Doki Literature Club!

O port usa a biblioteca CPython incorporada, nao um interpretador Python
proprio. Os leitores RPA/RPYC alimentam a VM e os componentes polimorficos do
motor. Os arquivos originais nao sao incluidos:

```sh
./build/games/doki-doki-literature-club/DokiDoki --ddlc /caminho/DDLC/game
./build/games/doki-doki-literature-club/DokiDoki --launcher
cmake -S . -B build -DDDLC_DATA_DIR=/caminho/DDLC/game
ctest --test-dir build --output-on-failure -R '^DokiDoki\.'
```

Mantenha os arquivos `.chr` originais em `characters/`, ao lado de `game/`.
Sem eles o jogo pode iniciar em outra fase; o launcher e o log avisam quando
faltam arquivos, mas nao impedem alteracoes intencionais pelo jogador.
O teste com dados reais percorre os scripts ate o primeiro minijogo de poema;
os atos posteriores e a apresentacao completa ainda precisam de validacao.
As rotas de traducao seguem a ordem dos arquivos `.rpyc`; textos dinamicos
e poemas ainda nao estao totalmente catalogados. O carregamento executa
codigo Python dos dados do jogo: as restricoes de importacao nao constituem
uma sandbox. Use somente arquivos confiaveis.

## Debug do motor

O motor inicializa o HUD automaticamente em todos os estados:
FPS no canto superior esquerdo da **janela**; no e clock no canto superior
direito. O clock aparece apenas quando o no ativo e temporal.
Ative/desative cada indicador com F9/F8, no menu F10 ou pela API:

```cpp
#include <Kotonoha/debug/DebugOverlay.hpp>
engine.GetDebug().getSettings().showFPS = true;
engine.GetDebug().getSettings().showNodeClock = true;
```

Jogos com interpretador proprio sobrescrevem `Runtime::debugNode()` para
mostrar a posicao do script como no (HUD, inspetor e `node_name` do
`get_state` no MCP). Zero no Tsukaima mostra
`PR_00.KS *PR_OUT | scene 204 | tag 48` (script, label, rota de traducao e
tag); Snow Radish mostra `srvpr.obj | route 5 | pc 0x0027`.

O inspetor ImGui lista todos os nos carregados, em ordem de ID, com nome,
tipo, destino, textos, alternativas e cues temporais. Selecione um no e
use **Jump to selected node**. A navegacao so e permitida durante gameplay;
remove modais do no anterior e executa o destino, mas nao reexecuta
acoes anteriores nem reconstrui automaticamente o inventario.
O inspetor pausa o relogio e captura entrada para nao avancar o jogo.

**Dump translation.json** (`Engine::dumpTranslation`) exporta o dicionario dos
nos carregados e das rotas do jogo (`Runtime::translationSources()`) para o caminho
informado (padrao: `translation.json` no diretorio de trabalho). A exportacao
usa objetos JSON por ID de no, com pares `"original": "traducao"`; preserva
traducoes carregadas, placeholders e espacos das bordas. Inclui legendas e escolhas, mas nunca caminhos
de midia. Um arquivo existente e substituido atomicamente; erros aparecem
no menu e no log. Jogos com rotas proprias (Zero no Tsukaima, Snow Radish)
exportam todos os textos do script sem precisar comecar a historia.

APIs correspondentes: `getNodeIds()`, `debugJumpToNode(id)` e
`dumpTranslation(path)` do VNManager.

Historico e menu bloqueiam entrada na historia e pausam a timeline e o
audio; fechar o overlay nao avanca o dialogo. Escolhas e entrada de nome
tambem capturam entrada, sem cliques atravessando o componente.
Carregar continua sendo um callback da aplicacao. Configuracoes tem uma
implementacao padrao do motor, acessivel nos menus principal e de pausa;
o jogo ainda pode substituir os callbacks de configuracao.

### Tema ImGui

O overlay de debug e o launcher compartilham o tema roxo do Kotonoha
(`lib/src/debug/ImGuiTheme.cpp`, privado da biblioteca): cantos
arredondados, abas com overline de destaque, cores de status/log por
prioridade e botao de acao primario. A fonte da UI e
`assets/fonts/NotoSansJP-Regular.ttf` (com glifos japoneses); sem ela usa a
fonte padrao do ImGui. A lista de nodos e cacheada ao abrir o overlay (botao
"Refresh node list" para atualizar) e as listas de nodos/log sao recortadas,
evitando queda de FPS em jogos com muitos nodos como School Days.

## Infraestrutura migrada do Kotonoha

A migracao mantem o runtime, os componentes SDL_ttf, a timeline e o MCP do
Kotonoha. Os backends de upscaling do Kotonoha foram adaptados para
`lib/src/media/upscaling`, sem dependencia dos antigos globais de configuracao
ou de caminhos para o outro projeto. Os perfis de jogos e o parser `.ORS`
do repositorio original nao fazem parte deste runtime de grafos/timelines.

### Dependencias locais

`lib/submodules/imgui` preserva os fontes que antes estavam em
`lib/third_party/imgui`, agora vinculados ao commit oficial correspondente.
As tres dependencias sao submodulos Git reais declarados em `.gitmodules`:

```sh
# Apos clonar o repositorio pai:
git submodule update --init --recursive
git submodule status
```

SDL3 usa o HEAD oficial obtido em 2026-10-30 (`4bb5020be2a3`), que ja expoe
`SDL_PROP_TEXTURE_METAL_TEXTURE_POINTER`. Nao ha patch local no SDL.
SDL-ttf usa o tag `release-3.2.2`; os fontes dos submodulos sao preservados.
Os gitlinks fixam as revisoes; um checkout
nao avanca automaticamente com mudancas futuras no upstream.

MetalFX sincroniza os comandos
SDL antes de consumir a textura em sua propria fila Metal; isso inclui uma
leitura de um pixel e uma espera de GPU, portanto tem custo de sincronizacao.

### Splash, viewport e bibliotecas

Nos builds desktop normais, `libkotonoha` (`Kotonoha::Engine`) e
`kotonoha_imgui` sao bibliotecas compartilhadas. No projeto iOS nativo,
`KotonohaEngine` e estatico e ImGui e um framework dinamico.
`kotonoha_copy_assets(alvo)` copia `assets/` (inclusive o `SPLASH.PNG`)
para o diretorio de recursos do executavel ou bundle.

Todo jogo comeca no estado `Splash` (`SplashScreen`): cada imagem de
`Runtime::splashPictures()` (padrao: `assets/SPLASH.PNG`) aparece com fade in
e avanca apos 3 segundos ou antes por clique/toque (teclas sao ignoradas);
depois da ultima o runtime vai para `Menu`.

`Runtime::viewport()` define o tamanho logico da janela de cada jogo (padrao
1280x720; Zero no Tsukaima 960x672; Snow Radish 960x720) e
`Runtime::sceneSize(scene)` o espaco de cada cena: componentes do motor
(`designSpace()`) usam 1280x720, ajustado e centralizado no viewport por
`UI::fit` / `UI::FrameScope`; eventos de mouse sao convertidos com
`UI::toFrame`. Sem `settings.json` a janela inicial segue a proporcao do
viewport.

### Janela e persistencia

O menu **Configuracoes** e SDL_ttf, nao ImGui. Edita modo de janela,
resolucao, monitor, redimensionamento, VSync, backend/preset e fator de
upscaling. A historia/audio pausam e os eventos nao chegam as cenas enquanto
o menu esta aberto. ESC/Voltar descartam o rascunho. Aplicar salva somente
apos validar/aplicar; erros aparecem no menu e no log e restauram as
configuracoes anteriores.

O exemplo usa `settings.json` no diretorio de preferencias SDL da aplicacao.
A biblioteca nao persiste por padrao: use `EngineOptions::persistSettings`
ou forneca `settingsPath`. Redimensionamentos em modo janela sao registrados,
e o encerramento salva a ultima configuracao. Arquivos inexistentes iniciam
com defaults; arquivos existentes invalidos causam erro, nao reset silencioso.
O formato e JSON versionado, limitado a 1 MiB e salvo atomicamente:

```json
{
  "version": 1,
  "width": 1280,
  "height": 720,
  "mode": 0,
  "display": 0,
  "resizable": true,
  "vsync": false,
  "renderer": "opengl",
  "upscaler": 2,
  "preset": 1,
  "factor": 2.0
}
```

`mode`: 0 janela, 1 fullscreen desktop sem bordas, 2 fullscreen exclusivo.
`display` e um indice zero-based dos monitores SDL. Fullscreen exclusivo
aceita `fullscreenWidth`, `fullscreenHeight` e `refreshRate`; ambos os
tamanhos em zero usam o modo desktop, senao usa o modo SDL mais proximo.
A novel continua em 1280x720 com letterbox, independente da janela.

```sh
./build/games/example/Kotonoha --config ./settings.json --log ./engine.log
./build/games/example/Kotonoha --no-config --renderer opengl
```

`--renderer` substitui a selecao carregada do arquivo. Trocar renderer exige
reiniciar a engine; o menu so oferece backends compativeis com o renderer
ativo. Um monitor removido ou backend indisponivel e reportado como erro.
O MCP desativa preferencias automaticas, salvo quando `--config` e explicito,
e nunca envia logs para stdout.

```cpp
auto settings = engine.GetSettings();
settings.vsync = true;
settings.upscaling.backend = Kotonoha::UpscaleBackend::Linear;
settings.upscaling.factor = 2;
engine.applySettings(settings);
engine.openSettings();
```

### Upscaling e logs

`Upscaler` e a classe base abstrata: centraliza conversao de textura,
alocacao, limites, cache por frame e invalidacao. `OpenGLUpscaler` e
`MetalFXUpscaler` estendem essa base e implementam apenas configuracao e
execucao do backend. `Upscaling` seleciona e possui a implementacao concreta
(incluindo linear SDL) e e acessivel por `engine.GetUpscaler()`.
A timeline e o MediaPlayer usam esse controlador; nao ha dispatch de
OpenGL/MetalFX dentro da renderizacao generica.

Headers e implementacoes ficam em `media/upscaling`. A base expoe
`bool isAvaliable() const`; as subclasses tambem oferecem a sobrecarga
estatica `isAvaliable(renderer)` para consultar disponibilidade sem construir
um pipeline. O controlador usa essas consultas para selecionar backends.
MetalFX verifica renderer, textura Metal nativa e suporte do dispositivo,
nao apenas o nome do renderer. Cada backend fica em um unico arquivo, sem
header interno compartilhado: `MetalFXUpscaler.mm` contem a classe e o pipeline
MetalFX (Objective-C++ na Apple; fora dela o mesmo arquivo compila como C++ e
informa indisponibilidade), e `MPVCompatibleUpscaler.cpp` contem
`OpenGLUpscaler` e o executor de shaders MPV. Os headers publicos contem
declaracoes, sem implementacoes inline; o carregamento de funcoes OpenGL fica
em `core/OpenGL.hpp` e `core/OpenGL.cpp`.

`UpscaleBackend`: 0 desligado, 1 linear SDL, 2 MPV/OpenGL, 3 MetalFX,
4 automatico (padrao: MetalFX no renderer Metal, MPV no OpenGL, senao linear).
Como no Kotonoha, **a escala e o interruptor**: `factor` 1.0 (padrao) desliga
o upscaling e libera o backend; acima de 1.0 liga com o backend escolhido
(`UpscaleSettings::enabled()`, `Upscaling::activeBackend()`). Um backend salvo
que o renderer atual nao suporta cai para automatico na inicializacao.
O backend MPV executa Anime4K B-S e ArtCNN C4F16/C4F16 DS/C4F32
(`preset`: 0/1/2/3). `shaderFiles` permite a lista de nomes separados por `:`
dentro da pasta do preset. Falhas do MPV sao registradas e usam a copia SDL
com tentativas limitadas, como no motor original. MetalFX requer renderer
Metal, dispositivo compativel e conteudo opaco; nao usa os presets OpenGL.
O fator e finito no intervalo 1..4; 1x desativa o processamento. A escala e
configuravel no launcher, no menu de configuracoes e no F10.

O video da timeline usa o upscaler automaticamente, antes das legendas,
dialogos e HUD: texto/UI nao sao ampliados como imagens. A API publica
`engine.GetUpscaler().process(texture, serial)` tambem atende texturas de
imagens. O serial deve mudar quando os pixels ou a modulacao mudam;
resultados sao reutilizados para frames parados e invalidados por nova
fonte, dimensao, fator ou preset. Texturas usam RAII e limites de tamanho/
memoria. Os backends migrados permitem uma engine/upscaler ativo por
processo; as chamadas de renderizacao pertencem a thread principal SDL.

Jogos externos devem chamar `kotonoha_copy_assets(MeuExecutavel)`
apos criar o target CMake, para copiar shaders/licencas ao lado do executavel.
O exemplo e os testes ja fazem isso.

`core/Log.hpp` captura mensagens SDL de todas as threads, preserva a saida
anterior e mantem as ultimas 1000 entradas. `EngineOptions::logPath`/`--log`
habilitam arquivo em append; falhas de abertura/escrita sao reportadas.
`logPriority` no JSON controla as categorias do motor (prioridades SDL).
F10 oferece **Logs**, com filtro, prioridade minima, auto-scroll e limpeza,
e **Window / renderer / upscaler**, com estado, edicao ao vivo do backend,
preset e escala (slider 1x = desligado) e acesso ao menu de configuracao.

Validacao adicional:

```sh
ctest --test-dir build --output-on-failure
# macOS, com sessao grafica e dispositivo compativel:
SDL_VIDEO_DRIVER=cocoa SDL_AUDIO_DRIVER=dummy ./build/lib/tests/KotonohaInfrastructureTests metal
SDL_VIDEO_DRIVER=cocoa SDL_AUDIO_DRIVER=dummy ./build/lib/tests/KotonohaInfrastructureTests opengl
```

Opcionalmente acrescente um arquivo de video como segundo argumento aos testes
GPU para exercitar decodificacao FFmpeg, novas frames e reabertura da midia.

### Canais de audio nomeados

Cada engine possui um `AudioMixer`. Canais sao identificados por nome
case-sensitive (1..128 bytes, sem controles/NUL), inclusive nomes com espacos.
Todos os players conectados ao mesmo mixer/nome compartilham volume e mute;
canais diferentes sao independentes. O ganho e aplicado ao stream SDL,
incluindo audio ja enfileirado, sem alterar os tempos da timeline.

```cpp
Kotonoha::MediaPlayer music(engine.GetAudio());
music.open("music.ogg", true, "background music");
auto channel = engine.GetAudio().channel("background music");
channel->setVolume(0.5f); // intervalo 0..1
channel->setMuted(true);
music.setChannel("ambience"); // tambem permite trocar durante a reproducao
```

Video usa o mesmo mecanismo para seu audio embutido. Sem nome explicito,
audio usa `"audio"` e video usa `"video"`. Um MediaPlayer sem mixer
explicito possui seu proprio mixer isolado; use `engine.GetAudio()` para
que seus canais aparecam no debug da engine.

F10 > **Audio channels** mostra nomes, numero de streams, slider de volume
e checkbox **Mute**. Mutar preserva o volume escolhido; novos streams e
reaberturas herdam os ajustes do canal. Os canais permanecem registrados
apos parar a midia, para conservar os ajustes durante a sessao (nao sao
salvos em disco). Chamadas de controle pertencem a thread da engine.

### AssetProvider

`AssetProvider` (`core/AssetProvider.hpp`) e a fonte de todos os assets do
motor, acessivel por `engine.GetAssets()`. O padrao e
`FileSystemAssetProvider`: sem raiz, identificadores sao caminhos como antes;
com raiz, sao relativos a ela e nao podem escapar (`..`, absolutos).

Jogos estendem a classe para ler arquivos compactados, imagens de disco ou
formatos proprios, implementando `name`, `contains` e `read` (opcionalmente
`list` e `localPath`). Os decodificadores tambem sao virtuais:
`loadSurface` (BMP/PNG/JPG via SDL; depois qualquer imagem ou o primeiro quadro
de um video pelo FFmpeg), `loadFrame(id, segundos)` e `loadAudio` (WAV via SDL;
depois qualquer audio pelo FFmpeg, em F32 intercalado) podem ser trocados por
formatos do jogo. `texture(renderer, id)` cacheia texturas ate `clearCache()`.

Com escala, `texture(renderer, id, escala, &engine.GetUpscaler())` (cache por
`id@escala`) e `surface(renderer, id, escala, upscaler)` (sem cache) passam a
imagem pelo upscaler configurado quando ele esta ativo e a escala e maior que 1
(fator do backend limitado a 4, ajuste final linear ao tamanho exato); com
backend desligado, escala < 1 ou upscaler nulo usam escala linear. A mesma
operacao existe direto em `Upscaling::scaled(textura|surface, escala)`.

```cpp
Kotonoha::EngineOptions options;
options.assets = std::make_shared<Kotonoha::FileSystemAssetProvider>("assets");
// ou, depois de iniciar: engine.SetAssetProvider(std::make_shared<MeuDisco>(...));
SDL_Texture &bg = engine.GetAssets().texture(engine.GetRenderer(), "bg/sala.png");
```

Videos e audios dos scripts temporizados sao abertos pelo provider:
`MediaPlayer::open(assets, id, ...)` usa `localPath` quando existe e, caso
contrario, decodifica os bytes de `read` em memoria (AVIO do FFmpeg).
Sem tempo externo, `update(renderer)` usa o relogio proprio (respeita pausa);
`openSoundtrack(assets, id)` toca uma faixa separada sincronizada (container
sem audio decodificavel); `render(renderer, area, upscaler, esticar)` desenha
numa area respeitando o SAR; `isOpen`, `duration` e `finished` (video, audio e
trilha drenados) controlam o fim.

`MediaDecoder` (`media/MediaDecoder.hpp`) decodifica bytes inteiros:
`decodePicture(bytes, segundos)` -> surface RGBA32, `decodeAudio(bytes)` ->
`AudioClip` F32 e `duration(bytes)`.

### Discos e texto japones

`DiscImage` (`core/DiscImage.hpp`) le ISO9660 em `.iso` ou `.bin` (setores
2048, 2352/MODE1, 2352/MODE2 e 2336), lista arquivos, busca sem diferenciar
maiusculas (ignora `/` inicial e `;1`) e le arquivos ou faixas com seguranca
entre threads. `DiscImageAssetProvider` expoe o disco como `AssetProvider`
(ids = caminhos ISO) e pode ser estendido com formatos do jogo.

`ConvertEncoding(texto, origem, destino = "UTF-8")` e
`ShiftJisToUtf8(texto)` (`localization/Encoding.hpp`) usam `SDL_iconv`; o
Shift-JIS e lido como CP932 (simbolos NEC e IBM), e sequencias invalidas viram
`?`. A lib forca `SDL_SYSTEM_ICONV` para ter iconv tambem no macOS/Windows.

## Servidor MCP interno

O motor oferece MCP via **stdio** (JSON-RPC 2.0, uma mensagem UTF-8 por linha).
E opt-in: sem `--mcp` nao le stdin nem expoe controle remoto. Nao abre porta de
rede. O cliente inicia uma instancia do jogo e controla essa mesma instancia;
nao se conecta a outra janela ja aberta. Logs ficam em stderr; stdout e
reservado ao protocolo. Fechar stdin encerra a aplicacao normalmente.

```sh
./build/games/example/Kotonoha --mcp --mcp-root .
```

No VS Code, a configuracao em `.vscode/mcp.json` registra `kotonoha`.
Compile primeiro e inicie o servidor na interface MCP do editor. Para testes
sem janela/dispositivo de audio, use `SDL_VIDEO_DRIVER=dummy` e
`SDL_AUDIO_DRIVER=dummy`. Nao ative esses drivers para jogar normalmente.

Ferramentas:

| Ferramenta | Uso |
|------------|-----|
| `engine_state` | Estado, no, modo, segundos, pausas, fala, legenda e escolhas visiveis |
| `list_nodes` | IDs, nomes, tipos, textos e destinos do grafo carregado |
| `start_game` | Iniciar uma nova partida |
| `jump_node` | Ir a `node_id`, com a mesma semantica do inspetor |
| `advance` | Completar/avancar fala tradicional, respeitando bloqueios |
| `choose` | Responder com `index` baseado em zero |
| `submit_text` | Substituir/confirmar `text` no modal ativo, ate 255 bytes UTF-8 |
| `set_paused` | Definir pausa de agente com `paused` |
| `advance_time` | Avancar `seconds` entre 0 e 60 em uma timeline pausada pelo agente |
| `set_debug` | Definir `visible`, `fps` e/ou `node_clock` |
| `save_game` / `load_game` | Salvar/restaurar `path` pelo Runtime do jogo |
| `dump_translation` | Exportar dicionario JSON para `path` |

O cliente faz `initialize`, envia `notifications/initialized` e usa
`tools/list` / `tools/call`. Versoes negociadas: `2024-11-05`, `2025-03-26`,
`2025-06-18` e `2025-11-25`. Exemplo de chamada apos a inicializacao:

```json
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"jump_node","arguments":{"node_id":30}}}
```

Todas as operacoes do motor rodam na thread principal SDL. O transporte le
sem bloquear a espera por linhas completas, limita a entrada a 1 MiB e atende
ate 32 mensagens por frame. Erros de protocolo/argumentos retornam erros
JSON-RPC; falhas de execucao retornam `isError: true` com explicacao e log.
Sem historico carregado, inicie uma partida antes de consultar/pular nos.

`set_paused(true)` seguido de `advance_time` permite testes deterministas.
Perguntas continuam obedecendo aos prazos e politicas do script, inclusive
pausar no prazo sem resposta ou escolher a rota padrao. Uma escolha pode ser
respondida durante a pausa do agente; `advance` exige retomar. Modais de jogo
e o inspetor continuam bloqueando acoes incompativeis. A mudanca de no ao
fim do passo e processada normalmente; tempo excedente nao passa ao no seguinte.

Arquivos do MCP ficam dentro de `--mcp-root` (padrao: diretorio atual).
A pasta deve existir. Caminhos absolutos fora dela, `..` e links simbolicos
que escapem sao rejeitados; nao sao criadas pastas automaticamente. Esse
controle local pressupoe cliente e diretorio confiaveis, nao isolamento contra
outros processos que alterem o sistema de arquivos simultaneamente.
O save usa os metodos virtuais `Runtime::saveGame(path)` e `loadGame(path)`,
implementados pelo exemplo para incluir nome/inventario e nodo/clock.
Outros jogos devem implementa-los; o padrao reporta operacao nao suportada.

Na API C++, use `EngineOptions::enableMCP` e `mcpFileRoot`.

## Scripts temporizados

Para executar o exemplo antes da historia:

```sh
KOTONOHA_TIMELINE=games/example/game-assets/prologue.timeline ./build/games/example/Kotonoha
```

Formato de texto, tempos em segundos:

```text
duration 12
at 0 video 12 "intro.mp4"
at 0 audio 12 "musica.ogg" channel "background music"
at 2 audio 3 "fala.ogg" channel "voice"
at 1 subtitle 3 "Uma legenda."
at 4 dialogue 4 "Narrador" "Uma fala temporizada."
at 8 subtitle 4 "Fim da introducao."
```

Cada comando `at` recebe inicio, tipo, duracao e texto/caminho entre aspas.
`dialogue` recebe duas strings: personagem e fala. Use `\"` e `\\` para
escapar aspas e barras. Comentarios com `#` e linhas vazias sao aceitos.
Caminhos de midia sao relativos ao arquivo de script.

Todos os intervalos sao `[inicio, fim)` e devem caber na duracao total.
Comandos podem estar fora de ordem. Legendas podem se sobrepor; os demais
tipos permitem um evento por trilha por vez. Audio permite uma trilha por
`channel "nome"`: canais distintos podem tocar ao mesmo tempo, mas eventos
do mesmo canal nao podem se sobrepor. Sem o sufixo, preserva a trilha unica
legada `"audio"`. Video inclui seu audio embutido, com canal padrao `"video"`,
e tambem aceita o sufixo `channel "nome"` para controle no mixer.
Todos os streams sao mixados pelo SDL. O video preserva a proporcao.
Ao terminar a midia, o ultimo quadro
permanece ate o fim do evento; eventos mais curtos cortam a reproducao.
Nao ha streaming de rede nem controle de seek manual nesta API; use arquivos
locais. O player faz seek internamente para sincronizar/restaurar a timeline.

Falas temporizadas aparecem completas e entram no backlog. Avanco manual
fica bloqueado durante a timeline. Ao fim, o VNManager segue `nextNodeId`.
Reiniciar uma timeline reinicia suas trilhas; `stop()` libera decodificadores
e audio. Falhas de fonte, sintaxe, decodificacao ou dispositivo sao
reportadas como excecoes e registradas pelos callbacks SDL.

Integracao C++:

```cpp
auto timeline = Kotonoha::TimelineScript::load("intro.timeline");
// ScriptNode(int id, TimelineScript timeline, int nextNodeId)
storyGraph.emplace_back(100, std::move(timeline), 0);
vnManager.loadScript(storyGraph);
vnManager.startScript(100);
```

Tambem e possivel construir `TimelineScript`/`TimelineCue` em C++ e usar
`engine.GetTimeline().play(script)`, `setPaused(bool)` e `stop()`.
A engine atualiza o tempo automaticamente; `update(delta)` aceita segundos
para testes deterministas. O player faz decodificacao local no loop
principal, com buffers limitados: arquivos grandes ou codecs pesados podem
exigir uma futura camada de decodificacao assincrona.

## Dois modos mestre

```cpp
vnManager.loadScript(nodes, Kotonoha::ScriptMode::Standard); // clique / teclado
vnManager.loadScript(nodes, Kotonoha::ScriptMode::Timed);    // relogio
```

`Standard` e o padrao: falas aguardam confirmacao e escolhas aguardam resposta.
Um no `Timeline` pode executar uma sequencia temporal dentro de uma historia
tradicional. `Timed` exige `ScriptNode::duration` positivo para cada no
`Dialogue` ou `Choice`; esses nos seguem o relogio, sem avanco por clique.
Nos `Action` e `Conditional` continuam imediatos, e `End` encerra a historia.
`getMasterMode()` informa o modo do script; `getMode()` informa o modo do no
ativo (uma timeline dentro de Standard reporta Timed).

Falas temporizadas aparecem completas; video, audio, textbox e legendas
obedecem intervalos `[inicio, fim)`. Ao entrar tarde ou restaurar um save,
a midia busca a posicao correspondente em vez de recomecar do zero.
Eventos SDL atualizam o relogio antes de aceitar uma resposta, evitando
respostas fora do prazo entre frames.

### Perguntas temporizadas

```text
duration 10
at 2 choice 4 "Qual caminho?" pause 2 "Caverna" 11 "Montanha" 12
```

Depois da pergunta: politica de timeout, quantidade de alternativas e pares
`"texto" nodo_destino`. Destinos sao IDs do grafo carregado pelo VNManager;
`-1` encerra a historia. Responder segue a rota selecionada. Sem resposta:

- `pause`: padrao C++; congela exatamente no prazo (no exemplo, segundo 6),
  mantendo a pergunta ate uma resposta. Audio/video ativos tambem pausam.
- `continue`: fecha a pergunta e segue a timeline; ao fim segue `nextNodeId`.
- `select ID`: segue uma rota padrao definida pelo jogo.

Exemplo: `at 2 choice 4 "Qual caminho?" select 11 2 "Caverna" 11 "Montanha" 12`.
No modo mestre Timed, configure `ScriptNode::timeout` com
`ChoiceTimeout::Pause`, `Continue` ou `Select`; para Select, defina tambem
`timeoutTargetNodeId`. Em C++, `TimelineCue` aceita os mesmos campos e
`choices`. Uma pergunta nao pausa ao aparecer, apenas ao esgotar seu prazo
quando a politica e Pause. Frames longos nao pulam os timeouts.

## Saves

[VNSave.cpp](src/componentes/VNSave.cpp) grava um formato versionado e substitui
o slot por rename de um arquivo temporario no mesmo diretorio.

```cpp
vnManager.save("slot.vn", dadosDoJogoSerializados);
auto save = Kotonoha::VNSave::read("slot.vn");
// Primeiro restaure os dados de jogo e as variaveis de texto da aplicacao.
vnManager.restoreSave(save);
```

O save inclui o no atual, modo ativo, segundos da timeline, perguntas ja
resolvidas, rota pendente e uma string `gameData` para o estado do jogo.
O grafo deve estar carregado antes de restaurar. Nos tradicionais reabrem no
inicio da fala; timelines retomam o instante salvo, incluindo uma pergunta
pausada no prazo. O save nao serializa callbacks C++ nem o grafo: use a mesma
versao da historia. Um no Action bloqueado e reexecutado ao carregar; mantenha
esses callbacks idempotentes (como a abertura de uma entrada de nome).
Variaveis e inventario sao responsabilidade de `gameData`.

### Multiplos saves (N slots)

O jogo permite criar **quantos saves manuais forem necessarios**, sem um limite
fixo de slots (limitado pelo armazenamento disponivel). No menu de pausa,
**Salvar...** abre a lista: escolha **+ Novo save** para criar `save-1.vn`,
`save-2.vn` etc., sem substituir os anteriores. Escolher um save existente
exige confirmar **Sobrescrever**; ESC cancela a confirmacao. Novos saves usam
o primeiro nome numerado livre. Arquivos de outros slots nao sao alterados.

**Carregar...** no menu de pausa e **Carregar Jogo** no menu principal abrem
a mesma lista de arquivos `.vn`. Use mouse, roda, setas, PgUp/PgDn e Enter.
A lista e rolavel e nao limita a quantidade de arquivos. ESC fecha e retoma
o menu anterior. O seletor e um componente SDL_ttf modal que pausa a historia
e o audio; erros aparecem na propria tela e no log, sem fechar o seletor.
Um save corrompido continua listado e produz erro ao carregar.

Selecione um save existente e use **Renomear [F2]** ou **Excluir [Delete]**
(tambem disponiveis por mouse), tanto na lista de salvar quanto na de carregar.
Renomear abre uma entrada UTF-8; informe o novo nome, com ou sem `.vn`, e
confirme com Enter. ESC cancela. A extensao `.vn` e acrescentada quando
necessario. O arquivo muda de nome sem alterar nodo, clock ou dados do jogo.
Nao e permitido sobrescrever outro arquivo, usar caminhos/separadores,
caracteres de controle ou nomes vazios; erros aparecem no seletor e no log.

Excluir abre uma confirmacao com o nome do arquivo. Enter ou **Excluir**
remove somente esse save; ESC ou **Cancelar** nao altera nada. A exclusao
e permanente, nao vai para a lixeira. A lista e atualizada apos cada operacao,
inclusive quando o ultimo save e excluido. O item **+ Novo save** nao pode
ser renomeado nem excluido.

**F5/F6** continuam salvando/carregando apenas `quicksave.vn`, separado dos
saves manuais. O quicksave tambem aparece na lista quando existe. Por padrao
todos ficam em `SDL_GetPrefPath("Kotonoha", "Kotonoha")`. `KOTONOHA_SAVE`
define o caminho do quicksave; os saves manuais usam o diretorio desse caminho
(ou `.` se nao houver diretorio). Novos saves manuais criam a pasta se necessario.
Nome, inventario, no atual e clock temporal sao restaurados em qualquer slot.
Se renomear/excluir o quicksave pela lista, F5 recria o caminho configurado
e F6 informa erro ate existir um quicksave nesse caminho novamente.

Na biblioteca, `VNSaveSlots(directory).list()` lista arquivos `.vn` ordenados
por comprimento/nome e `nextPath()` fornece o primeiro caminho `save-N.vn`
livre. Use `VNSaveMenu::open(directory, load)` e registre o componente por
ultimo com `Runtime::AddScene` para fornecer a interface a outros jogos.
Ele usa `Runtime::saveGame(path)` / `loadGame(path)` para incluir os dados
especificos do jogo. Nao abra a mesma pasta para gravacao simultanea em
multiplas instancias: a reserva de nomes nao e um lock entre processos.
`VNSaveSlots::rename(name, newName)` retorna o novo caminho e recusa colisoes;
`remove(name)` exclui um arquivo existente. Ambos recebem nomes locais `.vn`
(na origem), nunca caminhos, e recusam links simbolicos.

Via MCP, `save_game` e `load_game` ja aceitam `path`: use caminhos diferentes
para manter N saves (por exemplo `slot-a.vn`, `slot-b.vn`), dentro do MCP root.
O formato de cada arquivo e o mesmo; saves existentes continuam compativeis.

## Traducoes JSON

```sh
./build/games/example/Kotonoha --translation games/example/game-assets/translation.json
# Alternativa:
KOTONOHA_TRANSLATION=translation.json ./build/games/example/Kotonoha
```

Na API: `Engine engine(runtime, EngineOptions{"translation.json"});`.
A traducao e carregada antes da inicializacao da runtime.

```json
{
  "3": {
    "Certo, {player}! Precisamos nos preparar antes da travessia.": "All right, {player}! We must prepare before the crossing."
  },
  "30": {
    "[O vento aumenta]": "[The wind rises]",
    "O vento esconde o sinal. O que fazer?": "The wind hides the signal. What now?",
    "Esperar o sinal do companheiro": "Wait for your companion's signal"
  },
  "nodo": {
    "Narrador": "Narrator"
  }
}
```

O objeto raiz contem dicionarios por ID de no: `"0"`, `"3"`, `"30"` etc.
IDs devem ser inteiros nao negativos em formato decimal canonico, sem zeros
a esquerda. `"nodo"` e um dicionario compartilhado opcional; a traducao do
no especifico tem prioridade. Chaves sao textos originais nao vazios e valores
sao strings; um valor `""` traduz para texto vazio. Textos ausentes mantem o
original. Um arquivo `{}` e um dicionario valido sem traducoes.

Use JSON UTF-8 (BOM opcional), com aspas duplas e escapes JSON padrao:
`\n`, `\r`, `\t`, `\"`, `\\` e `\uXXXX`. `=`, `#`, `;` e colchetes nao
precisam de escape. Espacos nas bordas sao preservados, nao aparados.
Comentarios, virgulas finais, valores que nao sejam strings e caracteres NUL
nao sao aceitos. Evite chaves repetidas: o parser json-c mantem a ultima
ocorrencia. Falhas de sintaxe indicam arquivo e posicao em bytes; erros de
estrutura indicam o no. Uma falha de carga preserva o dicionario anterior.

**Migracao do INI:** o formato antigo nao e mais carregado. Converta `[ID]`
em `"ID": {...}`, `[nodo]` em `"nodo": {...}` e cada `original=traducao`
em `"original": "traducao"`. Remova comentarios e converta os escapes do INI
para os escapes JSON; por exemplo, `\=` vira `=` e `\s` vira um espaco
literal. Nao basta renomear o arquivo. Atualize `--translation` ou
`KOTONOHA_TRANSLATION` para o caminho JSON.

Jogos que nao usam o grafo de nos expoem rotas proprias sobrescrevendo
`Runtime::translationSources()` (rota -> textos originais) e consultam
`engine.GetTranslation().text(rota, original)`; o dump inclui essas rotas.

O inspetor F10 e a ferramenta MCP `dump_translation` exportam JSON legivel,
com nos ordenados por ID, textos deduplicados e traducoes carregadas
preservadas. O fallback compartilhado e resolvido em cada entrada do no.
Arquivos existentes sao substituidos atomicamente, sem caminhos de midia.

A traducao afeta nomes, falas, perguntas, alternativas e legendas do no,
inclusive cues dentro de uma timeline. Caminhos de audio/video nunca sao
traduzidos. Os originais do grafo nao sao alterados. Use
`vnManager.setVariable("player", nome)` para expandir `{player}` **depois**
da traducao. Variaveis nao expandem outras variaveis recursivamente.

Textbox e legenda sao canais distintos: `Dialogue` usa VNTextBox e backlog;
`Subtitle` usa um overlay temporal independente, nao entra no backlog nem
substitui o texto da textbox.

### Traducoes de Zero no Tsukaima e Snow Radish

`translations/` (ignorado pelo git, pois contem o texto dos jogos) guarda
`ja.json` (dump original), `en.json` e `pt-BR.json` de cada jogo. Os arquivos
`en`/`pt-BR` ja trazem nomes de personagens, menus, locais e mensagens do
sistema; as demais falas mantem o original como valor ate serem traduzidas.

```sh
ZNT=./build/games/zero-no-tsukaima/ZeroNoTsukaima
$ZNT --no-config --dump-translation translations/zero-no-tsukaima/ja.json
$ZNT --translation translations/zero-no-tsukaima/pt-BR.json   # jogar traduzido
# Atualiza o arquivo com rotas novas sem perder o que ja foi traduzido:
$ZNT --no-config --translation translations/zero-no-tsukaima/en.json \
     --dump-translation translations/zero-no-tsukaima/en.json
```

O mesmo vale para `SnowRadish` com `translations/snow-radish-vacation/`.

## HiDPI e elementos fora da novel

A novel usa uma area logica 1280x720 com letterbox, independente da resolucao
fisica. A janela tem `SDL_WINDOW_HIGH_PIXEL_DENSITY`; a densidade e recalculada
a cada frame/evento, inclusive apos resize ou mudanca de monitor. SDL_ttf
rasteriza fontes na densidade efetiva de cada area e mantem o tamanho logico,
sem simplesmente ampliar uma textura de texto de baixa resolucao.

Cada Scene escolhe seu espaco:

```cpp
Kotonoha::RenderSpace renderSpace() const override {
  return Kotonoha::RenderSpace::Screen;
}
```

O padrao e `Novel`. Cenas `Screen` sao desenhadas depois da novel, na janela
inteira, inclusive sobre as barras. Use `engine.screenSize()` ou
`UI::viewSize(renderer)` para ancorar elementos no canto direito/inferior.
Coordenadas de Screen sao unidades da janela (pontos), nao pixels fisicos.
O HUD de FPS/no/clock e renderizado diretamente pelo motor em Screen.
O FPS fica em `(10, 10)` da janela, nao no canto da novel, mesmo apos o jogo
limpar ou trocar suas cenas.
O argumento `scale` de render informa pixels fisicos por unidade logica.

O inspetor ImGui usa coordenadas da janela para layout e mouse. A escala
do framebuffer e calculada a cada frame pela saida fisica real do renderer,
e aplicada uma unica vez via `SDL_SetRenderScale`, com a apresentacao logica
desativada. Isso mantem geometria, recortes e cliques alinhados em Retina,
inclusive ao redimensionar ou trocar de monitor. Fontes usam a densidade do
framebuffer; escala de conteudo adicional ajusta o estilo sem duplicar o
tamanho em monitores que ja usam pontos.

Para cenarios, use `RenderLayer::Background` na cena: ela sera desenhada antes
do video e das legendas. Componentes de dialogo continuam em
`RenderLayer::Foreground`. Assim o cenario nunca encobre legendas/cinematicas.

Eventos tambem usam o espaco de cada cena. `handleEvent` pode retornar true
para consumir a entrada, e `capturesInput()` permite modais. Cliques nas
barras nao avancam a novel, mas podem acionar elementos Screen. A area de
entrada IME e convertida para coordenadas da janela apos resize/HiDPI.
