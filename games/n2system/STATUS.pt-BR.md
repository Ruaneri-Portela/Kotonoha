# Estado do Port N2System & PONscripter

[English (US)](STATUS.md) | **Português (Brasil)**

## Cobertura de Implementação

### 1. Nitroplus N2System (`.nss` / `.npa`)
- **VM & Indexação NsScript**: Capítulos, rótulos, sub-rotinas (`Call`, `CallLocal`, `Return`), controle de fluxo (`Goto`, `Jump`, `Chapter`).
- **Armazenamento de Variáveis e Expressões**: Variáveis inteiras (`$var`), texto (`@str`), booleanas (`#flag`) e avaliador de expressões com operadores aritméticos, lógicos e de comparação.
- **Grafo de Cena**: Árvore de entidades (Contêiner, Sprite, Texto, Escolha) com transformações (`x`, `y`, `z`, `scale`, `angle`, `opacity`, `tint`) e animações (`fade`, `move`, `zoom`).
- **Sistemas de Áudio e Diálogo**: Canais de BGM, Voz e SE com fading, loops e controle de volume; animação de texto estilo máquina de escrever, oradores e histórico (*backlog*).
- **Save State**: Serialização de save state em formato binário (`N2SV`) e JSON.

### 2. PONscripter / `Saya no Uta PT-BR` (`00.utf` / `01.utf` / `arc.nsa`)
- **Descoberta de Scripts e Diretivas (`Ponscripter::ScriptBuffer`)**: Concatena `0.utf`, `00.utf`..`99.utf` (e fallbacks `.txt`), remove o BOM UTF-8 (`EF BB BF`) antes da leitura de diretivas (`;mode800` -> `800x600`, `;value<N>`) e indexa instruções e `*labels`.
- **Provedor de Arquivos e Assets (`Ponscripter::PonscripterAssets` + `ArchiveReader`)**: Suporta arquivos SAR e NSA (v1/v2) com descompressão raw, LZSS, SPB e NBZ, resolução de caminhos case-insensitive no macOS/Linux e fallback `sound/<dir>/<clip>.ogg` -> `voice/<dir>/<clip>.ogg` para os 15 erros de caminho de áudio em `01.utf`.
- **Máquina Virtual PONscripter (`Ponscripter::PonscripterVm`)**: Implementa todos os 75 comandos utilizados em `00.utf` (`1.679` linhas) e `01.utf` (`11.576` linhas), além da família completa de comandos ONScripter/PONscripter (`numalias`, `stralias`, `defsub`, `if`/`notif` com `&&`/`||`/`<>`, `goto`/`gosub`/`return`, `for`/`next`/`break`, `select`/`csel`, `mov`/`inc`/`dec`/`add`/`sub`/`mul`/`div`/`mod`/`rnd`/`cmp`/`atoi`/`itoa`, `bg`, `ld`, `cl`, `lsp`/`csp`/`vsp`/`msp`/`amsp`, `bar`/`barclear`, `btndef`/`btn`/`spbtn`/`btnwait`, `print`/`effect`/`windoweffect`, `setwindow3`/`setcursor`/`h_mapfont`, `mp3`/`mp3loop`/`dwave`/`dwaveloop`, `mpegplay`, `rmenu`, `systemcall`).
- **Tags de Sprite e Compositor**: Suporta `:a` (alfa nativo em PNG ou máscara na metade direita para BMPs sem alfa como `system\uoncur.bmp`), `:c`, `:l`, `:r`, `:m` e sprites de texto `:s[;][#RRGGBB]^texto`, modos de animação multicélula `0` (loop), `1` (uma vez), `2` (ping-pong) e `3` (célula manual de botão hover), e ordem Z `999 -> 0`.
- **Texto Proporcional e Tags Inline (`Ponscripter::TextFormatter`)**: Processa texto proporcional `^`, espera inline `@`, quebra de página `\` com histórico (*backlog*), tags `~i~`/`~b~`/`~c~`/`~s~`, escape `~~` para til literal, cores `#RRGGBB` e ligaduras tipográficas (`...` -> `…`, `--` -> `–`, `---` -> `—`).
- **Persistência de Save e Extras (`Ponscripter::SaveSerializer`)**: Persiste variáveis globais (`gloval.sav`: `%lic_accepted`, `%end1_clear..%end3_clear`, `%cgset_0..4`, `%gro`, `%vol*`, `%textspeed`, `%autospeed`, `%movplay`) e 20 slots de save (`save1.dat`..`save20.dat`), desbloqueando a Galeria de CGs (`97` CGs), Music Box (`15` faixas) e Comentários da Equipe nos 3 finais.

### 3. UI compartilhada de Kotonoha
- **Menu de pausa (`InGameMenu`)**: o runtime usa o menu compartilhado do motor em vez do antigo `N2GameMenu`; as ações de configuração são encaminhadas ao `Engine`, enquanto a ação de retornar ao menu atualmente solicita saída da aplicação.
- **Histórico (`VNBacklog`)**: `B` alterna o backlog da VM. Com uma cena modal aberta, os eventos são direcionados à cena que captura a entrada.
- **Salvamento rápido**: `F5` salva em `n2system_quick.n2sv` e `F6` carrega esse arquivo. Não há navegador `VNSaveMenu` próprio ativo neste runtime.
- **Texto de Steins;Gate**: a fonte TTF original é carregada por meio do provider para `Kotonoha::UI`, que usa cache e desenho de texto do renderizador, evitando criar superfícies para cada frame.
- **Vídeos**: vídeos de fundo em loop, como os do título, não são dispensáveis por ações genéricas de avanço nem bloqueiam a navegação; vídeos narrativos sem loop respeitam os gestos explícitos de interrupção.

**Validação pendente:** a integração completa com os arquivos comerciais e as plataformas suportadas ainda exige testes de gameplay, vídeo/áudio, renderização de texto e save/load. As verificações sintéticas não garantem paridade integral com o N2System original.
