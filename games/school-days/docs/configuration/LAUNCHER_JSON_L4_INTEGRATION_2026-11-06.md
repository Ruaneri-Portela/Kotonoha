# School Days HQ — JSON/locales e L×H no launcher L4

> **ATUALIZAÇÃO V4b:** o espanhol foi corrigido e liberado para QA Windows
> com **30.968/30.968** diálogos/opções HQ e **31.241/31.241** com L×H,
> zero fallback inglês e zero identidades sem revisão no parser nativo L4.
> O launcher aceita o JSON certificado + manifesto de identidades;
> traduções incompletas continuam bloqueadas. Ver
> `../localization/ES_JSON_STRICT_AUDIT_L4_2026-11-06.md`. Os números V1/V2/V3 abaixo são
> **históricos** e não representam o estado atual da L4.

**Escopo:** somente `D:/Dev/Kotonoha-v3/build/school-days-dev-control4`
(Windows). **Originais comerciais NÃO modificados.** Mídias e traduções
comerciais/comunitárias permanecem privadas, fora do repositório.

## Contrato

- A base é `Packs/Script.GPK` comercial **inglês**, sem recompilação.
- `SchoolDays::Launcher` existente foi estendido no painel do próprio jogo
  para escolher `English (original)` ou `Spanish (JSON)` e DLC L×H `Auto/On/Off`.
  A aba genérica *Translation* continua aceitando JSON personalizado.
- O JSON do Ruaneri tem `NODE` em seção decimal não negativa e fallback `nodo`.
  A chave é o texto **literal do ORS efetivo** e o valor é o texto exibido.
- `Runtime::translationRoute()` usa o nodeIndex KTRF normal ou o índice
  temporário Replay/PlayData. `translationSources()` percorre os ORS do KTRF
  montados no `Assets` (incluindo os sete L×H quando ON) e exporta PRINT_TEXT
  e SetSELECT. O BackLog exibido é localizado por ocorrência; os ORS e
  a persistência/saves originais não são alterados.
- Legendas localizam **somente texto**, nunca o speaker/character: o nome
  original continua determinando a cor ASS (`Makoto`, `Sekai`, `Kotonoha`,
  etc.). Também em edição JSON ao vivo o speaker permanece canônico.
- Escolhas usam JSON por NODE e refazem o atlas quando texto ou slots mudam,
  sem modificar as opções/condições do KTRF/ORS.

## Espanhol privado extraído dos packs do usuário

Fonte EN: `Packs/Script.GPK` comercial v1.02, SHA256
`E82DC824449381B9A132AA092740C4169EB3D77A820D969046B74CA1D9DF4ADF`.
Fonte ES: patch espanhol `Script.gpk`, SHA256
`139B3C77EA0E4C3AC7F1B7B0871912E55C8480E01D2E5D37CD95A203B4038DD4`.

Extrator/prova **somente scratch privado**:
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-json-es-20261106`.

- Foram comparadas **1.855/1.855 cenas KTRF**. Dois ORS sem NODE no
  `Script.GPK` ficaram fora dos JSONs (não inventar índice).
- **30.968** ocorrências EN de fala/escolha; **30.015** candidatos pareados;
  **29.837** posições emitidas com segurança (cobertura **96,3478%**).
- Colisões de mesma chave EN com traduções ES diferentes dentro do mesmo NODE
  (**75 chaves/178 ocorrências**) e eventos sem par estrito foram omitidos,
  não escolhidos por heurística nem por proximidade.
- Codificação ES variável: 1.808 ORS UTF-8 BOM, 46 UTF-8, 1 CP1252 estrito.
  O parser de extração respeita o formato ORS e não troca acentos por `?`.
- JSON ES do HQ + L×H seguro é
  `es-ES-school-days-hq-merged-safe.json` (privado). Cópia para testes da L4,
  em pasta ignorada por Git: `<Release>/translations/es.json`.
- O L×H tem **273 novas falas**, 271 chaves ES únicas por NODE, sete cenas.
  O próprio ORS montado traz essas falas em espanhol; o JSON ES usa identidade
  nessas chaves. **Isso NÃO é tradução inglesa.**
- O arquivo `lxh-273-en-translation-queue.jsonl` no scratch lista os textos ES,
  speaker canônico, NODE e intervalo para tradução/revisão inglesa. O
  `en-EN-lxh-runtime-dictionary-template-empty.json` não contém traduções e
  **NÃO** representa um idioma inglês L×H completo.

## Status real de idiomas

| Idioma | HQ original | Novas falas L×H |
|---|---|---|
| Inglês | Nativo, sem JSON | Tradução EN das 273 falas **pendente** |
| Espanhol | JSON V4b: 30.968/30.968 falas/escolhas com cobertura nativa | 273 falas espanholas no overlay validadas |

O launcher valida todo o idioma selecionado no KTRF/ORS efetivo, incluindo
os textos originais mantidos com motivos de revisão. Não atribuir ao espanhol
100% da interface/imagens/vídeos sem uma auditoria separada, nem rotular as
273 falas L×H originalmente espanholas como traduzidas para inglês.
Outros idiomas, inclusive PT-BR **privado**, ficam fora da distribuição e
dependem de traduções e verificações próprias antes da ativação.

## Próximas verificações

- **Validado na L4 Windows Release:** `SchoolDays.JsonTranslation`, launcher
  sintético com mudança English↔Spanish e L×H Off↔On, persistência de JSON
  (inclusive arquivo externo restaurado pelo sidecar game-only), fades,
  carregamento de saves e MenuBar: sete casos focados passaram em CTest.
- **Validado em execução real da L4 com SDL dummy:** espanhol JSON privado
  e DLC On/Off iniciaram com sucesso em `--documents` isolado; um restart
  sem `--translation`/`--asset-overlay` recuperou `es.json` e o overlay L×H
  dos arquivos de preferência salvos. O launcher ImGui abriu responsivo na
  própria L4 com `--launcher` em perfil isolado e foi fechado normalmente.
- Ainda faltam inspeção manual da seleção English↔Spanish no launcher,
  renderização de falas traduzidas com cor por personagem em todas as cenas,
  compatibilidade extensiva de BackLog e textos longos após tradução.
- Testes de legenda/cor ao trocar fala, BackLog, escolhas com texto longo e
  tradução ao vivo, especialmente SetSELECT e cenas L×H.
- Tradução e revisão real das 273 falas em EN, revisão dos eventos espanhóis
  omitidos/ambíguos e cobertura de imagens com texto; só depois declarar
  localização completa.

Sem commits, push, Android, novos GPKs ou publicação de arquivos privados.
