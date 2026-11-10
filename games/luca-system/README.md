# LucaSystem port

Suporte a jogos da engine [LucaSystem](https://github.com/wetor/LuckSystem)
(Visual Art's / Key — AIR, Kanon, CLANNAD e outros) no Kotonoha.

Esta é uma nova porta desenvolvida com referência na
[wetor/LuckSystem](https://github.com/wetor/LuckSystem), conjunto de
ferramentas para decompilar e compilar scripts do LucaSystem, e validada com o
jogo **AIR** (`/Volumes/Nero 2TB/SteamLibrary/steamapps/common/AIR`) como
dados de teste de referência.

## O que é suportado

- **Arquivos .PAK** (PARAM.PAK, SCRIPT.PAK, SYSSE.PAK e arquivos de imagem):
  leitura de entradas, tabela de deslocamentos e nomes de arquivos, seguindo o
  formato decodificado pelo LuckSystem (pak.go).
- **Scripts de bytecode** (SCRIPT.PAK): parsing estrutural das instruções
  (opcode, fixedFlag, len, parâmetros) e extração das strings codificadas em
  Shift-JIS, seguindo o LuckSystem (script.go).
- **Launcher**: seleção de pasta/ISO via `--luca-system` ou pelo launcher de
  pastas do Kotonoha.
- **Apresentação de diálogo**: as strings extraídas dos scripts são exibidas
  na caixa de texto (VNTextBox), com avanço por entrada do jogador.
- **Save/load**: quicksave (F5/F6) e menu do jogo.
- **Integração Kotonoha**: engine, renderer, áudio, overlays, settings e MCP
  compartilhados.

## O que ainda não é suportado

- **VM completa com semântica de opcode**: os opcodes e o significado dos
  parâmetros são específicos de cada jogo (definidos por um OPCODE.txt
  externo, como no LuckSystem). A interpretação completa (escolhas, saltos,
  variáveis, áudio sincronizado) requer a tabela de opcodes do título alvo.
- **Decodificação de imagens nativas**: imagens dentro dos arquivos .PAK de
  imagem dependem do formato gráfico original do LucaSystem.

## Como construir

O alvo é construído automaticamente com o projeto. Para desativar:

```sh
cmake -S . -B build -DKOTONOHA_BUILD_LUCA_SYSTEM=OFF
```

## Como executar

```sh
build/games/luca-system/LucaSystem.app/Contents/MacOS/LucaSystem \
  --luca-system /caminho/para/dados/do/jogo --no-launcher
```

Com o AIR de referência:

```sh
build/games/luca-system/LucaSystem.app/Contents/MacOS/LucaSystem \
  --luca-system '/Volumes/Nero 2TB/SteamLibrary/steamapps/common/AIR/files' \
  --no-launcher
```

Ou use o launcher de pastas (`--launcher`, padrão) para selecionar o diretório
de dados.

O jogo é válido se a pasta contiver PARAM.PAK e SCRIPT.PAK.

## Testes

```sh
ctest --test-dir build -R LucaSystem
```

## Licença

Esta porta é uma implementação independente e não distribui nenhum código
binário da engine LucaSystem nem dados comerciais de jogos. Forneça seus
próprios dados legalmente obtidos.
