# LucaSystem port status

Validado com o jogo de referência **AIR**
(`/Volumes/Nero 2TB/SteamLibrary/steamapps/common/AIR/files`).

Engenharia reversa realizada com **rezin/REA** no binário original `AIR.exe`
(PE32+ x86-64, símbolos PDB preservados, não-stripado) e análise direta dos
scripts de bytecode em `SCRIPT.PAK` via LucaInspect.

## Suporte

| Recurso | Status | Notas |
|---|---|---|
| Leitura de arquivos .PAK (PARAM/SCRIPT/SYSSE/imagem/áudio/voz/fonte) | Completo | Descoberta recursiva; payloads lidos sob demanda, validado contra os arquivos reais do AIR (31 arquivos montados) |
| Tabela de nomes e IDs de entrada | Completo | Flags, blockSize, deslocamentos em blocos, nomes null-terminated |
| Parsing estrutural de scripts de bytecode | Completo | Cabeçalho `len/opcode/fixedFlag` corrigido; parâmetros tipados conforme tabela AIR |
| Decodificação de texto UTF-16/UTF-8 por opcode | Completo | `MESSAGE` (opcode 36) decodifica japonês UTF-16 e inglês UTF-8 |
| VM completa de opcodes | Completa | MESSAGE, FADE, IMAGELOAD, DRAW, WAIT, INIT, HAIKIE_SET, EQUN/EQU, BGM, VOICE, VOICE_STOP, SE, SE_STOP, JUMP, CALL, RETURN, CLOSE_WINDOW, WIPE, IMAGEUPDATE, GOTO, IFY, IFN, VARSTR, SET, ARFLAGSET e opcodes de objeto (MOVE, ROT, SCALE, SHAKE, FADE_OBJ, BASE, DISP, VOLUME, TONE, SEPIA, PRIORITY, FACE, EX) |
| Variáveis numéricas (EQUN) e strings (EQU) | Completo | Serializadas em save/load |
| Flags (ARFLAGSET) | Completo | Serializadas em save/load |
| Pilha de chamadas (CALL/RETURN) | Completo | Suporte a sub-rotinas de script |
| Menu principal original de AIR | Completo | `title1a` e `title_menu` decodificados dos PAKs; 9 opções de menu; New Game entra em `seen163` |
| Imagens CZ0/CZ1/CZ3/CZ4 | Completo | Decodificação nativa implementada e testada contra recursos do AIR |
| Áudio BGM/VOICE/SE | Completo | Via Kotonoha PcmPlayer nos canais BGM/Voice/Se |
| Launcher de pasta/ISO | Completo | `--luca-system` aceita raiz de AIR ou pasta `files`; suporte a ISO via DiscImage |
| Apresentação de diálogo (VNTextBox) | Completo | AirTextBox com frame MWIN1280_0, cursor MWIN_CURSOR, texto UTF-16/UTF-8 |
| Save/load | Completo | Serializa cenário, índice de instrução, diálogo, variáveis numéricas/strings, flags |
| Backlog, menu do jogo | Completo | Cenas Kotonoha nativas integradas aos scripts de AIR |

## Referência

- https://github.com/wetor/LuckSystem (formato pak.go / script.go)
- https://github.com/yoremi-trad-fr/LuckSystem-2.3.2-Yoremi-Update
- Binário original analisado: `AIR.exe` (PE32+, PDB symbols, via rizin/REA)
- Scripts analisados: `SCRIPT.PAK` do AIR (35 entradas, bytecode LucaSystem)
- Jogo de teste: AIR (Steam)

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
