# School Days HQ no Android — launcher em retrato

O launcher do School Days abre em retrato. Na primeira abertura, selecione a pasta que contém `Packs`, `game-assets` e `translations`. O Android concede acesso de leitura a essa árvore. **Desconectar pasta** libera a seleção sem alterar os arquivos. O botão Voltar do Android fecha as opções antes de sair do launcher.

## Referência Windows e entrada Android

O launcher Windows mostra o arquivo do usuário `SchoolDaysHQ-Launcher.png`, que já contém o logotipo. O cabeçalho das opções usa outro recorte da mesma textura. O ImGui desenha os botões e painéis. O controle de música usa o loop definido por `TitleBGM` em `STARTSCRIPT.INI`. As ações principais são Play, Options e Quit. As opções do Windows incluem Display, Graphics, Audio, General, Language e Game Files. As configurações do motor e as preferências retail têm armazenamento separado. [Implementação Windows](../../src/SchoolDaysLauncher.cpp)

O flavor Android usa o mesmo cabeçalho azul-claro, texto azul-escuro e ações principais. O launcher procura o PNG em `game-assets/launcher-art/`, `assets/` e `launcher-art/`, nessa ordem, dentro da árvore selecionada. A imagem opcional ocupa a largura disponível e preserva a proporção. O cabeçalho em texto cobre a primeira abertura, a falta da imagem e falhas de decodificação. A permissão de redistribuir a arte local do Windows neste APK permanece desconhecida.

Iniciar jogo abre a configuração de arquivos quando a origem ainda não pode ser usada. Arquivos do jogo verifica por metadados o nome da rota KTRF e os nomes dos 29 pacotes HQ mais L×H. Idiomas lista os pares completos de JSONs HQ/L×H encontrados em `translations/jsons`. Dentro de `Packs`, o launcher reconhece variações de maiúsculas e minúsculas como `System.gpk`. Os nomes das pastas, da rota e das traduções continuam com a grafia exata. Falha do provedor ou perda de permissão mantém acessíveis a seleção e a nova verificação. Mesmo uma árvore completa ainda depende da montagem SAF nativa para jogar. Uma instalação privativa existente em `game_assets` com a rota e os pacotes pode seguir para o bootstrap anterior mediante toque em Iniciar jogo. Uma instalação de QA separada exercitou essa passagem com pacotes sintéticos de um byte. O parser nativo os rejeitou. Duas tentativas consecutivas de Iniciar jogo voltaram ao launcher com erro localizado. O SDL consulta os metadados de recriação da atividade e de Voltar no nível da aplicação.

## Motor e armazenamento

- O Java Android genérico cuida da seleção, da concessão persistida de leitura, das consultas aos diretórios e do aviso de inicialização na tela. A organização da pasta e os pares de idiomas pertencem ao conjunto de fontes Android do jogo.
- A `GameActivity` empacotada mantém o runtime C++ compartilhado. O launcher e o jogo têm políticas de orientação independentes. No manifesto, o gameplay segue a configuração do aparelho. Orientação efetiva no SDL, preservação da proporção, entrada, superfícies de vídeo e áudio ainda exigem QA do gameplay.
- `SDLFileSource`, `FileHandle`, `IMount` e o suporte do SDL Android a `content://` e descritores são a base para a futura montagem. O parser nativo ainda não recebe caminhos SAF. A rota KTRF externa, os overlays e os pares de JSON ainda exigem resolução nativa e validação de conteúdo.
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

## POCO X7 Pro — S0 sintético

Dois novos ciclos de 1 GiB passaram na API 36 com o provedor de armazenamento local, incluindo encerramento do processo e reabertura. Cada linha abaixo mostra o percentil 95 em milissegundos para leituras individuais de 1 MiB. Os três padrões compararam iguais nos 1024 blocos de cada ciclo. O histórico persistido reproduziu 12 registros únicos após reabrir.

| Ordem de leitura | SAF inicial / reaberto | Privativo inicial / reaberto |
|---|---:|---:|
| Sequencial | 1,125 / 0,469 | 1,337 / 0,473 |
| Reversa | 0,795 / 0,500 | 0,806 / 0,539 |
| Saltos | 0,540 / 0,508 | 0,589 / 0,506 |

A tentativa anterior pelo provedor Downloads retornou `different_document_refused`. Nos ciclos aprovados foi usada a raiz do armazenamento do aparelho nos dois seletores. A leitura direta do descritor funcionou. A reabertura por `/proc/self/fd` retornou errno 13. O aplicativo S0 separado mantém o documento sintético, a referência privativa e as medições para o ciclo pendente após reiniciar o aparelho. Os dois arquivos ocupam cerca de 2 GiB. A decisão de armazenamento e o acesso a GPKs reais continuam bloqueados.

## Critérios de aceite pendentes

1. Completar a evidência S0 após reiniciar o aparelho e revisá-la antes de escolher a estratégia de armazenamento.
2. Integrar a origem de leitura autorizada à montagem existente, ao validador da rota, à precedência de overlay e aos perfis de tradução. Preservar o comportamento de rota DEV/DIST e distinguir as falhas de validação nativa de cada recurso.
3. Vincular as demais opções Windows e a seleção de idioma aos modelos existentes. Verificar toque contextual com `MobileTouchAdapter`, `TouchPointer` e as coordenadas originais dos hotspots.
4. Exercitar Route Map, BackLog, Save/Load, MenuBar, escolhas, mídia, pausa/retomada e recriação da GPU em perfis isolados. Medir FPS, memória e legibilidade de legendas em hardware representativo.
