# School Days HQ no Android — launcher em retrato

O launcher do School Days abre em retrato. Na primeira abertura, selecione a pasta que contém `Packs`, `game-assets` e `translations`. O Android concede acesso de leitura a essa árvore. **Desconectar pasta** libera a seleção sem alterar os arquivos. O botão Voltar do Android fecha as opções antes de sair do launcher.

## Referência Windows e entrada Android

O launcher Windows mostra o arquivo do usuário `SchoolDaysHQ-Launcher.png`, que já contém o logotipo. O cabeçalho das opções usa outro recorte da mesma textura. O ImGui desenha os botões e painéis. O controle de música usa o loop definido por `TitleBGM` em `STARTSCRIPT.INI`. As ações principais são Play, Options e Quit. As opções do Windows incluem Display, Graphics, Audio, General, Language e Game Files. As configurações do motor e as preferências retail têm armazenamento separado. [Implementação Windows](../../src/SchoolDaysLauncher.cpp)

O flavor Android usa o mesmo cabeçalho azul-claro, texto azul-escuro e ações principais. O launcher procura o PNG em `game-assets/launcher-art/`, `assets/` e `launcher-art/`, nessa ordem, dentro da árvore selecionada. A imagem opcional ocupa a largura disponível e preserva a proporção. O cabeçalho em texto cobre a primeira abertura, a falta da imagem e falhas de decodificação. A permissão de redistribuir a arte local do Windows neste APK permanece desconhecida.

Iniciar jogo abre a configuração de arquivos quando a origem ainda não pode ser usada. Arquivos do jogo verifica por metadados o nome da rota KTRF e os nomes dos 29 pacotes HQ mais L×H. Idiomas lista os pares completos de JSONs HQ/L×H encontrados em `translations/jsons`. Dentro de `Packs`, o launcher reconhece variações de maiúsculas e minúsculas como `System.gpk`. Os nomes das pastas, da rota e das traduções continuam com a grafia exata. Falha do provedor ou perda de permissão mantém acessíveis a seleção e a nova verificação. Com a pasta completa, escolha um idioma detectado e toque em Iniciar jogo. O launcher memoriza o idioma e solicita uma nova escolha se o par estiver ausente em outra pasta. Uma instalação privativa existente em `game_assets` com a rota e os pacotes pode seguir para o bootstrap anterior mediante toque em Iniciar jogo. Uma instalação de QA separada exercitou essa passagem com pacotes sintéticos de um byte. O parser nativo os rejeitou. Duas tentativas consecutivas de Iniciar jogo voltaram ao launcher com erro localizado. O SDL consulta os metadados de recriação da atividade e de Voltar no nível da aplicação.

## Motor e armazenamento

- O Java Android genérico cuida da seleção, da concessão persistida de leitura, das consultas aos diretórios e do aviso de inicialização na tela. A organização da pasta e os pares de idiomas pertencem ao conjunto de fontes Android do jogo.
- A `GameActivity` empacotada mantém o runtime C++ compartilhado. O launcher e o jogo têm políticas de orientação independentes. No manifesto, o gameplay segue a configuração do aparelho. Orientação efetiva no SDL, preservação da proporção, entrada, superfícies de vídeo e áudio ainda exigem QA do gameplay.
- Ao tocar em Iniciar jogo, o motor recebe um `IMount` somente leitura. A montagem associa os nomes canônicos dos pacotes às URIs fornecidas pelo provedor. O SDL abre um `FileHandle` independente com acesso aleatório para o índice, as entradas decodificadas e os fluxos de mídia. O parser original valida os 30 pacotes. Uma permissão revogada, tamanho alterado ou origem sem acesso aleatório interrompe a leitura sem importar o pacote.
- Os componentes que exigem um arquivo físico usam uma pasta de cache privativa por abertura do jogo. Ela contém o KTRF do usuário, os JSONs HQ e L×H do idioma selecionado, um catálogo de URIs e os arquivos encontrados entre os seis suportados em `overlay/System/Option/`. O launcher encaminha o overlay ao resolvedor compartilhado e descarta a sessão no retorno. Um processo interrompido pode deixar temporários no cache do Android. Os saves continuam nos diretórios privativos existentes.
- O perfil mobile inicial usa `renderScale=0.75`, Linear e fator 1. O alvo de renderização escalado já usa amostragem linear. As configurações persistidas têm precedência. Orçamentos de cache e desempenho em aparelhos de 4 GB dependem de medições.
- O jogo preserva os caminhos existentes de `StoragePaths` e `FileResolver`. A preferência do launcher guarda somente a URI da árvore no armazenamento privativo da aplicação. Saves ficam fora do SAF.
- A lista de assets permitidos no APK inclui os shaders do motor e seus avisos. Ficam de fora o splash opcional do motor, o PNG do launcher, a rota do usuário, as traduções e os pacotes do jogo. A árvore de dependências A1 contém fontconfig e libunibreak. O APK não contém `fonts.conf` nem TTF de produção. `SubtitleScene` registra no libass as fontes fornecidas por `FileResolver`. Quando falta a fonte padrão do motor, o Android resolve uma fonte japonesa do sistema com as APIs de fontes. O launcher usa as fontes da interface Android. A cobertura de glifos das legendas e a face usada em coleções ainda exigem QA visual do gameplay.

## Reprodução com dados sintéticos

1. Na raiz do repositório, execute `python tools/android/make_school_days_setup_fixture.py build/SchoolDaysSetupQA`. Use um diretório novo a cada execução. Ele contém um PNG 2:1, um arquivo com o nome da rota e JSONs sintéticos, adequados somente para testes de metadados.
2. Em `buildsystems/android`, execute `gradle :app:assembleSchoolDaysDebug -Pabis=arm64-v8a -PbundleGameAssets=false --no-daemon --console=plain`. Use o NDK fixado e o executável pkgconf do host utilizado pelo ambiente vcpkg.
3. Na raiz, execute `python tools/android/check_school_days_apk.py <apk>` e `zipalign -c -P 16 4 <apk>`. Instale com `adb install -r <apk>`.
4. Coloque apenas a nova fixture em uma nova pasta de teste no aparelho. Abra o launcher, cancele o seletor uma vez e depois selecione a fixture. Verifique o aviso de Packs ausente, a imagem sem deformação e somente `pt-BR` em Idiomas. O exemplo em inglês não tem o par L×H.
   Somente na pasta sintética, adicione um arquivo de um byte chamado `Packs/System.gpk` e toque em **Verificar novamente**. A lista de ausentes deve incluir `Script.GPK` e omitir `System.GPK`. Com um `System.gpk` de zero bytes, ambos devem aparecer.
5. Encerre o processo e reabra o aplicativo. Confira a mesma seleção, use Desconectar pasta e abra o seletor novamente. Verifique Voltar do Android e Sair. Mantenha os dados reais do jogo fora desse procedimento.

Para testar a falha nativa, acrescente `-I ../../tools/android/school-days-setup-qa.init.gradle` ao mesmo comando Gradle em `buildsystems/android`. O resultado é `me.hirameki.kotonoha.schooldays.setupqa`, com armazenamento separado. Nesse perfil de QA, use uma rota fictícia e arquivos de um byte com os nomes esperados dentro da pasta privativa `game_assets/Packs`. Toque em Iniciar jogo duas vezes, retornando das opções entre as tentativas. Cada falha nativa de validação deve voltar ao launcher em retrato. As duas tentativas passaram no POCO. Um teste dentro do aplicativo também abriu a fonte japonesa escolhida pelo sistema e verificou o cabeçalho sfnt (32.355.424 bytes). A renderização de legendas ainda exige QA.

Para o teste da montagem nativa, acrescente `--pack-writer <SchoolDaysGpkStreamingTest>` ao gerador de arquivos sintéticos. Selecione a nova pasta no aplicativo de QA, escolha `pt-BR`, volte e toque em Iniciar jogo. No POCO, o motor compartilhado montou os 29 pacotes HQ e o L×H. Em seguida, rejeitou o KTRF propositalmente curto e retornou ao launcher. Uma sonda temporária no aparelho leu as 28 entradas sintéticas L×H e uma entrada de BGM em quatro posições cada, e decodificou a entrada comprimida de Movie00. A mesma sonda resolveu o PNG sintético de Display Options pelo overlay nativo, conferiu os dois JSONs selecionados e verificou a ausência de GPKs no cache da sessão. Um pacote sintético truncado produziu o aviso específico de pacotes no launcher. Os APKs finais excluem a sonda. O teste de streaming no Windows também lê por montagem com os pacotes fora da pasta física do jogo.

## POCO X7 Pro — S0 sintético

Os três ciclos passaram na API 36 com o provedor de armazenamento local. Após o reinício do aparelho, o aplicativo S0 reabriu o mesmo documento sintético de 1 GiB com a permissão de leitura existente. Os 1024 blocos coincidiram com a referência privativa em cada passagem. O histórico pós-reinício continha as 12 medições anteriores na abertura e reproduziu as 18 após o reteste. A tabela mostra o percentil 95 por leitura de 1 MiB, em milissegundos. [Medições por ciclo](s0-poco-api36.csv)

| Ordem de leitura | SAF inicial / app reaberto / aparelho reiniciado | Privativo inicial / app reaberto / aparelho reiniciado |
|---|---:|---:|
| Sequencial | 1,125 / 0,469 / 2,479 | 1,337 / 0,473 / 2,062 |
| Reversa | 0,795 / 0,500 / 2,063 | 0,806 / 0,539 / 1,987 |
| Saltos | 0,540 / 0,508 / 0,630 | 0,589 / 0,506 / 0,673 |

A tentativa anterior pelo provedor Downloads retornou `different_document_refused`. Nos ciclos aprovados foi usada a raiz do armazenamento do aparelho nos dois seletores. O provedor permitiu leitura direta pelo descritor com acesso aleatório nos três ciclos. A reabertura por `/proc/self/fd` retornou errno 13. O aplicativo S0 mantém os dois arquivos sintéticos (cerca de 2 GiB no total). Esses resultados justificam testar a leitura no lugar pelo sistema de montagem do motor com esse provedor. Outros provedores e a execução nativa do jogo ainda precisam de testes.

## Critérios de aceite pendentes

1. Obter autorização separada antes de testar GPKs reais.
2. Validar no aparelho uma rota completa autorizada, cobertura da tradução, precedência visual do overlay, renderer e reprodução audiovisual.
3. Vincular as opções restantes do Windows às configurações compartilhadas. Verificar o toque contextual com `MobileTouchAdapter`, `TouchPointer` e os hotspots originais.
4. Exercitar Route Map, BackLog, Save/Load, MenuBar, escolhas, mídia, pausa/retomada e recriação da GPU em perfis isolados. Medir FPS, memória e legibilidade de legendas em hardware representativo.
