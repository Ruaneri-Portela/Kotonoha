# Revisão da compatibilidade dos portes — 16/11/2026

[English (US)](PORT_COMPATIBILITY.md)

Esta matriz cobre **todas as aplicações** em
`buildsystems/xcode/ProjectManifest.json`. O escopo fundamentado no código e
nos testes é diferente da compatibilidade com o jogo original. Abertura
de arquivos e testes sintéticos bem-sucedidos **não comprovam** que todos os
comandos, cenas, rotas, transições ou finais funcionem.

| Porte | Cobertura existente e limites | Avanço desta revisão |
| --- | --- | --- |
| Example | Demonstração de cenas, interface e motor compartilhado; não necessita de jogo comercial | ID do runtime alinhado ao bundle com migração conservadora dos arquivos legados; smoke test nativo de inicialização |
| Amagami | Prólogo e mapa inicial implementados; calendário, rotas posteriores e finais ainda não verificados | Validação do startup e do identificador de assets |
| Zero no Tsukaima | Nocturne jogável; cobertura distinta para Concerto e Symphony | Verificação de inicialização e metadados; comparação multidisco pendente |
| Snow Radish Vacation!! | ADV3 jogável; efeitos originais e temporização incompletos | Verificação de inicialização e metadados |
| Summer Radish Vacation!! | CMP/scripts/mídia/entrada testados; rotas e finais não certificados | Verificação de inicialização e metadados |
| Doki Doki Literature Club! | Ren'Py, RPA e CPython; dependência da edição e do patch | Verificação unificada no VFS de `scripts.rpa` e `images.rpa`, inclusive em `game/` e arquivos montados |
| School Days HQ | Testes de GPK, vídeo, menus e saves; sem certificação de todas as rotas | Verificação de startup; entrada específica mantém bloqueio intencional de eventos ainda não autenticados |
| RealLive | ISO, cenários, mídia e vários opcodes; apresentação integral não verificada | Validador VFS exige arquivos regulares `Seen.txt` e `Gameexe.ini` |
| Kirikiri2/KirikiriZ | XP3/TLG/KAG e TJS2 experimental; TJS/plugins originais ainda incompletos | Provider XP3 passado ao runtime **e** ao motor, evitando o provider genérico |
| LucaSystem | Cobertura dos PAKs e VM de AIR; outros jogos não certificados | Correção do provider, slug de assets, validação ISO/VFS sem diferenciar caixa e migração conservadora do ID legado |
| Pure Mail | Script/gráficos do DVD e começo da história; computador, timing e rotas incompletos | Remoção de caminhos específicos de um computador, launcher, fonte lembrada e reconhecimento de ISO/pastas no VFS |
| PONscripter / ONScripter | Novo port independente CMake/Android, com VM, arquivos SAR/NSA, runtime SDL3 e testes sintéticos; paridade comercial não comprovada | Registrado como alvo independente; schemes Xcode macOS/iOS pendentes |
| N2System | VM NsScript/NPA/AFS, grafo de cenas e testes sintéticos de saves; sem comprovação de paridade de jogos originais | Integrado ao CMake/Android e à auditoria de inicialização; targets/schemes Xcode ainda pendentes |

## Launcher e plataformas (18/11/2026)

- Todos os 12 launchers de jogos (mais o demo Example) seguem a política compartilhada: abertura interativa com launcher por padrão; `--no-launcher`, `--frames N`, exportação de tradução e MCP executam diretamente. Descoberta automática de arquivos não pode anular a escolha do usuário.
- `Kotonoha.CommandLine` testa o modo automatizado e `check_port_contracts.py` detecta tentativas de forçar launcher no port ou deixar o jogo fora do índice.
- N2System e PONscripter possuem CMake/Android, porém **não há schemes Xcode macOS/iOS para esses ports** no projeto versionado. O manifesto registra `xcode_pending` para não anunciar suporte Apple não comprovado.
- Testes sintéticos verificam contratos, não substituem validação completa em dispositivos nem com mídia original.

## Integração compartilhada com a UI de Kotonoha (18/11/2026)

Todos os portes conectam o menu in-game e o histórico nativos aos componentes
do motor, diretamente ou por subclasses específicas do jogo:

- Example, RealLive, Kirikiri2/KirikiriZ, LucaSystem, Summer Radish e N2System usam `InGameMenu`, `VNBacklog` e `VNSaveMenu`; o N2System cria uma subclasse do menu para que Histórico abra o backlog que ele popula.
- Amagami, Zero no Tsukaima, Snow Radish, Doki Doki Literature Club, Pure Mail e PONscripter preservam rótulos, desenho e entrada nativos por meio de subclasses de `InGameMenu` e/ou `VNBacklog`.
- Saya no Uta/PONscripter mapeia o `rmenu` nativo para um `InGameMenu` de quatro itens, o `lookback` nativo para `VNBacklog` e `save`/`load` nativos para `VNSaveMenu`; a VM permanece em `WaitSystemCall` até cada modal fechar.
- School Days HQ mantém o MenuBar/SysMenu nativo autenticado e a pele de histórico; `VNSaveMenu` e `SchoolDaysBackLogEngine` (subclasse de `VNBacklog`) permanecem registrados como fallback.
- Verificado nesta mudança com `AmagamiEngineTest` (disco comercial), `PonscripterRuntimeTests` (Saya no Uta sintético e comercial) e a suíte `N2System` (7 testes).

## Testes de contrato e validação sintética

- `tools/check_port_contracts.py` revisa todos os aplicativos: manifesto,
  targets CMake, callbacks SDL, identificadores de bundle e de assets e
  presença de testes C++.
- `lib/tests/GameSourceProbeTests.cpp` testa fontes virtuais sintéticas,
  maiúsculas/minúsculas, diretórios e grupos de arquivos obrigatórios.
- `kotonoha_configure_app` registra automaticamente
  `Kotonoha.PortHelp.<target>` para os executáveis nativos selecionados.
  O teste verifica inicialização do CLI/dependências sem assets comerciais.
- O workflow `port-contracts.yml` valida os contratos em PRs relevantes e
  na branch `dev`.

Sequência recomendada em uma máquina com as dependências do projeto:

```sh
python3 tools/check_port_contracts.py
python3 tools/check_game_platform_isolation.py
cmake -S . -B build -DKOTONOHA_BUILD_ALL=ON -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build -R 'Kotonoha\\.(PortHelp|GameSourceProbe)' --output-on-failure
```

Em cada porte, testar depois boot com assets legítimos, avanço de cenas,
escolhas, salvar/carregar, entrada touch/gamepad e temporização de áudio/vídeo
nas plataformas atendidas. Não incluir os dados comerciais no Git.

## Limites importantes

- A restrição de entrada do gameplay em School Days é intencional: aceitar
  eventos SDL não significa que os comandos correspondem ao jogo original.
- Encontrar nomes de arquivos numa ISO comprova apenas o layout de entrada,
  não a decodificação ou a jogabilidade.
- No LucaSystem e no exemplo, o diretório legado só é movido se existir, não for symlink
  e o destino ainda não existir; dados novos nunca são sobrescritos.
- **Não foram executadas** compilações completas de macOS/iOS/Android/Linux/
  Windows/WebAssembly nem validações com mídia comercial nesta revisão.
  A compatibilidade completa permanece condicionada a testes reais.
