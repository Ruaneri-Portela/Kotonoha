# Kotonoha Launcher Unificado Multi-Motor (`KotonohaLauncher`)

[English](README.md)

A pasta `launcher/` fornece o launcher unificado multi-motor e o registro modular de bibliotecas de motores (`kotonoha_unified_launcher` / `KotonohaLauncher`), mantido fora de `games/` como um componente host de nível superior.

## Recursos Principais

1. **Detecção Multi-Motor Kotonoha (`detectKotonohaEngine`)**:
   - Inspeciona pastas, imagens `.iso` e discos PS2 Mode 1 / Mode 2 (`.bin`) via `Kotonoha::Filesystem` VFS V2.
   - Identifica todas as famílias de motores do Kotonoha (LucaSystem, RealLive / rlBabel, KiriKiri2/Z, Ren'Py + patches VNX, N2System, PONScripter/ONScripter, Amagami PS2 SCF, School Days HQ GPK, Pure Mail ADV, Snow Radish ADV, Summer Radish / MEO CMP, Zero no Tsukaima PS2 Squirrel/KAG).
   - Associa automaticamente patches/overlays de tradução (como `DDLC_PT-BR_Patch` -> `DDLC`).

2. **Runtimes vinculados diretamente (`EngineLibraryRegistry`)**:
   - Com `KOTONOHA_BUILD_ALL=ON`, o CMake linka as bibliotecas dos **12 jogos reais** (sem Example) diretamente em `kotonoha_unified_launcher`. O arquivo `GameFactories.cpp` registra construtores concretos de `Runtime` e dos provedores de assets.
   - O `KotonohaLauncher` instancia `Runtime` + `Engine` **no mesmo processo**. Ao sair do jogo, libera os recursos, restaura o ambiente de saves e reabre o hub. Não invoca os `main.cpp` nem executáveis secundários.
   - O Example permanece como aplicativo independente e nunca é registrado nem linkado no hub. O launcher não é criado quando se compila um único port.

3. **Divisão da Pasta de Save e Config por Jogo (`splitSavesByGame`)**:
   - Isola `KOTONOHA_USER_DATA_DIR` e `settings.json` em `<SaveRoot>/Kotonoha-Profiles/<saveProfileId>/save` (omitindo o nome do pacote no modo unificado e em mobile/WASM) para que múltiplos títulos do mesmo motor (ex.: `AIR_PT-BR`, `AIR-SE.ISO`, `Clannad_PT-BR`, `KANON-SE.ISO`, `PLANETARIAN.ISO` no RealLive; `SummerRadishVacation`, `SummerRadishVacation2`, `MEO` no CMP; `ZeroNoTsukaima0..2` no Zero) nunca entrem em conflito.

## Compilar com as bibliotecas dos jogos

```sh
# Todas as bibliotecas dos runtimes desktop ligadas diretamente ao hub
cmake -S . -B build -DKOTONOHA_BUILD_ALL=ON
cmake --build build --target KotonohaLauncher

# Compilar um único port NÃO cria o alvo KotonohaLauncher.
# O Example continua independente, fora do launcher.
```

A compilação completa exige as dependências de todos os ports, incluindo Python de desenvolvimento para DDLC quando necessário. O launcher é compilado somente com `KOTONOHA_BUILD_ALL=ON`, em plataformas desktop nativas, e exclui o Example. No macOS, o bundle do hub também inclui a biblioteca padrão do Python para o DDLC.

## Varredura mais rápida da biblioteca

O scanner utiliza um **mount leve e sob demanda, exclusivo da detecção**:
consulta apenas os caminhos necessários para identificar o motor sem indexar
todos os assets e texturas em subpastas. A execução do jogo continua usando o
índice completo do VFS somente-leitura. Resultados reconhecidos e negativos
são armazenados em cache, com fingerprints limitados às pastas/arquivos de
metadados, atualização periódica por hora e gravação uma vez por varredura.

A descoberta examina a pasta selecionada e suas subpastas imediatas/imagens
de disco, não o disco inteiro. Selecione a pasta que contém as pastas de jogos
ou uma pasta de jogo individual.

## Uso via CLI

```bash
# Escanear uma biblioteca completa de jogos e exibir a matriz de motores e perfis de save
KotonohaLauncher --scan-library "/caminho/para/biblioteca-de-jogos"

# Detectar um único jogo (pasta ou imagem de disco)
KotonohaLauncher --detect "/caminho/para/biblioteca-de-jogos/SayaNoUta_PT-BR"

# Abrir o Launcher Unificado ImGui pré-carregado com uma biblioteca
KotonohaLauncher --library "/caminho/para/biblioteca-de-jogos"
```

O launcher unificado é compilado em plataformas desktop (Linux, macOS, Windows) e mobile (Android, iOS), mas não no WebAssembly. Detecção de fontes e testes sintéticos não comprovam compatibilidade integral com os jogos originais.
