# GitHub Actions — auditoria pós-sincronização (23/11/2026)

Repositório: `tsubaki-0x/Kotonoha`, `dev` em `9ef8daf395458038270567b9160a194483430848`.
O lote de push iniciou **21 workflows**; 15 passaram e 6 foram sinalizados como falha.
Esta auditoria usa os logs dos **jobs**, não apenas o status agregado do workflow.

| Workflow / run | Evidência | Encaminhamento |
|---|---|---|
| [LucaSystem](https://github.com/tsubaki-0x/Kotonoha/actions/runs/45161321785) | C++ não encontrou `modules/providers/pak/src/CzImage.cpp` nem `LucaSystem/CzImage.hpp`. O codec está em `modules/formats/image/cz/`. | Corrigidos include/source e gatilho `paths` no YAML. |
| [Port contracts](https://github.com/tsubaki-0x/Kotonoha/actions/runs/45161321625) | `tools/check_web_shell.js` tentou abrir `lib/src/Filesystem/Sources/FetchAssetProvider.cpp` (ENOENT). | Corrigido para `lib/src/filesystem/sources/`. O teste complementar `test_em_asm_fetch.js` tinha a mesma referência antiga e também foi corrigido. |
| [Shared filesystem](https://github.com/tsubaki-0x/Kotonoha/actions/runs/45161321831) | Apenas o job `macos-15` foi cancelado, sem runner e com zero passos; Linux e Windows passaram. | Gatilhos de caminhos renomeados corrigidos, mas o cancelamento macOS requer investigação do GitHub Actions. |
| [Apple encoding](https://github.com/tsubaki-0x/Kotonoha/actions/runs/45161321718) | Job `macos-15` cancelado sem runner e sem passos. | Não há erro de compilação comprovado; verificar fila/quotas/runner e tentar nova execução. |
| [Ponscripter](https://github.com/tsubaki-0x/Kotonoha/actions/runs/45161321549) | Apenas `macos-15` cancelado sem runner; jobs Linux de C++, sanitizers e CMake passaram. | Não desabilitar teste Apple para ocultar o cancelamento. |
| [Repository hygiene](https://github.com/tsubaki-0x/Kotonoha/actions/runs/45161321349) | Hygiene Linux passou; job Apple NSB foi cancelado antes do primeiro passo. | Verificar disponibilidade e quotas macOS. |

Os quatro cancelamentos macOS ocorreram cerca de 15 minutos após a criação,
sem nome de runner atribuído e sem etapas executadas. A causa administrativa
(capacidade, quota, faturamento ou serviço) **não foi determinada**; não tratar
esses estados como erro do código nem declarar a plataforma validada.

## Verificação local das correções

- Paridade sintética CZ3/CZ4 compilada com `clang++ -std=c++17 -Wall -Wextra -Werror -pedantic`: **PASS**.
- `node tools/check_web_shell.js`: **PASS** (4 scripts HTML).
- `node tools/test_em_asm_fetch.js`: **PASS** com preprocessador C++ local.
- `node tools/test_pwa_worker.js`, `test_preload_cache.js` e `test_web_locales.js`: **PASS** (mensagens esperadas de rede indisponível no teste PWA).
- `python -m unittest discover -s tools -p test_generate_manifest.py -v`: **9 testes, 8 PASS e 1 skip** por privilégio de symlink ausente no Windows.
- Verificadores de higiene, metadados CMake, 16 contratos de ports,
  matriz de releases, identidade de targets e SDL/CPack: **PASS**. A higiene
  mantém três lembretes preexistentes de proveniência de assets.
- `cmake -P tools/test_nested_python_bundle.cmake`: não validado no Windows por falta de privilégio de criar symlink (`WinError 1314`); executar no runner Linux da CI.

O validador Node usa `python3`/`c++` em Linux e `python`/`clang++` em Windows,
permitindo substituir o executável por `KOTONOHA_PYTHON`/`KOTONOHA_CXX` em QA.
O fork mantém `dev` e `hybrid`, sem sobrescrever os packs retail, saves ou
distribuições de School Days HQ.

**Próximo gate:** publicar correções sem force-push, verificar a nova execução
da CI e investigar os jobs macOS cancelados separadamente. O histórico de runs
antigos não desaparece quando um commit novo corrige o código.
