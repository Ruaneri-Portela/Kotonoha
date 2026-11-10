# LucaSystem port status

Validado com o jogo de referência **AIR**
(`/Volumes/Nero 2TB/SteamLibrary/steamapps/common/AIR/files`).

## Suporte

| Recurso | Status | Notas |
|---|---|---|
| Leitura de arquivos .PAK (PARAM/SCRIPT/SYSSE/imagem/áudio/voz/fonte) | Completo | Descoberta recursiva; payloads são lidos sob demanda, validado contra os arquivos reais do AIR |
| Tabela de nomes e IDs de entrada | Completo | Flags, blockSize, deslocamentos em blocos |
| Parsing estrutural de scripts de bytecode | Parcial | Cabeçalho real `len/opcode/fixedFlag` corrigido; parâmetros ainda precisam ser tipados pela tabela AIR |
| Decodificação de texto UTF-16/UTF-8 por opcode | Parcial | `MESSAGE` de AIR usa corretamente japonês UTF-16 e inglês UTF-8; demais opcodes textuais ainda pendentes |
| Menu principal original de AIR | Parcial | `title1a` e `title_menu` são decodificados diretamente dos PAKs; New Game entra em `seen163` |
| Imagens CZ0/CZ3 | Parcial | Implementadas para os recursos originais do título; CZ1/CZ2/CZ4 ainda pendentes |
| Launcher de pasta/ISO | Completo | `--luca-system` aceita tanto a raiz de AIR quanto a pasta `files` |
| Apresentação de diálogo (VNTextBox) | Stub | A varredura sequencial não representa a execução do LucaSystem e será removida |
| Save/load | Stub | Ainda não serializa o estado da VM LucaSystem |
| Backlog, menu do jogo | Stub | Ainda usa cenas genéricas, não os scripts/recursos de AIR |
| VM completa (escolhas, saltos, variáveis, áudio) | Não suportado | Requer OPCODE.txt específico do jogo |
| Decodificação de imagens de .PAK de imagem | Não suportado | Formato gráfico nativo do LucaSystem |

## Referência

- https://github.com/wetor/LuckSystem (formato pak.go / script.go)
- https://github.com/yoremi-trad-fr/LuckSystem-2.3.2-Yoremi-Update
- Jogo de teste: AIR (Steam)
