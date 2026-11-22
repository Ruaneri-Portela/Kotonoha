# Fluxo de Trabalho do Fork Kotonoha v3 (`tsubaki-0x/Kotonoha`)

[English (US)](FORK_WORKFLOW.md) | **Português (Brasil)**

Este guia documenta a arquitetura ativa, a política de branches, os procedimentos de compilação DEV/DIST, as regras de testes isolados e o fluxo de contribuição upstream do fork `tsubaki-0x/Kotonoha` voltado ao **School Days HQ v1.02** em **Windows x64** e **Android (`arm64-v8a`)**.

---

## 1. Arquitetura Compartilhada Windows e Android

As duas plataformas executam uma única base de código compartilhada para a lógica do School Days HQ, sem duplicar regras do jogo:

- `lib/`: motor genérico em C++17, integração SDL3/ImGui/FFmpeg/libass, `Filesystem` V2 (`FileHandle`, `IMount`, `SDLFileSource`), `ForcedSubtitleRenderer`, `DebugOverlay` e adaptadores móveis genéricos (`lib/include/Kotonoha/mobile/`).
- `modules/`: máquinas virtuais de script (`modules/vms/`) e provedores de pacotes (`modules/providers/`).
- `games/school-days/`: runtime compartilhado do School Days HQ, roteamento KTRF/ORS, durabilidade de Save/Load/GlobalFlag, Replay, Route Map, BackLog, política retail `Config.DAT`, interface Display Options e testes CTest unitários/de runtime.
- `buildsystems/android/`: configuração Gradle, ponte JNI, setup portrait via SAF e buildType Android `dist` (`BuildConfig.IS_DISTRIBUTION_BUILD`).

---

## 2. Branches Permanentes e Alinhamento com o Upstream

| Branch | Papel |
|---|---|
| `dev` | Branch padrão de `tsubaki-0x/Kotonoha`; linha principal de desenvolvimento e base limpa para colaboração upstream. |
| `kotonoha-v3-ruaneri-sync-transitions` | Branch híbrida de distribuição que preserva a infraestrutura DEV/DIST do School Days HQ e os fluxos Windows/Android específicos do fork. |
| `upstream/dev` (`Ruaneri-Portela/Kotonoha`) | Repositório upstream de referência. Nunca faça push diretamente para `upstream`. |

- Atualize referências remotas com `git fetch origin --prune` e `git fetch upstream --prune`.
- Crie branches de PR upstream sempre a partir de `upstream/dev`, com commits pequenos e bem delimitados, zero símbolos de School Days dentro de `lib/` e zero assets proprietários.
- Nunca utilize force-push em branches publicadas (`dev` ou `kotonoha-v3-ruaneri-sync-transitions`).

---

## 3. Política DEV vs DIST e Compilação Windows x64

Sempre compile builds Windows virgens em **diretórios CMake novos** usando `Visual Studio 18 2026` (`-A x64`), configuração `Release` (`--config Release --parallel 1`), SDL3/SDL3_ttf/ImGui via submódulos e dependências auxiliares de `x64-windows-static-md`.

### Flags CMake Obrigatórias
```cmake
-DKOTONOHA_BUILD_ALL=OFF
-DKOTONOHA_BUILD_SCHOOL_DAYS=ON
-DBUILD_TESTING=ON
-DKOTONOHA_SDL_FROM_VCPKG=OFF
-DKOTONOHA_IMGUI_FROM_VCPKG=OFF
-DKOTONOHA_DEV_CONTROL=OFF
-DKOTONOHA_ENABLE_PLAYER_DUBBING=OFF
```

- **Windows DEV**: `-DKOTONOHA_DISTRIBUTION_BUILD=OFF -DKOTONOHA_ENABLE_MCP=ON` (subsistema `WINDOWS_CUI`; `DebugOverlay` e MCP habilitados).
- **Windows DIST**: `-DKOTONOHA_DISTRIBUTION_BUILD=ON -DKOTONOHA_ENABLE_MCP=OFF` (subsistema `WINDOWS_GUI` — sem console adicional; `DebugOverlay`, MCP e flags CLI de DEV `--mcp`, `--mcp-port`, `--mcp-root`, `--log` excluídos da compilação).

### Estrutura de Distribuição Windows em Duas Fases (QA Privado)
1. **Fase A (Compilar)**: gerar binários DEV e DIST virgens em diretórios CMake separados e aprovar CTest.
2. **Fase B (Montar)**: empacotar cada distribuição de forma limpa sem copiar `.pdb`, `.lib`, caches, configurações pessoais ou os 29 GPKs retail:
   - `SchoolDays.exe`, `SDL3.dll`, `SDL3_ttf.dll`, `imgui.dll`, `kotonoha.dll`
   - `translations/jsons/` (10 JSONs de idioma: `en`, `pt-BR`, `es`, `ru`, `zh-CN` + `*-lxh.json`) e `translations/scripts/` (9 GPKs privados de tradução para QA)
   - `overlay/System/Option/` (10 arquivos PNG/CMAP do overlay Display Options)
   - `game-assets/` (`school-days-hq.ktnroute`, `styles.ass` e `forced_subtitles/` com 198 arquivos `.ass`: 33 na raiz legacy + 33 por idioma)
   - `assets/` (`SPLASH.PNG`, `SchoolDays.ico`, `SchoolDaysHQ-Launcher.png`, `Anime4K/`, `ArtCNN/`, `fonts/NotoSansJP-Regular.ttf`)
   - `third-party-licenses/` (16 avisos de licença)

**Validação obrigatória da fonte no QA Windows (DEV e DIST):** A fonte
da launcher é privada, não rastreada pelo Git e não deve ser copiada às cegas
de `assets/fonts/` de um checkout. O placeholder de 1.204 bytes deixou o
texto invisível; a fonte correta tem 5.733.060 bytes (SHA-256
`7c8597677e9fac0f54d7848ad18bc6a708dfb5baa4ebf4bd91e66efcca313bf3`).
Configure cada build Windows com
`-DKOTONOHA_SCHOOL_DAYS_LAUNCHER_FONT_SOURCE=<ttf-local-aprovada>`.
O estágio pós-build do School Days verifica e copia a fonte **depois** dos
assets genéricos do motor. Antes do empacotamento, execute
`cmake --build <dir-build> --config Release --target SchoolDaysVerifyLauncherFont`.
Após montar cada pasta DEV/DIST de QA, valide a cópia **final** (depois de
todos os assets e antes de criar o ZIP):

```powershell
cmake "-DKOTONOHA_FONT_ACTION=verify" "-DKOTONOHA_FONT_DESTINATION=<pasta-qa>/assets/fonts/NotoSansJP-Regular.ttf" -P games/school-days/windows/StageLauncherFont.cmake
```

Para reparar a pasta de montagem com uma fonte local aprovada, use o mesmo
script com `-DKOTONOHA_FONT_ACTION=stage`,
`-DKOTONOHA_FONT_SOURCE=<ttf-local-aprovada>` e o destino acima.
Ambos os modos falham com fonte ausente, tamanho incorreto ou hash divergente;
nenhum deles adiciona a fonte ao Git. Nunca gere um ZIP após falha na
verificação. Execute `ctest -R SchoolDays.LauncherFontStage` para testar
a rejeição de placeholders.

Os scripts legados L4 de duas DLLs (`games/school-days/tools/stage_clean_local_l4.ps1`, `publish_two_dll_l4.ps1`, `stage_windows_clean_local.py`) são históricos e não devem ser executados às cegas sobre instalações pessoais.

---

## 4. APKs Android DEV/DIST e SAF

- As builds Android ficam em `buildsystems/android/` e produzem APKs isoladas DEV (`me.hirameki.kotonoha.schooldays`) e DIST (`me.hirameki.kotonoha.schooldays.dist`) via Gradle (`assembleSchoolDaysDebug` / `assembleSchoolDaysDist`).
- A launcher inicial em retrato permite ao jogador selecionar a pasta externa `SchoolDaysHQ` via Storage Access Framework (SAF); os 29 GPKs retail originais executam de forma independente e o `LxH.GPK` opcional é controlado por `EnableLxh=0|1` em `Kotonoha.ini` (e pelo checkbox/switch das launchers Windows/Android, além das flags CLI `--lxh` / `--no-lxh`). Consulte [OPTIONAL_LXH_AND_PERFORMANCE.pt-BR.md](../games/school-days/docs/OPTIONAL_LXH_AND_PERFORMANCE.pt-BR.md).
- O gameplay roda em paisagem usando o runtime compartilhado de `games/school-days/`, com splash `SPLASH.PNG` do motor, ícones derivados de `SchoolDays.ico`, banner recortado e rótulo persistente `Display Options`.
- Valide as APKs estaticamente com `python3 tools/android/check_school_days_apk.py` e execute sempre uma passagem física própria no aparelho de referência (**POCO X7 Pro**) antes de declarar prontidão em hardware.

---

## 5. Testes Isolados e Política de Assets Comerciais

- Execute validações headless com `SDL_VIDEO_DRIVER=dummy` e `SDL_AUDIO_DRIVER=dummy`:
  - `python3 tools/ValidateBuildMetadata.py`
  - `python3 tools/check_game_platform_isolation.py`
  - `python3 tools/check_hybrid_android_contracts.py` (na branch híbrida)
  - `git diff --check`
  - Baseline Windows DEV: `65/65` CTests direcionados (`97/98` na suíte completa; falha histórica `#1 Kotonoha.Infrastructure` restrita ao stub Apple MetalFX em software renderer no Windows).
  - Baseline Windows DIST: `6/6` CTests DIST direcionados e `91/91` na suíte completa DIST.
- Para QA interativo ou com dados reais, passe sempre `--documents <pasta-temp>` e `--config <arquivo-temp>` isolados para que saves reais (`Save/`, `GlobalFlag.DAT`) e `Config.DAT` nunca sejam modificados.
- **Fronteira de Assets Comerciais e Privados**: nunca comite nem redistribua publicamente os 29 GPKs retail, `LxH.GPK`, GPKs/JSONs privados de tradução, saves pessoais ou artes sem autorização. Mantenha ZIPs privados de QA estritamente locais até uma auditoria dedicada de licenças.

---

## 6. Limitações Conhecidas

- O upstream `Ruaneri-Portela/Kotonoha:dev` utiliza uma launcher Android unificada multi-jogos (`LauncherActivity.java` + `SafBridge.java`) e dual-ABI (`arm64-v8a` + `armeabi-v7a` NEON); contribuições Android upstream devem integrar-se a essa arquitetura sem substituí-la.
- QA físico no POCO X7 Pro e QA visual em GPU real no Windows devem sempre ser relatados separadamente das validações CTest, Gradle e estáticas.
