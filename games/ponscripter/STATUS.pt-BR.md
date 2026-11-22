# Estado do Port PONscripter

[English (US)](STATUS.md) | **Português (Brasil)**

## Cobertura de Implementação

- **Descoberta de Scripts e Diretivas (`ScriptBuffer`)**: Concatena `0.utf`, `00.utf`..`99.utf` (e fallbacks `.txt`), remove o BOM UTF-8 (`EF BB BF`) antes da leitura de diretivas (`;mode800` -> `800x600`, `;value<N>`) e indexa instruções e `*labels`.
- **Provedor de Arquivos e Assets (`PonscripterAssets` + `ArchiveReader`)**: Suporta arquivos SAR e NSA (v1/v2) com descompressão raw, LZSS, SPB e NBZ, resolução de caminhos case-insensitive no macOS/Linux e fallback `sound/<dir>/<clip>.ogg` -> `voice/<dir>/<clip>.ogg` para os 15 erros de caminho de áudio em `01.utf`.
- **Máquina Virtual PONscripter (`PonscripterVm`)**: Implementa todos os 75 comandos utilizados em `00.utf` (`1.679` linhas) e `01.utf` (`11.576` linhas), além da família completa de comandos ONScripter/PONscripter (`numalias`, `stralias`, `defsub`, `if`/`notif` com `&&`/`||`/`<>`, `goto`/`gosub`/`return`, `for`/`next`/`break`, `select`/`csel`, `mov`/`inc`/`dec`/`add`/`sub`/`mul`/`div`/`mod`/`rnd`/`cmp`/`atoi`/`itoa`, `bg`, `ld`, `cl`, `lsp`/`csp`/`vsp`/`msp`/`amsp`, `bar`/`barclear`, `btndef`/`btn`/`spbtn`/`btnwait`, `print`/`effect`/`windoweffect`, `setwindow3`/`setcursor`/`h_mapfont`, `mp3`/`mp3loop`/`dwave`/`dwaveloop`, `mpegplay`, `rmenu`, `systemcall`).
- **Tags de Sprite e Compositor**: Suporta `:a` (alfa nativo em PNG ou máscara na metade direita para BMPs sem alfa como `system\uoncur.bmp`), `:c`, `:l`, `:r`, `:m` e sprites de texto `:s[;][#RRGGBB]^texto`, modos de animação multicélula `0` (loop), `1` (uma vez), `2` (ping-pong) e `3` (célula manual de botão hover), e ordem Z `999 -> 0`.
- **Texto Proporcional e Tags Inline (`TextFormatter`)**: Processa texto proporcional `^`, espera inline `@`, quebra de página `\` com histórico (*backlog*), tags `~i~`/`~b~`/`~c~`/`~s~`, escape `~~` para til literal, cores `#RRGGBB` e ligaduras tipográficas (`...` -> `…`, `--` -> `–`, `---` -> `—`).
- **Persistência de Save e Extras (`SaveSerializer`)**: Persiste variáveis globais (`gloval.sav`: `%lic_accepted`, `%end1_clear..%end3_clear`, `%cgset_0..4`, `%gro`, `%vol*`, `%textspeed`, `%autospeed`, `%movplay`) e 20 slots de save (`save1.dat`..`save20.dat`), desbloqueando a Galeria de CGs (`97` CGs), Music Box (`15` faixas) e Comentários da Equipe nos 3 finais.

## Integração com a UI de Kotonoha

- O `rmenu` nativo (`Salvar`, `Carregar`, `Histórico`, `Tela inicial`) abre `PonscripterMenu`, implementação PONscripter injetada através do componente compartilhado `Kotonoha::InGameMenu`. Cada item mapeia para a system call nativa (`save`, `load`, `lookback`, `reset`) e a renderização substitui o painel genérico do Kotonoha.
- O `lookback` nativo abre `PonscripterBacklog`, implementação PONscripter injetada através de `Kotonoha::VNBacklog`; Save/Load permanecem no overlay nativo de slots do PONscripter e não usam `Kotonoha::VNSaveMenu`.
- A VM permanece em `WaitSystemCall` até um modal PONscripter fechar, com callbacks de fechamento restaurando o estado de espera anterior via `finishSystemCall()`.
- A navegação do histórico PONscripter suporta Up/Left/PageUp e Down/Right/PageDown, roda do mouse, `B`, `Escape` e fechar com clique direito, além de contador de página e linha de instruções no estilo nativo.
- A seleção do histórico PONscripter fica limitada ao intervalo válido `[0, entries.size() - 1]` ao abrir, navegar, mover com a roda e renderizar.
- Os itens do histórico e do menu PONscripter usam `Engine::GetVNUI()` e respeitam `vnTextColorOverride`, `vnSpeakerColor` e `vnTextColor`; entradas do histórico genérico usam os mesmos controles compartilhados.
- Fundos, personagens, sprites estendidos, cursores e fundos da janela de texto são processados individualmente, cacheados e passam pelo upscaler do motor quando ativo.
- `PonscripterRuntimeTests` cobre um script sintético e os dados comerciais de Saya no Uta: `rmenu` por clique direito, Salvar/Carregar, Histórico, `lookback`/`save` diretos e a retomada da VM após fechar cada modal.

## Melhorias de compatibilidade (novembro de 2026)

- Implementada passagem de parâmetros de subrotinas em `gosub`, `defsub` e `getparam`, incluindo persistência dos parâmetros pendentes nos saves.
- Adicionada interpolação de texto proporcional com `{$variável}`, `{%variável}` e `{?vetor[índice]}`, sem executar caracteres interpolados como marcação.
- Corrigidos laços `for` que já começam fora do intervalo, inclusive aninhados, e reforçada a validação de referências numéricas.
- Implementados `tablegoto`/`debugtablegoto` (índices iniciados em zero), `tablegoto1` (índices iniciados em um), `len`, `mid` com suporte a UTF-8 e `movz` com preenchimento de zeros preservando a diferença em relação a `movl`.
- Diferenciados `select` (salto), `selgosub` (chamada com retorno) e `selnum` (índice da escolha); o modo e o destino da seleção persistem em Save/Load, inclusive com leitura de saves V1 anteriores.
- Acrescentados testes sintéticos em C++17 para VM, lexer, imagens/arquivos compactados e fluxo de menus sem depender de dados comerciais.

### Cobertura atual de scripts, menus e gráficos

- Continuação explícita de escolhas em múltiplas linhas (`select`, `selgosub`, `selnum`, `csel`) reunida antes de numerar instruções.
- `csel` chama `*customsel`, preserva `getcselnum`/`getcselstr` e implementa botões de texto `cselbtn` desenhados com a UI acelerada e `cselgoto`; escolhas e botões são recuperados do Save/Load.
- `getparam i%N`/`s%N` usa a identidade original das variáveis da chamada (`CALLREF`), inclusive após carregar; `return` restaura a label do chamador (`CALLFROM`).
- `pretextgosub` e `textgosub` entram nas subrotinas apropriadas, evitam recursão e suportam `texec`; saves guardam callbacks pendentes e o progresso dos efeitos.
- O grupo de comandos gráficos agora possui tratamento explícito para `tal`, `strsp`/`strsph`, visibilidade independente de `lsp2`, blend aditivo/subtrativo, desenho manual `draw*`/`blt`, capturas por camadas `bgcopy`/`bgcpy`, comandos compostos `spstr`, tremores `quake`/`quakex`/`quakey` e filtros `monocro`/`nega`.
- Os testes sintéticos em C++17 cobrem cada grupo, com matriz da VM em Linux/macOS, verificação da sintaxe do runtime completo via GCC/Clang no Linux e workflow de build/teste nativo CMake do jogo.

### Verificações e diferenças ainda relevantes

- Os dados dos jogos comerciais não estão incluídos nos testes sintéticos. As rotas e particularidades dos scripts precisam ser comparadas com conteúdo real antes de declarar compatibilidade universal.
- `bgcopy` registra as camadas do cenário em vez de copiar literalmente cada pixel do framebuffer, podendo diferir em overlays temporários; as matrizes afins e os modos de blend SDL3 também exigem comparação visual com o interpretador de referência.
- `monocro`/`nega` usam leitura do quadro e atualização de textura SDL3 quando ativados; esse caminho pode custar desempenho em alta resolução e precisa de medição em dispositivos.
- Pausas de clique embutidas no meio das linhas e operações específicas de plataforma podem divergir do original. A validação deste workflow não certifica build completo iOS/Android/WebAssembly nem equivalência visual de jogos comerciais.
- A implementação é independente, não uma transferência 1:1 do código GPL do projeto de referência.
