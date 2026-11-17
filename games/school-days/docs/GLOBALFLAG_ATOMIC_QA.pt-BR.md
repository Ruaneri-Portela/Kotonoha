# Durabilidade do GlobalFlag e QA sintético do AllClear

**Escopo:** persistência específica do School Days e testes sintéticos, sem
pacotes comerciais, saves reais nem executável retail. O formato `FlgH` no
disco permanece inalterado.

## Escritor atômico standalone — 17/11/2026

`SchoolDaysAtomicFile` usa apenas C++17, sem dependências do motor, SDL ou
vcpkg. O teste standalone inclui 1.000 substituições completas, falhas
injetadas em escrita/flush/publicação/sincronização do diretório, limpeza de
órfãos e 220 interrupções aleatórias `SIGKILL` por runner POSIX. No macOS,
usa `fcntl(F_FULLFSYNC)` quando disponível, com alternativa `fsync` em
`ENOTSUP`; trata chamadas interrompidas e escritas parciais. O mesmo teste
passou no Windows local.

A verificação temporária no GitHub Actions ocorreu em **17/11/2026**, no fork:

| Runner | Versão da imagem CI | Resultado |
| --- | --- | --- |
| Ubuntu 26.04 | `20261111.196.1` | C++17 e ASan/UBSan PASS |
| macOS 26 ARM64 | `20261102.0473.1` | C++17 PASS |

Execuções: [original](https://github.com/tsubaki-0x/Kotonoha/actions/runs/44013431802)
e [complementar](https://github.com/tsubaki-0x/Kotonoha/actions/runs/44014281654).
A branch CI temporária e o workflow foram excluídos. Esses resultados validam
o **helper isolado**, não a compilação completa do jogo em Linux/macOS nem
a durabilidade durante uma perda física de energia.

## Limites dos testes AllClear e DIST

`SchoolDays.AllClearSynthetic` cobre N-1/N flags de final, `EndBGView`,
promoção ao preparar o título, idempotência, rejeição de GlobalFlag corrompido
e persistência após carregar com novo objeto. A ferramenta
`tools/make_synthetic_globalflag.py` exige perfil de QA isolado com marcador
e recusa sobrescrever sem `--replace`; `tools/inspect_synthetic_globalflag.py`
é somente leitura e imprime somente as flags pertinentes.

A sequência completa de finais **não teve validação visual**. A confirmação
de paridade integral requer uma partida de verificação.
