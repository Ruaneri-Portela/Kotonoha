# LucaSystem

[English (US)](README.md) | **Português (Brasil)**

Interpretação nativa de PAK/bytecode e renderização de imagem/áudio/texto testadas com AIR.

**Estado:** O suporte documentado é específico do AIR; não implica compatibilidade equivalente com outros títulos LucaSystem.

## Compilação

```sh
cmake -S . -B build -DKOTONOHA_BUILD_LUCA_SYSTEM=ON
cmake --build build --target LucaSystem
```

## Dados do jogo

Use PAK originais (incluindo PARAM.PAK e SCRIPT.PAK); AIR é o título de referência.
Use o launcher ou passe `--luca-system /caminho/para/dados` ao executável. Nunca versione dados comerciais.

## Importação e reempacotamento de PAK

Em um build nativo, compile a ferramenta host `LucaPak`:

```sh
cmake --build build --target LucaPak
build/bin/LucaPak list SCRIPT.PAK
build/bin/LucaPak repack SCRIPT.PAK SCRIPT-patched.PAK name:seen170 replacement.bin
build/bin/LucaPak repack VOICE.PAK VOICE-patched.PAK id:1 replacement.ogg
```

`list` mostra o ID, o tamanho em bytes e o nome. Passe um ou mais pares de
seletor e arquivo para substituir entradas, ou omita os pares para
reempacotar sem substituições. A ferramenta preserva os nomes, os IDs, a
ordem e o alinhamento de blocos e grava um novo arquivo. Ela não adiciona
nem renomeia entradas e não interpreta os dados importados. Mantenha os
arquivos comerciais de origem e os PAKs reempacotados apenas localmente.

### Importação de diálogos do AIR

Use `LucaInspect` com o opcode `36` para localizar o offset decimal original
do `MESSAGE` que você quer alterar. Salve o texto em inglês como UTF-8 sem
BOM. Por exemplo, com um diretório `overlay` já existente:

```sh
cmake --build build --target LucaInspect LucaPak
build/bin/LucaInspect /path/to/AIR/files seen170 36
build/bin/LucaPak patch-air /path/to/AIR/files/SCRIPT.PAK overlay/SCRIPT.PAK name:seen170 2202 dialogue.txt
```

Passe `--asset-overlay overlay` ao executar o port com
`/path/to/AIR/files`. Para importar várias alterações de uma vez, repita os
argumentos de seletor, offset e arquivo usando os offsets do PAK de origem.
A ferramenta preserva os IDs de voz e os outros idiomas, ajusta os offsets
dos saltos conhecidos dentro de um cenário e entre cenários e gera um novo
`SCRIPT.PAK`. O comando é voltado aos diálogos do AIR da Steam no Kotonoha;
ele não altera escolhas, texto de histórico nem tabelas auxiliares do motor
original.

### Edição de fontes bitmap

`LucaPak edit-glyph` substitui a máscara alfa e as métricas de um glifo que
já existe em um atlas CZ2 de 8 bits. Para a fonte de diálogo do AIR da Steam,
use um PNG de 28 × 28 com transparência. O diretório de saída ainda não deve
existir:

```sh
build/bin/LucaPak edit-glyph /path/to/AIR/files/font_win32_1280/FONT_GOTHIC1.PAK name:ゴシック27 /path/to/AIR/files/font_win32_1280/FONT__INFO.PAK name:info27 font-overlay U+0041 glyph-A.png 3 4 24 32
```

Os quatro números definem os deslocamentos X e Y com sinal, a largura de
desenho e o avanço, em pixels. Um avanço de zero usa a largura de desenho.
O comando mantém a paleta original, recodifica apenas os blocos CZ2
alterados e reempacota os arquivos. Use `--asset-overlay font-overlay` com
`/path/to/AIR/files` como diretório de dados do port. Os PAKs de origem
permanecem intactos. O comando não adiciona células; caracteres que
compartilham uma célula também compartilham sua imagem e métricas de desenho.

Use `add-glyph` para acrescentar uma célula para um caractere BMP ainda não
mapeado. Por exemplo, para adicionar `U+E000`:

```sh
build/bin/LucaPak add-glyph /path/to/AIR/files/font_win32_1280/FONT_GOTHIC1.PAK name:ゴシック27 /path/to/AIR/files/font_win32_1280/FONT__INFO.PAK name:info27 font-overlay U+E000 glyph-new.png 3 4 24 32
```

O comando atualiza o índice Unicode e as métricas, amplia o atlas se
necessário e preserva as posições dos glifos e os índices de paleta
existentes. Para adicionar outro glifo à mesma fonte, use os PAKs gerados
anteriormente como entrada e escolha um novo diretório de saída.

A caixa de diálogo do AIR mantém a fonte bitmap e a arte original da
janela. Quando ativadas, as cores de diálogo e de personagem do Kotonoha
também se aplicam aos glifos do atlas bitmap e de seus overlays. A caixa
usa o cache de quebra de linhas compartilhado para manter as linhas
estáveis durante o efeito de digitação.
Os botões laterais abrem **Menu**, alternam **Skip** ou **Auto** e repetem
a fala atual com **Voice** quando há uma gravação.
As escolhas da história usam a arte original centralizada de `SELWIN_s` e
`SELWIN` com os comandos de escolha do Kotonoha. O indicador no canto
superior esquerdo usa a data de `#6003` e a arte original `jul_` e `aug_`.
Os dias 16–31 selecionam `jul_`; os dias 1–15 selecionam `aug_`. O indicador
é desenhado uma vez após a composição da cena, de modo que um slot de fundo
ausente ou substituído não o esconda. Preenchimentos virtuais preto e branco
continuam ocultando o indicador.

## Menus do AIR

A abertura mostra primeiro o aviso de direitos autorais e depois os logos
da Visual Arts, Key e Prototype/LUCA. Os logos são silenciosos. No título,
as ondas do mar (`nami`, `200014`) tocam em loop no canal `Se` do Kotonoha;
**Reminiscences** (`trk22`, `100021`) começa no prólogo de DREAM, não no
título. Use Enter, Espaço ou um clique para avançar na abertura, ou Escape
para ir direto ao título.

**NEW GAME** abre o seletor de histórias original antes de iniciar um
capítulo. Em um perfil novo, só **DREAM** está disponível. Use a seta do
título para deslizar até a segunda página, que contém **GALLERY** e **MUSIC**.

Abra a pausa com Escape ou a ação de voltar do controle. O port usa os
ícones `systemmenu`, o fundo `system_menu_bg` e as páginas em inglês do
manual do AIR. Salvar, Carregar e Opções abrem o gerenciador de saves e as
configurações nativas do Kotonoha; salvar e carregar rapidamente continuam
usando `quicksave.kns`. O histórico (B ou Page Up) usa o `backlog_texture`
original com rolagem e repetição de voz do Kotonoha. Menus abertos capturam
a entrada sem avançar o diálogo.
A pausa também congela as esperas do roteiro e o vídeo de abertura. A
abertura usa a trilha embutida no WebM original; efeitos do roteiro em
canais separados não interrompem mais um ao outro.

As telas de salvar e carregar do AIR usam os fundos, as molduras, o
destaque e as setas originais em uma grade de 20 cartões. Use as setas ou
o direcional do controle para selecionar um cartão; Page Up/Page Down ou
Z/C trocam de página, e Escape ou X cancelam. O Kotonoha cuida de nomear,
sobrescrever, renomear e excluir saves `.vn`. Cada novo save inclui uma
captura da cena sem os menus, exibida nos cartões de salvar e carregar.
Saves antigos sem captura continuam mostrando o nome do slot.

**Options** usa o fundo, as nove abas em inglês, a moldura de prévia de
diálogo e a arte dos controles deslizantes originais do AIR. O Kotonoha
controla as opções de vídeo, texto, áudio e sistema. Use Tab ou clique em
uma aba e depois em **Apply and save** para gravar as mudanças pendentes.
Escape ou X descarta o que ainda estiver pendente e retorna ao título ou à
pausa; volume e velocidade de texto são aplicados na hora. As abas Keyboard,
Mouse e Touch listam os comandos do port, sem os editores de remapeamento
do jogo original.

No título, abra **Gallery** para navegar pelas 72 posições de CG em quatro
categorias. Use as setas ou o direcional do controle para navegar, Enter
para abrir uma posição e Escape para voltar. Page Up/Page Down trocam a
página da grade. Cada variante é desbloqueada quando aparece no roteiro,
não quando é pré-carregada, e permanece liberada em `global_system.kns`.
A galeria não exibe a arte de variantes bloqueadas.

## Investigação detalhada

[Resumo do estado do port](STATUS.pt-BR.md) · [Investigação detalhada](PORT_STATUS.md). Resultados históricos se aplicam aos assets/revisão analisados e podem conter informações substituídas depois.

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)

## Saves e retomada de execução

O formato `LUCA_SYSTEM_SAVE_3` preserva a posição no script, os
registradores numéricos, as variáveis de texto (incluindo espaços, quebras de
linha e bytes nulos), flags, pilha de GOSUB, mensagem visível e tempo restante
de espera. Carregar um save não deve executar automaticamente o próximo
opcode: a execução continua a partir da posição registrada.

Saves `LUCA_SYSTEM_SAVE_2` anteriores continuam aceitos quando válidos;
valores de texto que já foram truncados pelo formato antigo não podem ser
recuperados. Arquivos inválidos são rejeitados antes de aplicar o estado à VM.
O estado visual completo da cena e a compatibilidade total com AIR ainda
precisam de testes com os dados originais.

Teste de regressão independente de dados comerciais:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DKOTONOHA_BUILD_LUCA_SYSTEM=ON
cmake --build build --target LucaSystemSaveCodecTests
ctest --test-dir build -R LucaSystem.SaveCodec --output-on-failure
```

## Diálogos na ordem dos opcodes

A entrada agora usa a mesma rotina para teclado, mouse e ações de avanço:
primeiro revela a fala incompleta; depois retoma o interpretador no próximo
opcode. Isso impede que o motor mostre a lista de textos extraídos ignorando
comandos de imagem, música, waits ou desvios intermediários.

F5/F6 têm prioridade sobre a confirmação da caixa de diálogo, permitindo
salvar/carregar enquanto uma fala está visível. Um teste sintético verifica
a decisão `Reveal` versus `ResumeScript`. Ainda é necessária a validação
com o AIR original.

## Saves visuais V4 e chamadas entre cenários V5

O formato `LUCA_SYSTEM_SAVE_5` registra os IDs de imagem, posições dos objetos
`IMAGELOAD` e os quadros da pilha de chamadas entre múltiplos cenários (`CallStackFrame`).
Ao carregar, reabre as imagens dos PAKs originais e restaura a camada visual e o histórico
de sub-rotinas entre scripts. Saves `V4`, `V3` e `V2` permanecem 100% legíveis.

**Limites:** o efeito visual original, as camadas de fundo, transições e
animações ainda exigem reconstrução. O save não inclui pixels nem arquivos
de mídia; o jogo original precisa estar acessível no carregamento.

## Composição e transições experimentais

`SceneLayers` agora separa imagens `IMAGELOAD` ainda não apresentadas das
camadas exibidas. O opcode `DRAW` publica as imagens em ordem e usa uma
transição de opacidade com duração limitada. O final de uma instrução
`MESSAGE` ou `END` também publica imagens pendentes se o título omitir
`DRAW`. `FADE` usa uma sobreposição preta temporizada e `WIPE` utiliza
uma transição de opacidade provisória. A renderização SDL aplica a opacidade
por camada, enquanto o codec V4 continua salvando apenas o estado exibido.

**Importante:** ainda são *aproximações visuais*, não a implementação exata
das curvas, máscaras, direção dos wipes, composição original do fundo ou
operações de animação do AIR. Precisam ser comparadas com o executável e
assets originais. Teste C++17 sem SDL:

```sh
cmake --build build --target LucaSystemSceneLayersTests
ctest --test-dir build -R LucaSystem.SceneLayers --output-on-failure
```

## Recorte horizontal em WIPE

O opcode `WIPE` passa a usar um modo específico de revelação da esquerda
para a direita no lugar do cross-fade provisório. O renderizador recorta tanto
a textura de origem quanto o destino no limite de avanço da transição, sem
esticar sprites. Imagens da composição anterior são preservadas durante o
wipe; somente imagens recém-adicionadas são reveladas. O teste
`LucaSystem.SceneLayers` verifica progresso e limites de recorte.

Isso ainda **não é uma transição AIR 1:1**: direções e máscaras específicas,
operação de objetos e temporizações originais exigem validação com o jogo.

## Composição incremental sem desenho duplicado

Durante `DRAW` ou `WIPE`, os objetos anteriores formam a camada de
saída e apenas os objetos recém-publicados são renderizados na camada de
entrada, com opacidade ou recorte variável. Isso impede que imagens
transparentes antigas sejam desenhadas duas vezes e fiquem artificialmente
escuras/opacas durante a transição. Ao término, a composição inteira é
desenhada uma única vez. O teste `LucaSystem.SceneLayers` verifica a
fronteira entre objetos antigos e novos.

As expressões de personagens CZ4 do AIR podem ser recortes pequenos da mesma
tela do retrato. Quando `IMAGELOAD` substitui um slot de personagem por um
recorte menor da mesma tela, o retrato completo anterior permanece por baixo
e apenas a expressão muda. `INIT` e retratos completos posteriores removem
a expressão anterior. A verificação usa o par comercial `cgmz20` / `cgmz21p`
e o fluxo `seen190`.

Ainda não há garantia de correspondência exata às máscaras, camadas,
ordens e curvas do AIR original.


## Nodos nativos e assets no Debug UI

Pressione **F10** e abra o inspetor de scripts. A lista agora apresenta os
cenários do `SCRIPT.PAK`; para o cenário ativo, também mostra nodos de
instruções relevantes, como MESSAGE, SELECT, IMAGELOAD, BGM, VOICE, SE
e MOVIE. Ao selecionar um nodo, aparecem o script original e os IDs
lógicos do provider, como `image:1127`, `voice:73` e `bgm:100001`.
A instrução atual também mostra as imagens visíveis e a voz/BGM ativos.
A lista é atualizada quando o cenário muda.

Use **List provider assets** para navegar pelos assets nomeados dos PAKs
e pelos IDs numéricos de imagens, vozes, músicas e efeitos. A opção
**Dump raw asset to Documents** salva os bytes sem conversão em
`Documents/asset-dumps`, podendo escolher a origem original ou efetiva
(com overlay). **Open in Voice Dubbing** envia o áudio selecionado ao
applet de dublagem compartilhado.

Pular diretamente para uma instrução reinicia o script naquela posição e
**não reconstrói variáveis anteriores, pilha de chamadas ou camadas já
desenhadas**. O catálogo inspeciona somente o script ativo, evitando
decodificar todos os scripts comerciais de uma só vez. O teste sintético
C++17 é `LucaSystem.DebugCatalog`; a navegação real ainda requer os
testes opcionais com os dados originais.
