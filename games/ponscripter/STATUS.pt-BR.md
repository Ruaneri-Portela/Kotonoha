# Estado do Port PONscripter

[English (US)](STATUS.md) | **Português (Brasil)**

## Cobertura de Implementação

- **Descoberta de Scripts e Diretivas (`ScriptBuffer`)**: Concatena `0.utf`, `00.utf`..`99.utf` (e fallbacks `.txt`), remove o BOM UTF-8 (`EF BB BF`) antes da leitura de diretivas (`;mode800` -> `800x600`, `;value<N>`) e indexa instruções e `*labels`.
- **Provedor de Arquivos e Assets (`PonscripterAssets` + `ArchiveReader`)**: Suporta arquivos SAR e NSA (v1/v2) com descompressão raw, LZSS, SPB e NBZ, resolução de caminhos case-insensitive no macOS/Linux e fallback `sound/<dir>/<clip>.ogg` -> `voice/<dir>/<clip>.ogg` para os 15 erros de caminho de áudio em `01.utf`.
- **Máquina Virtual PONscripter (`PonscripterVm`)**: Implementa todos os 75 comandos utilizados em `00.utf` (`1.679` linhas) e `01.utf` (`11.576` linhas), além da família completa de comandos ONScripter/PONscripter (`numalias`, `stralias`, `defsub`, `if`/`notif` com `&&`/`||`/`<>`, `goto`/`gosub`/`return`, `for`/`next`/`break`, `select`/`csel`, `mov`/`inc`/`dec`/`add`/`sub`/`mul`/`div`/`mod`/`rnd`/`cmp`/`atoi`/`itoa`, `bg`, `ld`, `cl`, `lsp`/`csp`/`vsp`/`msp`/`amsp`, `bar`/`barclear`, `btndef`/`btn`/`spbtn`/`btnwait`, `print`/`effect`/`windoweffect`, `setwindow3`/`setcursor`/`h_mapfont`, `mp3`/`mp3loop`/`dwave`/`dwaveloop`, `mpegplay`, `rmenu`, `systemcall`).
- **Tags de Sprite e Compositor**: Suporta `:a` (alfa nativo em PNG ou máscara na metade direita para BMPs sem alfa como `system\uoncur.bmp`), `:c`, `:l`, `:r`, `:m` e sprites de texto `:s[;][#RRGGBB]^texto`, modos de animação multicélula `0` (loop), `1` (uma vez), `2` (ping-pong) e `3` (célula manual de botão hover), e ordem Z `999 -> 0`.
- **Texto Proporcional e Tags Inline (`TextFormatter`)**: Processa texto proporcional `^`, espera inline `@`, quebra de página `\` com histórico (*backlog*), tags `~i~`/`~b~`/`~c~`/`~s~`, escape `~~` para til literal, cores `#RRGGBB` e ligaduras tipográficas (`...` -> `…`, `--` -> `–`, `---` -> `—`).
- **Persistência de Save e Extras (`SaveSerializer`)**: Persiste variáveis globais (`gloval.sav`: `%lic_accepted`, `%end1_clear..%end3_clear`, `%cgset_0..4`, `%gro`, `%vol*`, `%textspeed`, `%autospeed`, `%movplay`) e 20 slots de save (`save1.dat`..`save20.dat`), desbloqueando a Galeria de CGs (`97` CGs), Music Box (`15` faixas) e Comentários da Equipe nos 3 finais.
