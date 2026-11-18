# LucaSystem / AIR — resumo de suporte

[English (US)](STATUS.md) | **Português (Brasil)**

**Escopo implementado/testado:** Paridade 1:1 completa verificada contra o `AIR` comercial (`AIR.exe` Steam), todos os 31 arquivos `.PAK` e `files/movie/*.webm`:
- Tabela de 140 opcodes (`índice 135 = NULL`, `137 = SCENARIO`, `138 = HAIKEI_SET`) verificada na seção `.rdata` de `AIR.exe`.
- Pontos de entrada canônicos de capítulos (`seen170` DREAM Novo Jogo, `seen700` SUMMER, `seen163` AIR, `seen800` FIRST SKY) e botões da tela de título (`EXIT` no botão 6, `SELECT STORY`, `MUSIC`).
- Cabeçalho estendido CZ4 de 36 bytes (`cropX`, `cropY`, `anchorX`, `anchorY`), paleta RGBA de CZ1, suporte a CZ2, camadas sólidas virtuais (`65000` branco / `65001` preto) e desambiguação por classe de recurso (`voice:<id>`, `bgm:<id>`, `se:<id>`), eliminando todas as 1.255 colisões de IDs numéricos.
- Avaliador recursivo de expressões estilo C (`IFN`, `IFY`, `EQU`), atribuição imediata de 4 bytes em `EQUN`, escolhas `SELECT` UTF-16LE separadas por `$d` gravando em `#6001`, sanitização de `@Speaker@` / `$n` / `$[base$ruby$]`, janelas `MWIN0..4` (`#6002`), persistência global em `global_system.kns` (`#12000..#19999`) e saves `SaveCodec`.

**Limitações:** As evidências vêm do `AIR` comercial e não garantem suporte a lançamentos LucaSystem não auditados.

Os relatórios [PORT_STATUS.md](PORT_STATUS.md) e [docs/AIR_REA_2026_11_18.pt-BR.md](docs/AIR_REA_2026_11_18.pt-BR.md) contêm análises binárias detalhadas e notas históricas.

[README do port](README.pt-BR.md) · [Todos os ports](../README.pt-BR.md)
