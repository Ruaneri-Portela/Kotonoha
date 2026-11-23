# Kotonoha — Verificação de Paridade Retail (2026-11-23)

Este documento registra a evidência objetiva de paridade coletada em 2026-11-23.

## Metodologia

- Build dedicado: `out/build/retail-verify` (macOS arm64, vcpkg, triplet
  `macos-arm64-vcpkg`, `CMAKE_BUILD_TYPE=Release`).
- Todos os gates de dados retail populados apontando para
  `/Volumes/Nero 2TB/KotonohaAssets` (somente leitura).
- Filtro CTest: `-L real` (minúsculo; `-L REAL` retorna 0 — erro de casos já
  corrigido nos relatórios).
- Testes que não possuem dados retornam `77` e são registrados com
  `SKIP_RETURN_CODE 77`, aparecendo como `Skipped`, nunca como `Passed` falso.

## Resultado

```
$ ctest --test-dir out/build/retail-verify -L real -j4
100% tests passed out of 77
Label Time Summary: real = 201.69 sec*proc (77 tests)
```

Duas rodadas consecutivas com todos os gates: **77/77 em ambas** (estável).

## Cobertura por jogo

| Jogo | Gate | Testes `real` | Status |
|---|---|---:|---|
| AIR (LucaSystem PAKs) | `LUCA_SYSTEM_TEST_DATA` | 5 | PASS |
| AIR_PT-BR (RealLive) | `REALLIVE_TEST_DATA` | 2 + Session | PASS |
| Steins;Gate (N2System) | `KOTONOHA_STEINSGATE_DIR` | 1 | PASS |
| NUKITASHI + NUKITASHI 2 (Artemis) | `KOTONOHA_ARTEMIS_RETAIL_DIR` | 1 (ambas edições) | PASS |
| DDLC + DDLC PT-BR Patch | `DDLC_DATA_DIR`, `DDLC_OVERLAY_DIR` | 8 | PASS |
| Katawa Shoujo | `KATAWA_DATA_DIR` | 2 | PASS |
| Amagami | `AMAGAMI_DISC_IMAGE` | 4 | PASS |
| Clannad PT-BR (parcial) | `CLANNAD_TEST_DATA` | SpriteAtlas | PASS |
| School Days HQ | `SCHOOL_DAYS_DATA_DIR` | 17 | PASS |
| Snow Radish Vacation | `SNOW_DATA_DIR` | 4 | PASS |
| Summer Radish 1/2 | `SUMMER_DATA_DIR` | 5 | PASS |
| Zero no Tsukaima 0/1/2 | `ZERO_*_DISC_IMAGE` | 5 | PASS |
| MEO | `MEO_DATA_DIR` | 6 | PASS |
| Pure Mail | `PMAIL_TEST_ASSET_DIR` | 10 | PASS |
| Saya no Uta PT-BR | `PONSCRIPTER_TEST_DATA` | 1 | PASS |

## Correções de bugs reais encontrados pela bateria

- **Ren'Py VM**: slots RPYC2 pós-transformação (`Translate`/`EndTranslate`)
  eram ignorados → zero diálogos nos scripts retail de DDLC/Katawa. Corrigido
  em `games/doki-doki-literature-club/src/Script.cpp` e `modules/vms/renpy/src/Vm.cpp`.
- **RealLive**: `objDispRect` tratava `w,h` como `x2,y2`, desenhando o atlas
  inteiro da logo Key; frames de botão e strip do cursor também estavam errados.
  Corrigido em `games/reallive/src/RealLiveRuntime.cpp`.
- **N2System**: `CreateMovie` lia o nome literal do parâmetro `"描画優先度"`
  como `asFloat()`→0, deixando o filme atrás do fundo preto. Corrigido em
  `modules/vms/n2system/src/NsbVm.cpp`.
- **Artemis**: `resolveUiImagePath` não tratava IDs de arte renomeados entre
  edições (`background_2a` → `background_trial.png`). Corrigido em
  `modules/providers/artemis/src/ArtemisAssets.cpp`.
- **Zero**: condição de corrida no teste de input do menu (espera fixa de frames).
  Corrigido em `games/zero-no-tsukaima/tests/ZeroEngineTest.cpp`.

## Falsos positivos eliminados

Antes da auditoria, testes retail sem dados retornavam `0` e apareciam como
`Passed`. Todos passaram a retornar `77` com `[SKIP]` e `SKIP_RETURN_CODE 77`.
Resultado sem dados: `Skipped`, nunca `Passed`.

## Limitações conhecidas

- `AIR-SE.ISO`, `KANON-SE.ISO`, `PLANETARIAN.ISO`, `Clannad` completo,
  `LostM`, `ImoutoDeIkou` — cobertura em expansão (gates em trabalho).
- `NEKOPARA_VOL0_PT-BR` — XP3 protegido; avaliação em andamento.
- `LargePonPon` — testes sintéticos; gate retail próprio em avaliação.
- Windows cross-build a partir do macOS: bloqueio de plataforma (sem MSVC /
  Windows SDK; MinGW linka libs Mach-O do Homebrew no libass).

## Builds das plataformas (estado verificado nesta sessão)

| Plataforma | Resultado |
|---|---|
| macOS CMake arm64 + amd64 | PASS |
| Xcode macOS + iOS (34 schemes) | PASS |
| WASM (14 alvos, Asyncify 14/14) | PASS |
| Android (17 flavors, 16 KiB) | PASS |
| Windows cross (macOS host) | BLOQUEIO DE PLATAFORMA |
