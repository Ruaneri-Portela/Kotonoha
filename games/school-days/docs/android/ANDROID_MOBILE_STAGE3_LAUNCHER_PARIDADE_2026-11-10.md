# Launcher School Days HQ — matriz PC → Android (Etapa 3)

Escopo: launcher móvel, sem executar o jogo nem instalar no telefone. A implementação Java em `BootstrapActivity` **substitui o fluxo de dois launchers**; `GameActivity` abre diretamente a runtime SDL quando PLAY é acionado.

| Opção/função na launcher do PC | Android | Justificativa / implementação |
|---|---|---|
| Arte de fundo `SchoolDaysHQ-Launcher.png` | Adaptado | Copiada ao APK de `games/school-days/launcher-art`, exibida como fundo `CENTER_CROP` sob painel escuro responsivo |
| Seleção da pasta original / Packs | Adaptado | `ACTION_OPEN_DOCUMENT_TREE` com concessão persistível de leitura; aceita `SchoolDaysHQ` ou `Packs` e valida os 30 nomes; o motor ainda exige arquivos de disco, portanto importa os GPKs para armazenamento privado sem modificar os arquivos de origem |
| Lista de arquivos faltantes | Adaptado | Feedback `N/30` com nomes ausentes/sem conteúdo; o carregador C++ é o validador final |
| Idioma HQ em GPK | Adaptado | EN, PT-BR, ES, ZH compatível aparecem quando os arquivos de base + sete ORS L×H associados estão presentes; leitura nativa verifica a semântica dos GPKs |
| Idioma em JSON | Adaptado | Lista `translations/jsons` sem `-lxh.json` nem `-reviewed-identities.json` como opções base; associa `<base>-lxh.json` correspondente; ES V4b admite sidecar embutido apenas depois da validação C++ |
| Persistir idioma e modo | Adaptado | Perfis `translation-pack.txt`, `language.txt`, `settings.json` no diretório Android do aplicativo; idioma ausente não reverte para inglês |
| Native English retail `Script.GPK` | Adaptado | Usa `Packs/Script.GPK`, nunca duplicado dentro do APK; necessita texto L×H da mesma seleção |
| Play / Start | Adaptado | Botão grande; habilita após assets, Packs e perfil de idioma presentes; a runtime valida cobertura antes de tocar cena |
| Display / Graphics | Em andamento (Etapa 5) | Botão já presente; render scale inicial, restante das opções serão próprias de smartphone |
| Audio / General | Em andamento | Persistência nativa mantida; controles móveis apropriados ainda requerem revisão funcional |
| Windowed / exclusive fullscreen / monitor | Não aplicável | Android gerencia Surface, cutout e display físico |
| ISO, caminhos manuais Windows e seletor de pasta desktop | Não aplicável | SAF Android usa `content://`; sem digitação de caminhos do PC |
| Advanced / QA / Debug | Não aplicável em dist | A edição comercial compila com `KOTONOHA_DISTRIBUTION_BUILD=ON`; opções extras só podem existir na DEV |
| Animações por hover / atalhos de teclado | Substituído | Alvos Android em dp e ações por toque; matriz detalhada das telas do jogo depende da Etapa 6 |

**Estado de verificação**: Gradle `assembleSchoolDaysDev` passou após ajustes na launcher. Verificação ZIP do APK confirmou `assets/launcher-art/SchoolDaysHQ-Launcher.png`, rota KTRF e estilos; nenhum GPK original foi empacotado. O uso efetivo da imagem, touch, SAF, orientação 180° e jogabilidade ainda não foram testados no dispositivo/emulador.

**Restrição de armazenamento**: por usar GPKs que exigem caminho físico, o importador copia os 11,36 GiB para espaço privado do aplicativo; permissão SAF é conservada para reler/selecionar a pasta fonte. Não substitui nem apaga a pasta do jogador, mas exige espaço adicional.
