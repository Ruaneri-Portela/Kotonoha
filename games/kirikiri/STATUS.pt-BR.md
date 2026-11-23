# Kirikiri2 / KirikiriZ — resumo de suporte

[English (US)](STATUS.md) | **Português (Brasil)**

**Escopo implementado/testado:** Arquivos XP3 v1/v2 padrão (índices encadeados/zlib, arquivos segmentados, precedência de patches), imagens TLG5/TLG6/PIMG, parser binário clean-room PSB v2 + MDF (`PsbFile`), executor de cenários KAGEnv `.ks.scn` (`ScnRunner` com localização PT-BR `VNX+/`, placas de nome `cn_name_*.png`, vozes multicanal, animações keyframe E-mote, persistência de snapshots de savepoint e progressão pelos capítulos `00_01.ks`..`00_15.ks`), telas de interface nativa `VNX+/` (menu inicial `title_cn@*`, Save/Load `saveload_cn@*`, Config `config_cn@*`, Backlog `backlog_cn@*`, Extra/seleção de capítulos `extra_cn@*`, caixa de mensagem/barra de ferramentas `window@help__*` e `qlpopup@*`), scripts KAG3 em texto e avaliação limitada de bootstrap TJS2 estão implementados.

**Limitações:** Pacotes `.xp3` comerciais criptografados por filtros nativos de terceiros (`PackinOne.dll`) são indexados sem contorno de DRM; overlays não criptografados (`VNX+/`) e assets Kirikiri padrão são renderizados diretamente.

## NEKOPARA_VOL0_PT-BR — evidência retail (2026-11-23)

Fonte somente leitura: `/Volumes/Nero 2TB/KotonohaAssets/NEKOPARA_VOL0_PT-BR`.
Nenhum asset comercial foi copiado para o repositório. `PORT_STATUS.md` não foi
modificado.

### Hashes e tamanhos (evidência)

| Arquivo | Bytes | SHA-256 |
|---|---:|---|
| `data.xp3` | 191864049 | `6384cd1a312d64574339fe9066120bcd7cd997b9a4b0378ca4b1ddffe9d2cfbf` |
| `patch.xp3` | 152097 | `691bb011334dd13c9c1b01522c61fad8100bf7e847dad70d8387c7f49beaf8f1` |
| `data.xp3.sig` | 212 | `840670c2ec7365c28214ce5b1dfff616dbe33c9456a1fb5fb4e85abaee8ea05b` |
| `nekopara_vol0.exe` | 4338688 | `8f2d864eaedc9fcd31a31fcecd22fbed3d9299c0ea68f86781e7523ae2a0df23` |
| `plugin/PackinOne.dll` | 332288 | `8a2d7af0e53603020ab791e9ed676f2cd7fe5b7d0250acce26ff393db4576875` |

`data.xp3.sig` começa com `-- SIGNATURE - SHA256/PSS/RSA --` seguido de duas
linhas em base64. `patch.xp3` não possui arquivo `.sig` nesta instalação.

### Estrutura e proteção do XP3

`KirikiriXp3` (o provider existente `Kotonoha::Providers::KirikiriAssets`)
abre os dois arquivos **sem lançar exceção**: o índice é legível. Isso não
significa que os payloads sejam utilizáveis.

- `data.xp3`: o offset de índice do cabeçalho `0x17` é um redirecionamento
  encadeado de 0 bytes (`flags=0x80`); o índice real está em `0xb6e83d2`
  (`flags=0x01`, 71950 bytes armazenados -> 314966 decodificados), gerando
  1183 entradas / 1183 segmentos (948 crus, 235 zlib).
- `patch.xp3`: índice em `0x250c5` (`flags=0x01`, 331 armazenados -> 960
  decodificados), gerando 4 entradas / 4 segmentos crus.
- Flags de `info`: `data.xp3` tem 1182/1183 entradas com o bit 31 ligado
  (`0x80000000`, payload criptografado); apenas o `.txt` de aviso em texto
  claro tem flags `0`. `patch.xp3` tem 4/4 entradas com `0x80000000`.
  Total: **1186/1187 entradas protegidas**.
- Verificação de payload: a única entrada legível começa com
  `57 61 72 6e 69 6e 67 3a` (`"Warning:"`); as entradas com nome hash começam
  com bytes de alta entropia e sem magic PNG/TLG/OGG/PSB.
- `startup.tjs` (de `data.xp3`) tem 316 bytes de bytecode TJS2 compilado
  (`TJS2100\0`), com o diagnóstico embutido
  `startup failed: corrupted execution file`. Não há bootstrap em código-fonte
  executável nem entrada KAG dentro do XP3 (`kag=''`, `error=''`).
- Os 16 cenários visíveis vêm todos de arquivos soltos `VNX+/`; **0** são
  exclusivos do XP3. Portanto `KirikiriXp3` não consegue fornecer o título ou
  os scripts originais do jogo a partir destes arquivos.

Diagnóstico retail exato:

```
[SKIP] protected XP3: 1186/1187 entries carry info flag 0x80000000 (encrypted payload); startup error: startup failed: corrupted execution file
```

### Alcançável vs bloqueado

- **Alcançável (teste real, PASS):** montar/indexar os dois XP3; carregar o
  overlay PT-BR solto `VNX+/` (185 arquivos: 166 PNG, 18 `.scn`, 1 `.ks`),
  ler `VNX+/sysmenu.ks` (560 bytes UTF-8), parsear `VNX+/scn/scenelist.scn`
  (15 entradas de capítulo) e executar `VNX+/scn/00_01.ks.scn` no `ScnRunner`
  com slot de idioma 2 (PT-BR; primeiro diálogo decodificado com 29 bytes).
- **Bloqueado:** paridade 1:1 da arte, áudio, filmes, PSBs E-mote e scripts
  originais. Os payloads XP3 são criptografados por `PackinOne.dll`; o parser
  clean-room deliberadamente não contorna DRM. Sem assets descriptografados ou
  o filtro nativo do Windows, o Kotonoha só apresenta a interface e os scripts
  da tradução `VNX+/`, não o jogo retail original.

### Testes com gate retail e verificação

`KIRIKIRI_TEST_DATA` agora alimenta dois testes com `SKIP_RETURN_CODE 77` e
label `real` (nenhum PASS falso):

- `Kirikiri.RetailXp3Startup` — monta `data.xp3` + `patch.xp3`, inspeciona o
  `startup.tjs` do próprio XP3 e tenta o startup KAG. Reporta SKIP com o
  diagnóstico exato de proteção acima.
- `Kirikiri.RetailVnxScripts` — carrega os scripts PT-BR soltos e valida
  diálogo real do capítulo `00_01.ks.scn`.

```
cmake -S . -B build -DKIRIKIRI_TEST_DATA="/Volumes/Nero 2TB/KotonohaAssets/NEKOPARA_VOL0_PT-BR"
ctest --test-dir build -R '^Kirikiri\.' -V
# com gate: Kirikiri.RetailXp3Startup = Skipped (77),
#           Kirikiri.RetailVnxScripts = Passed
cmake -S . -B build -DKIRIKIRI_TEST_DATA=""
ctest --test-dir build -R '^Kirikiri\.' -V
# sem gate: ambos = Skipped (77)
```

`ctest --test-dir build -j8` completo: 214 testes, 97% aprovados; as 7 falhas
são itens pré-existentes e não relacionados (3 testes sintéticos `Kotonoha.*` e
4 testes `RealLive.Retail.*` "Not Run" cujos binários não existem neste build).
`python3 tools/ValidateBuildMetadata.py`: 16 jogos CMake; 34 schemes Xcode
existentes; Xcode pending: none. `git diff --check`: limpo.

[README do port](README.pt-BR.md) · [Todos os ports](../README.pt-BR.md)
