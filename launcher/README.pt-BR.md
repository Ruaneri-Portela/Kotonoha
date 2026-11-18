# Kotonoha Launcher Unificado Multi-Motor (`KotonohaLauncher`)

[English](README.md)

A pasta `launcher/` fornece o launcher unificado multi-motor e o registro modular de bibliotecas de motores (`kotonoha_unified_launcher` / `KotonohaLauncher`), mantido fora de `games/` como um componente host de nível superior.

## Recursos Principais

1. **Detecção Multi-Motor Kotonoha (`detectKotonohaEngine`)**:
   - Inspeciona pastas, imagens `.iso` e discos PS2 Mode 1 / Mode 2 (`.bin`) via `Kotonoha::Filesystem` VFS V2.
   - Identifica todas as famílias de motores do Kotonoha (LucaSystem, RealLive / rlBabel, KiriKiri2/Z, Ren'Py + patches VNX, N2System, PONScripter/ONScripter, Amagami PS2 SCF, School Days HQ GPK, Pure Mail ADV, Snow Radish ADV, Summer Radish / MEO CMP, Zero no Tsukaima PS2 Squirrel/KAG).
   - Associa automaticamente patches/overlays de tradução (como `DDLC_PT-BR_Patch` -> `DDLC`).

2. **Motores Construídos como Bibliotecas Modulares (`EngineLibraryRegistry`)**:
   - Mapeia cada `EngineKind` para sua biblioteca CMake modular (`luca_system_formats`, `reallive_port`, `kirikiri_formats`, `doki_doki_literature_club`, `n2system_formats`, `ponscripter_port`, `amagami_port`, `school_days`, `PmailPort`, `snow_radish_vacation`, `summer_radish_vacation`, `zero_no_tsukaima`).
   - Inicia executáveis modulares no desktop. `EngineLibraryFactory` permite implementações futuras, mas nenhuma fábrica de runtime em processo é registrada por padrão.

3. **Divisão da Pasta de Save e Config por Jogo (`splitSavesByGame`)**:
   - Isola `KOTONOHA_USER_DATA_DIR` e `settings.json` em `<SaveRoot>/Kotonoha-Profiles/<saveProfileId>/Kotonoha/<packageId>/save` para que múltiplos títulos do mesmo motor (ex.: `AIR_PT-BR`, `AIR-SE.ISO`, `Clannad_PT-BR`, `KANON-SE.ISO`, `PLANETARIAN.ISO` no RealLive; `SummerRadishVacation`, `SummerRadishVacation2`, `MEO` no CMP; `ZeroNoTsukaima0..2` no Zero) nunca entrem em conflito.

## Uso via CLI

```bash
# Escanear uma biblioteca completa de jogos e exibir a matriz de motores e perfis de save
KotonohaLauncher --scan-library "/caminho/para/biblioteca-de-jogos"

# Detectar um único jogo (pasta ou imagem de disco)
KotonohaLauncher --detect "/caminho/para/biblioteca-de-jogos/SayaNoUta_PT-BR"

# Abrir o Launcher Unificado ImGui pré-carregado com uma biblioteca
KotonohaLauncher --library "/caminho/para/biblioteca-de-jogos"
```

O launcher independente é compilado no desktop (Linux, macOS e Windows), mas não no Android nem no WebAssembly. Detecção de fontes e testes sintéticos não comprovam compatibilidade integral com os jogos originais.
