# L4 Windows — School Days HQ / L×H: idiomas, etapa 2–3 (2026-11-07)

## Estado da L4

- **Build de trabalho exclusiva:** `D:/Dev/Kotonoha-v3/build/school-days-dev-control4/games/school-days/Release`.
- Não houve commit/push, alteração dos packs comerciais, alteração dos saves reais,
  execução do EXE comercial nem build Android.
- O overlay privado contém sete imagens universais inglesas, ORS/WMV/OGG
  originais da DLC instalada. A Etapa 1 e seu backup estão detalhados em
  `LXH_UNIVERSAL_ENGLISH_ASSETS_L4_2026-11-07.md`.

## Artefatos de idioma instalados — SHA256

Somente estes quatro novos arquivos foram copiados do scratch privado
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-en-lxh-worker-20261107/translations`
para `<Release>/translations`:

| Arquivo | SHA256 |
| --- | --- |
| `en-lxh.json` | `1DECA4298EF0DD65E1CA0C16C2513A2DDFBBF8FEBD61059C5EB27E9369E1D5B1` |
| `en-lxh-reviewed-identities.json` | `03C0F30CA601C26CADD45B85EF7E9AC1AC4FE569B8D1FA14B9ABBE69DBDF0A6B` |
| `es-lxh.json` | `589A42B642B456B14C45BFE45AE323E9D19686515EF2D87F0000EF36AF81319A` |
| `es-lxh-reviewed-identities.json` | `B3A3CE7EDBA6A9F40BB94F5AEA6056E3782EFD1F0BCDD9DE47D678C0606D9871` |

**Preservados sem alteração** os JSONs base
`es.json` (SHA256 `17F0DB1490F97E84E1A83001EE50FEEE3D82F19749B3625C2F49EAF0FBA8495A`)
e `es-reviewed-identities.json`
(SHA256 `9EB70DF4BFA7593484D4CB6A8B8010F8DF6D55EFFF7D1AD965F8F43864355598`).
Backup pré-staging e relatório pós-cópia em
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-locale-staging-20261107-20261107-005443-3826542`.

## Revisão de tradução

- **273/273 novas ocorrências L×H** dos sete ORS, conferidas contra o
  `Kotonoha_OrsParserBytes` real (NODE/ordinal/evento/ticks 24 Hz/speaker).
  São 266 falas e sete créditos originais, com 20 identidades inglesas revisadas.
- Descobertos e corrigidos dois eventos invertidos do catálogo Python na
  cena `05-SB-E01`: `Huh...` = `@ors:91:0`; a fala espanhola
  `También estás feliz...` = `@ors:90:0`.
- A versão separada `es-lxh.json` corresponde **exatamente** aos 273 valores
  efetivos do ES V4b original, incluindo as **nove correções não idênticas**
  ao ORS-fonte; 264 identidades com justificativa conservada.
- Os marcadores `^` e `\\n` foram preservados; o original mantém os nomes
  canônicos de speaker, necessários para as cores ASS.
- Provas e código de reprodução: `qa-l4-en-lxh-worker-20261107/QA_REPORT.md`
  e `qa-stage2-native-prime-20261107-20261107-004616-2349758/`.
- Não confundir revisão estática de significado com inspeção audiovisual
  definitiva no vídeo a 1×; esta continua na Etapa 5.

## Runtime e launcher — Etapa 3

- HQ inglês continua nativo, sem inventar um JSON de 30.968 entradas.
- Durante L×H ON, `translations/<locale>-lxh.json` é selecionado e consultado
  exclusivamente para o epílogo identificado no ORS nativo por
  `END_ROLL → PLAY_MOVIE LxH`; todo evento recebe seu ordinal do parser real.
- Lookup: DLC `@ors` → DLC literal NODE → HQ `@ors` → HQ literal NODE →
  HQ `nodo`. Lacuna ou identidade não revisada de língua não nativa falha
  explicitamente. Chaves de DLC órfãs/deslocadas são rejeitadas.
- `Subtitles`, opções de escolha e `BackLog` usam a mesma tradução.
- O launcher existente valida **antes do Play**, inclusive inglês nativo com
  DLC ON/AUTO. Para EN/futuros idiomas exige sidecar JSON específico da DLC;
  somente ES V4b é exceção porque já contém os 273 textos extras.
- `DlcJsonPath` diferencia `ENOENT` de erros reais de acesso no Windows,
  para não rejeitar ES V4b legado por ausência do sidecar opcional.
- Não houve redesenho do launcher nesta etapa; a modernização é vedada antes
  de fechar os 28 cenários + audiovisual da Etapa 5.

## Execuções / resultados

1. Compilação Release (MSBuild 18.9.1) de `SchoolDays.exe`,
   `SchoolDaysLauncherTest`, `SchoolDaysLxhLocaleTest`,
   `SchoolDaysTranslationContextTest` e
   `SchoolDaysTranslationCoverageTest`: **PASS**.
2. CTest serial, com `PATH` contendo `lib/Release` e
   `games/school-days/Release` e SDL áudio/vídeo dummy: **8/8 PASS**:
   `SchoolDaysPortTest`, `SchoolDays.JsonTranslation`,
   `SchoolDays.TranslationCoverage`, `SchoolDays.TranslationContext`,
   `SchoolDays.LxhLocale`, `SchoolDaysLxhPolicyTest`,
   `SchoolDaysLxhFadeTest`, `SchoolDaysLauncherTest`
   (total ~28 s na execução final).
3. O executável recompilado teve **4/4 inicializações headless PASS**
   (English/Spanish × L×H ON/OFF, SDL dummy, `--frames 6`,
   `--documents` isolado). Logs em
   `D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-layered-locale-smoke-20261107-20261107-010049-5753413`.
4. A auditoria nativa de espanhol, antes do último rebuild, confirmou
   OFF **30.968/30.968**, ON **31.241/31.241**, zero lacunas,
   identidades não revisadas ou 485 opções ultrapassando o espaço.
5. Nova conferência SHA256 **depois dos builds e testes**:
   `Packs/Script.GPK` + sete saves QA do baseline **8/8 idênticos**;
   manifesto do overlay **28/28 arquivos idênticos ao hash esperado**.
6. `git diff --check -- games/school-days`: **PASS**,
   mesmo com outras alterações previamente presentes no worktree.

## Limites e próximas etapas

- Nenhum teste de seis frames comprova final completo, áudio 1× ou sync
  WMV/OGG. **28 cenários completos (2×2×7), replay/SaveFeel e inspeção visual
  física ainda pendentes**; não promover estes quatro smokes a esse gate.
- Saves comerciais antigos não gravam tick nem proveniência DLC no SLog.
  Não se pode distinguir apenas pelos seus bytes se foram feitos dentro do
  epílogo L×H. A Etapa 4 estuda sidecar **apenas para novos saves L4** sem
  modificar o formato DAT, com validação antes de qualquer mutação de estado.
- Etapas 6–7/launcher moderno e Android ainda não executadas.
