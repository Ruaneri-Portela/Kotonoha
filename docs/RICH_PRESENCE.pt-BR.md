# Discord Rich Presence opcional

[English (US)](RICH_PRESENCE.md) | **Portugu?s (Brasil)**

O alvo est?tico CMake `kotonoha_rich_presence` ? um transporte reutiliz?vel e opcional. Por padr?o, n?o ? vinculado ao motor compartilhado nem a qualquer jogo. N?o inclui login, token, assets de jogos ou bin?rios do SDK do Discord.

## Compila??o e uso

Configure uma build desktop com `-DKOTONOHA_BUILD_RICH_PRESENCE=ON`. Vincule `kotonoha_rich_presence` ao alvo do jogo consumidor e inclua `<Kotonoha/integrations/RichPresence.hpp>`. O transporte Windows utiliza IPC local por named pipes; outros hosts desktop utilizam um transporte nulo. WebAssembly n?o ? suportado. Os alvos nativos Xcode permanecem inalterados.

O jogo controla sua prefer?ncia de ativa??o, o Application ID do Discord e uma pol?tica de atividade sanitizada. Verifique `platformPresenceAvailable()` antes de criar uma sess?o. Crie um transporte com `discordPlatform(applicationId, largeImage, applicationName)` e depois `RichPresence` com o transporte, hor?rio Unix de in?cio em segundos e `RichPresence::Timing{}`. Chame `setDetails()` apenas com texto p?blico e selecionado. Destrua a sess?o antes de encerrar o host. Nunca publique di?logos, escolhas do jogador, caminhos de arquivos, credenciais ou outros dados sens?veis.

## Adaptador Android (requer integra??o do host)

O transporte Android opcional exige o Discord Social SDK oficial, obtido separadamente. Extraia o m?dulo Prefab do AAR release para um diret?rio contendo `prefab/modules/discord_partner_sdk` e configure com `-DKOTONOHA_BUILD_RICH_PRESENCE=ON`, `-DKOTONOHA_ANDROID_DISCORD_RPC=ON` e `-DKOTONOHA_ANDROID_DISCORD_SDK_DIR=<extracted-root>`. O m?dulo deve conter os headers e a biblioteca da ABI selecionada. O aplicativo Android consumidor deve empacotar o AAR correspondente, inicializar o SDK com sua Activity ap?s a ativa??o expl?cita pelo usu?rio e chamar `setAndroidPresenceReady(true)` por sua pr?pria integra??o JNI; deve destruir as sess?es antes de liberar o contexto do SDK. O adaptador n?o autentica nem solicita acesso a voz. O empacotamento Android Gradle, as prefer?ncias da launcher e a pol?tica de atividade espec?fica de cada jogo n?o s?o conectados por esta altera??o de infraestrutura.

## Testes

Compile `KotonohaRichPresenceTest` com `BUILD_TESTING=ON` e `KOTONOHA_BUILD_RICH_PRESENCE=ON`, depois execute `ctest --test-dir build -R '^Kotonoha.RichPresence$' --output-on-failure`. O teste utiliza um transporte simulado; n?o se conecta ao Discord.
