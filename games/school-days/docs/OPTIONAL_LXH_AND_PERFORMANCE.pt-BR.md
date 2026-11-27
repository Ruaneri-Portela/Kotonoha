# School Days HQ — L×H Opcional e Otimização de Desempenho no Windows

[English (US)](OPTIONAL_LXH_AND_PERFORMANCE.md) | **Português (Brasil)**

Este documento descreve a arquitetura opcional de **L×H (7 epílogos estendidos)** compartilhada entre Windows e Android, juntamente com as otimizações de desempenho baseadas em evidência no Windows x64 e os benchmarks com dados reais concluídos em 22 de novembro de 2026.

---

## 1. Arquitetura do L×H Opcional

### Contrato Principal
- **Independência da Base HQ**: o School Days HQ funciona integralmente apenas com os **29 GPKs retail originais**. `Packs/LxH.GPK`, `*-lxh.GPK`, `*-lxh.json` e `forced_subtitles/` são todos estritamente opcionais.
- **Separação de Estados**:
  1. **Preferência do usuário** (`EnableLxh=0|1` em `Kotonoha.ini`, persistida ao lado de `ContentAspectPreference` e `AndroidFrameLimit`).
  2. **Disponibilidade de conteúdo** (validação sem mutação de `Packs/LxH.GPK` — 28 entradas raw — e da tradução L×H correspondente ao idioma selecionado).
  3. **Estado efetivo em execução** (`effectiveEnabled` / `assets.lxhMounted()`).
- **Padrão em Instalação Limpa**:
  - Sem `EnableLxh` explícito + `Packs/LxH.GPK` válido presente → padrão **ON**.
  - Sem `EnableLxh` explícito + `Packs/LxH.GPK` ausente ou inválido → padrão **OFF**.
- **Preservação de Preferência Explícita**: marcar ou desmarcar `Enable LxH endings` é persistido literalmente em `Kotonoha.ini`. Se o usuário escolher ON enquanto `Packs/LxH.GPK` ou a tradução L×H do idioma escolhido estiver ausente/incompleta, a preferência permanece salva, uma mensagem objetiva informa o motivo da indisponibilidade e o School Days HQ base inicia normalmente sem fallback silencioso entre idiomas.

### Controles nas Launchers e Flags CLI
- **Launcher Windows DEV e DIST** (`SchoolDaysLauncher`): checkbox `Enable LxH endings` na aba `General` com status em tempo real, reversão via snapshot em Cancel e persistência atômica em `Kotonoha.ini`.
- **Launcher Android DEV e DIST** (`SchoolDaysSetup`, `SchoolDaysFolder`, `SchoolDaysConfigStore`): `Switch` (`Ativar finais L×H` / `Enable L×H endings`) com status em tempo real, aceitação apenas dos 29 GPKs retail e repasse explícito de `--lxh` / `--no-lxh` ao runtime nativo.
  - *Nota*: a implementação Android, os 11 casos sintéticos do `PackCaseRegression` e a validação XML estão concluídos; **Android: implementação preparada; QA físico pendente no POCO X7 Pro.**
- **Flags CLI** (DEV e DIST): `--lxh` e `--no-lxh` sobrepõem a resolução naquela sessão sem alterar o valor salvo em `Kotonoha.ini`.

### Comportamento em Runtime e Proveniência de Saves
- **L×H OFF**: `Packs/LxH.GPK` e `*-lxh.GPK` nunca são montados. Os 7 scripts finais (`05-A2-F00`, `05-A2-Z00`, `05-B0-F00`, `05-G0-L03`, `05-KC-F00`, `05-SB-E01`, `05-SC-L03`) vêm exclusivamente do `Packs/Script.GPK` retail base (ou do `Script - <Idioma>.GPK` base), sem fades nem mídia L×H.
- **L×H ON**: todos os 7 epílogos, transições de fade L×H e validação estrita de tradução L×H permanecem íntegros.
- **Proteção de Proveniência de Saves**:
  - Saves normais do HQ e saves legados (sem sidecar `.kotonoha-lxh.v1`) carregam com **L×H ON** e **L×H OFF**.
  - Saves atestados de epílogo L×H carregam com **L×H ON** e são recusados com segurança **antes de qualquer mutação do estado do runtime** com aviso claro quando **L×H está OFF ou indisponível** — sem deletar nem corromper o arquivo de save ou o sidecar.

---

## 2. Otimizações de Desempenho no Windows x64

### Ambiente de Benchmark
- **SO**: Microsoft Windows 10 Pro (`10.0.19045`, x64)
- **CPU**: Intel Xeon E5-2690 v4 @ 2.60 GHz
- **RAM**: 23,9 GB
- **GPU**: NVIDIA GeForce RTX 3060 (OpenGL + MPV Upscaler)
- **Compilador / Build**: MSVC 19.51 (`Visual Studio 18 2026`), C++17, `Release` (`/O2 /MD`), SDL3 + SDL3_ttf + ImGui de submódulos, vcpkg `x64-windows-static-md`
- **Base de Dados**: conjunto retail somente leitura de 30 GPKs (29 retail + `LxH.GPK`), `pt-BR.json` (2,19 MiB), `pt-BR-lxh.json`, `Script - Brazilian.GPK`, `pt-BR-lxh.GPK`, `--documents` temporário isolado

### Otimizações Implementadas com Evidência
1. **Cache Lazy de Índice de Diretórios em `SchoolDaysGpkArchive::ListDirectory()`**: substituiu a varredura linear `O(N)` com dobramento de caminho por entrada a cada chamada por um índice de diretórios construído sob demanda sob mutex, preservando integralmente a detecção de colisões case-insensitive. Eliminou travamentos de ~12,4 ms a cada troca de fundo no gameplay (`discoverAbcForBackground()`).
2. **Reuso de Stream de Arquivo em Desktop + Leitura Raw Zero-Copy (`SchoolDaysGpkArchive::ReadEntry`)**: estendeu o `cachedStream_` protegido por mutex (antes restrito ao Android) para o desktop com reabertura automática em caso de erro, leitura direta em `*out` para entradas raw sem prefixo inline e troca de `out->assign(size, 0)` por `out->resize(size)` em `ReadRange()`.
3. **Eliminação de Leitura Duplicada de ORS em `ValidateTranslatedScript()`**: removeu a leitura/descompressão zlib em duplicidade dos 1.855 scripts ORS ao validar pacotes `Script.GPK` traduzidos.
4. **Otimização de `SchoolDaysTranslationGate::CollectDetailed()` e `CheckLayered()`**: pré-alocação de `sources`, cache de ponteiros de seção por nó consecutivo e rastreamento de colisões zero-copy com `std::string_view` em `unordered_map` no lugar de `std::map<pair<int, string>, set<string>>`.
5. **Caminhos Quentes por Frame sem Alocação em Renderização**: cache de `choiceScratchLines` em `GameScene` (eliminando reconversão UTF-16 e quebra de linhas a cada frame de escolha) e comparação direta de campos em `SubtitleScene::render()` (eliminando 6+ alocações de `std::string` por frame).

### Resultados Medidos ANTES vs DEPOIS (Windows x64 Release)

| Cenário (Dados Retail Reais, Mediana de 5 execuções salvo nota) | ANTES | DEPOIS | Ganho / Delta |
|---|---:|---:|---:|
| `ListDirectory` ×120 pastas `Event*` (transições ABC) | 1.485,50 ms | **8,38 ms** | **177,3× mais rápido (−99,4%)** |
| Leitura de 500 entradas `Script.GPK` (529 KiB) | 30,74 ms | **6,36 ms** | **4,83× mais rápido (−79,3%)** |
| `CollectDetailed` 1.855 ORS (L×H OFF, 30.968 slots) | 836,18 ms | **342,32 ms** | **2,44× mais rápido (−59,1%)** |
| `CollectDetailed` 1.855 ORS (L×H ON, 31.241 slots) | 944,59 ms | **436,62 ms** | **2,16× mais rápido (−53,8%)** |
| Montagem `Script - Brazilian.GPK` (L×H OFF, 3 execuções) | 1.272,65 ms | **558,83 ms** | **2,28× mais rápido (−56,1%)** |
| Montagem `Script - Brazilian.GPK` + `pt-BR-lxh.GPK` (L×H ON, 3 exec.) | 1.239,17 ms | **556,20 ms** | **2,23× mais rápido (−55,1%)** |
| Auditoria `CheckLayered` `pt-BR` (L×H OFF, 30.968 slots) | 36,24 ms | **18,14 ms** | **2,00× mais rápido (−49,9%)** |
| Auditoria `CheckLayered` `pt-BR` (L×H ON, 31.241 slots) | 37,99 ms | **15,41 ms** | **2,47× mais rápido (−59,4%)** |
| Montagem dos 29 GPKs retail originais (L×H OFF) | 156,04 ms | **125,49 ms** | **1,24× mais rápido (−19,6%)** |
| Montagem dos 30 GPKs (L×H ON) | 152,36 ms | **126,60 ms** | **1,20× mais rápido (−16,9%)** |
| `Runtime::loadGame` (20 execuções, temp isolado) | 1,30 ms | **1,07 ms** | **1,21× mais rápido (−17,7%)** |
| `Runtime::saveGame` (20 execuções, temp isolado) | 1,02 ms | **1,04 ms** | Paridade |
| GPU real `SchoolDays.exe` (`--no-lxh`, 90 frames, VSync) | 4.050,4 ms | **4.039,0 ms** | Paridade (limitado por VSync) |
| GPU real `SchoolDays.exe` (`--lxh`, 90 frames, VSync) | 4.044,8 ms | **4.030,3 ms** | Paridade (limitado por VSync) |
| Pico de Memória (Working Set) | 57,6 MiB | **56,6 MiB** | **−1,0 MiB** |

Nenhum timing narrativo, qualidade audiovisual, estilo de legenda, upscaler ou formato de save foi alterado.
