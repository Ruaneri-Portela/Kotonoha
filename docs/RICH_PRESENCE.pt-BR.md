# Discord Rich Presence opcional

[English (US)](RICH_PRESENCE.md) | **Portugu?s (Brasil)**

O alvo est?tico CMake `kotonoha_rich_presence` ? um transporte reutiliz?vel e opcional. Não é vinculado ao motor compartilhado; jogos consumidores podem vincular o alvo mesmo com RPC desativado, quando será usado o transporte nulo. N?o inclui login, token, assets de jogos ou bin?rios do SDK do Discord.

## Compila??o e uso

Configure uma build desktop com `-DKOTONOHA_BUILD_RICH_PRESENCE=ON`. Vincule `kotonoha_rich_presence` ao alvo do jogo consumidor e inclua `<Kotonoha/integrations/RichPresence.hpp>`. Windows usa named pipe; Linux e macOS usam socket Unix local; Android usa o SDK Social opcional. RPC ? desativado em **WebAssembly e iOS**, onde `KOTONOHA_HAS_DISCORD_RPC` ? 0. No macOS, o launcher, o launcher core usado pelo host separado e o aplicativo consumidor compilam `DiscordUnix.cpp` uma vez por alvo; no iOS é usado o transporte nulo. O Discord desktop deve estar em execução na mesma sessão de usuário. A busca também usa o diretório temporário por usuário do Darwin, inclusive ao iniciar pelo Finder.

O jogo controla sua prefer?ncia de ativa??o, o Application ID do Discord e uma pol?tica de atividade sanitizada. Verifique `platformPresenceAvailable()` antes de criar uma sess?o. Crie um transporte com `discordPlatform(applicationId, largeImage, applicationName)` e depois `RichPresence` com o transporte, hor?rio Unix de in?cio em segundos, `RichPresence::Timing{}` e o nome de exibi??o do jogo. O nome ? sempre publicado; `setDetails()` acrescenta texto curado e `setState()` seleciona Menu/Carregando/Jogando. Destrua a sess?o antes de encerrar o host. Nunca publique di?logos, escolhas do jogador, caminhos de arquivos, credenciais ou outros dados sens?veis.

O menu do sistema (Configura?es) e o overlay de debug exp?em um interruptor **Discord RPC** ligado/desligado. `setRichPresenceEnabled(false)` desconecta todas as sess?es imediatamente e ? compartilhado por todos os jogos do processo.

No macOS, a folha **Settings & Library Sources** do launcher nativo também traz o interruptor **Discord Rich Presence**. Ele grava a preferência compartilhada da biblioteca no diretório Documents do launcher. O `Kotonoha::Engine` possui a única sessão `EnginePresence` e a atualiza a cada quadro com `presenceDescriptor()` e `presenceDetails()` do jogo em execução; os jogos apenas fornecem dados sanitizados. O School Days HQ fornece o Application ID e os detalhes da atividade, de modo que o executável isolado e o runtime hospedado pelo launcher publicam a mesma presença.

## Adaptador Android (requer integra??o do host)

O transporte Android opcional exige o Discord Social SDK oficial, obtido separadamente. Extraia o m?dulo Prefab do AAR release para um diret?rio contendo `prefab/modules/discord_partner_sdk` e configure com `-DKOTONOHA_BUILD_RICH_PRESENCE=ON`, `-DKOTONOHA_ANDROID_DISCORD_RPC=ON` e `-DKOTONOHA_ANDROID_DISCORD_SDK_DIR=<extracted-root>`. O m?dulo deve conter os headers e a biblioteca da ABI selecionada. O aplicativo Android consumidor deve empacotar o AAR correspondente, inicializar o SDK com sua Activity ap?s a ativa??o expl?cita pelo usu?rio e chamar `setAndroidPresenceReady(true)` por sua pr?pria integra??o JNI; deve destruir as sess?es antes de liberar o contexto do SDK. O adaptador n?o autentica nem solicita acesso a voz. O empacotamento Android Gradle, as prefer?ncias da launcher e a pol?tica de atividade espec?fica de cada jogo n?o s?o conectados por esta altera??o de infraestrutura.

## Testes

Compile `KotonohaRichPresenceTest` com `BUILD_TESTING=ON` e `KOTONOHA_BUILD_RICH_PRESENCE=ON`, depois execute `ctest --test-dir build -R '^Kotonoha.RichPresence$' --output-on-failure`. O teste utiliza um transporte simulado e não se conecta ao Discord. Em macOS e Linux, compile também `KotonohaDiscordUnixTest` e execute `ctest --test-dir build -R '^Kotonoha\.(RichPresence|DiscordUnix)
 --output-on-failure`. O servidor IPC simulado valida fallback de socket, mensagens fragmentadas, handshake, atividade, ping/pong e limpeza. Ambos os testes são executados no GitHub Actions.
