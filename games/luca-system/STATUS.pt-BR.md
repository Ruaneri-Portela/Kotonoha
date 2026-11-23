# LucaSystem / AIR — resumo de suporte

[English (US)](STATUS.md) | **Português (Brasil)**

**Escopo implementado/testado:** `AIR` comercial (`AIR.exe` Steam), com 31 arquivos `.PAK` montados e `files/movie/*.webm`:
- Tabela de 140 opcodes (`índice 135 = NULL`, `137 = SCENARIO`, `138 = HAIKEI_SET`) verificada na seção `.rdata` de `AIR.exe`.
- Pontos de entrada canônicos de capítulos (`seen170` DREAM, `seen700` SUMMER, `seen163` AIR, `seen800` FIRST SKY). O título desliza entre duas páginas e `NEW GAME` abre o seletor de histórias original.
- O aviso de direitos autorais vem antes dos três logos silenciosos da abertura. O título toca `nami` (`200014`, `SE.PAK`), identificada por comparação com uma [gravação da versão Steam](https://www.youtube.com/watch?v=Xfal9fQxdxI). DREAM toca `trk22` (`100021`, Reminiscences) pelo comando `BGM` do roteiro.
- Cabeçalho estendido CZ4 de 36 bytes (`cropX`, `cropY`, `anchorX`, `anchorY`), paleta RGBA de CZ1, camadas sólidas virtuais (`65000` branco / `65001` preto) e desambiguação por classe de recurso (`voice:<id>`, `bgm:<id>`, `se:<id>`), eliminando todas as 1.255 colisões de IDs numéricos.
- A decodificação de CZ2 segue o LZW de largura variável e a paleta BGRA do LuckSystem, com preenchimento isolado por bloco, como no fork Yoremi. O `VNTextBox` compartilhado usa as métricas originais `info27` e o atlas `ゴシック27` para diálogos e nomes de personagens. Se algum caractere não tiver glifo ou o atlas estiver indisponível, o trecho inteiro usa a fonte da interface compartilhada.
- O provedor lê vozes Ogg avulsas e pacotes OggPak. `voice:<id>` seleciona o clipe zero e `voice:<id>:<index>` seleciona um clipe por índice, a partir de zero. O carregador de áudio nativo aceita substituições em `voice/<id>/<index>.ogg` no overlay de recursos.
- Avaliador recursivo de expressões estilo C (`IFN`, `IFY`, `EQU`), atribuição imediata de 4 bytes em `EQUN`, escolhas `SELECT` UTF-16LE separadas por `$d` gravando em `#6001`, sanitização de `@Speaker@` / `$n` / `$[base$ruby$]`, janelas `MWIN0..4` (`#6002`), persistência global em `global_system.kns` (`#12000..#19999`) e saves `SaveCodec`.

- `LucaPak` importa substituições por nome ou ID e reempacota PAKs nomeados e numéricos sem alterar o arquivo original.
- `LucaPak patch-air` importa texto inglês de tamanho variável em `MESSAGE` e ajusta os saltos conhecidos dentro de um cenário e entre cenários. Validado com um `SCRIPT.PAK` comercial modificado e montado pelo overlay de recursos do Kotonoha.
- `LucaPak edit-glyph` modifica glifos existentes em CZ2 de 8 bits e suas métricas em PAKs reempacotados. O `VNTextBox` renderizou o glifo e o avanço editados com o overlay de fontes do AIR comercial.
- `LucaPak add-glyph` adiciona um caractere BMP ainda não mapeado, ampliando o atlas CZ2 quando necessário. O `VNTextBox` do AIR renderizou um novo `U+E000` com o avanço importado e as cores de diálogo e de personagem configuradas no Kotonoha.
- O menu de pausa, o manual e o histórico usam arte original do AIR. A lista de saves, as configurações, a captura de entrada e a repetição de voz no histórico usam componentes do Kotonoha. A caixa de diálogo tem botões funcionais de Menu, Skip, Auto e Voice.
- As escolhas da história usam a arte original `SELWIN_s` e `SELWIN` no `ChoiceQuestionScene` do Kotonoha. O indicador de data original no canto superior esquerdo acompanha `#6003`. Esperas do roteiro congelam durante a pausa; efeitos em canais diferentes podem tocar juntos, e a abertura usa o áudio embutido.
- Salvar e carregar usam os fundos, as molduras e as setas de página originais do AIR. O `VNSaveMenu` controla a seleção e as ações nos slots. A VM do AIR grava seu estado e uma captura da cena no mesmo `.vn` nativo, sem depender de um nó da timeline do Kotonoha.
- Options usa o fundo, as abas em inglês, a moldura de prévia de texto e os controles deslizantes originais do AIR. O `SettingsMenu` do Kotonoha continua responsável pela entrada, pelo áudio em tempo real, pela aplicação e pelo cancelamento das opções e pela persistência.
- A opção **Gallery** do título abre as 72 posições e variantes agrupadas de `_cggallery` com fundos, arte das categorias, miniaturas e cursores originais do AIR. A VM grava o desbloqueio de cada CG exibida em `global_system.kns`; variantes ainda não exibidas continuam bloqueadas entre sessões.

**Limitações:** Options usa as configurações e a ajuda de comandos do Kotonoha em vez de todos os editores originais por dispositivo. As vozes Ogg avulsas foram validadas com o `AIR` comercial; os pacotes OggPak foram validados com dados sintéticos, não com outro jogo comercial. Entre os recursos das referências que ainda precisam ser portados ou validados estão a criação de fontes do zero, a importação de scripts além dos diálogos em inglês do AIR e os opcodes específicos de outros jogos.

Os relatórios [PORT_STATUS.md](PORT_STATUS.md) e [docs/AIR_REA_2026_11_18.pt-BR.md](docs/AIR_REA_2026_11_18.pt-BR.md) contêm análises binárias detalhadas e notas históricas.

[README do port](README.pt-BR.md) · [Todos os ports](../README.pt-BR.md)
