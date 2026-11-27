# Estilos das legendas do School Days HQ

A legenda usa nomes canônicos (`PrintText.character`) dos scripts e seleciona o estilo ASS por comparação **exata** e **sensível a maiúsculas/minúsculas**: sem `trim`, sem normalização de acentos, sem remover prefixos/sufixos. A tradução do texto das falas não altera o nome do falante. Nome não encontrado (inclusive vazio) usa `Default`.

O mesmo arquivo ASS atende **qualquer idioma** (EN, PT-BR, ES, RU, ZH) desde que o script preserve o identificador literal de falante. Traduções que alterem esse identificador precisam de uma linha de estilo própria.

## Paleta por slot

As linhas individuais de `game-assets/styles.ass` usam **o nome exato do falante**, não o identificador do slot; nomes no mesmo slot têm as mesmas cores. ASS é `&HAABBGGRR` e RGB do contorno é `#RRGGBB`.

| Slot | Nomes exatos | FILL ASS | OUT ASS | Contorno RGB |
|---|---|---|---|---|
| SekaiMother | `Sekai's Mother`, `Youko`, `Mãe da Sekai` | `&H00F7F1F3` | `&H00AB6077` | `#7760AB` |
| KotonohaMother | `Kotonoha's Mother` | `&H00F0ECF4` | `&H005D2C8A` | `#8A2C5D` |
| MakotoMother | `Makoto's Mother` | `&H00E9F1F1` | `&H000A6065` | `#65600A` |
| KotonohaFather | `Kotonoha's Father` | `&H00EBEDF4` | `&H001E3886` | `#86381E` |
| Kasannoin | `Kasannoin` | `&H00EEECF5` | `&H00402B90` | `#902B40` |
| Teacher | `Teacher` | `&H00EEF1F5` | `&H003D5E8C` | `#8C5E3D` |
| Roka | `Roka` | `&H00F1F3EB` | `&H00627B1F` | `#1F7B62` |
| Ruka | `Ruka` | `&H00F5F0F6` | `&H0093539B` | `#9B5393` |
| Natsuki | `Natsuki` | `&H00F8F2EA` | `&H00AE7216` | `#1672AE` |
| Mugi | `Mugi` | `&H00E9F0ED` | `&H000C5936` | `#36590C` |
| Ashikaga | `Ashikaga` | `&H00E9EFF2` | `&H000D4B69` | `#694B0D` |
| Kazuha | `Kazuha` | `&H00F6F3E8` | `&H00987605` | `#057698` |
| Futaba | `Futaba` | `&H00EDF1E8` | `&H00336801` | `#016833` |
| ExtraGirl | `Girl A`, `Girl B`, `Girl C`, `Girl D`, `Woman A`, `Woman B`, `Little Girl` | `&H00EEF0EE` | `&H00455540` | `#405545` |
| ExtraMan | `Man A`, `Man B`, `Man C`, `Boy A`, `Niño` | `&H00F6F4F4` | `&H00998186` | `#868199` |
| Fairy | `Fairy A`, `Fairy B`, `Ameria`, `Chris` | `&H00F4F4F2` | `&H0083896E` | `#6E8983` |
| System | `Cellphone`, `Screen`, `School PA`, `Creditos` | `&H00F3F3F6` | `&H00757D9C` | `#9C7D75` |
| Role | `Class Rep`, `Committee Rep`, `Periodista` | `&H00F0EFEF` | `&H005B4E51` | `#514E5B` |
| Pair | `Sekai&Makoto`, `Makoto&Sekai`, `Makoto Makoto・Sekai Sekai` | `&H00F2F1E9` | `&H00696406` | `#066469` |

## Setsuna

Somente `PrimaryColour=&H00EEEEF9` e `OutlineColour=&H003F43BE` foram modificados no estilo `Setsuna`; os outros campos foram preservados. `Makoto` conserva suas cores anteriores.

### Contraste dos grupos mais claros

O upstream sugeriu contornos mais escuros para ExtraMan, Fairy e System, mas o fork preserva as cores originais aprovadas pelo usu?rio na tabela acima. A variante de contraste do upstream n?o foi aplicada silenciosamente; mudan?as futuras exigem QA visual espec?fico.

## ALIASES ADICIONADOS - REVISAR

| Nome exato | Slot | Motivo |
|---|---|---|
| `Credits` | System | Correspondência de idioma com `Creditos` no mesmo evento L×H |
| `Reporter` | Role | Correspondência de idioma com `Periodista` no mesmo evento L×H |
| `Child` | ExtraMan | Correspondência de idioma com `Niño` nos mesmos três eventos L×H |
| `Boy B` | ExtraMan | Figurante genérico já presente na T1; regra de mesmo tipo |

A atribuição de alguns nomes a personagens ou funções é **inferida**, e uma correspondência entre rótulos em idiomas diferentes deve ser revista. Se houver dúvida sobre uma nova variante, não crie um alias automaticamente.

O fork preserva os aliases Ameria e Chris previamente autorizados dentro de Fairy; diferentemente da proposta upstream, seus estilos exatos permanecem at? revis?o visual expressa.

## Como adicionar um personagem

1. Levante a string **literal** `PrintText.character` do script e confirme-a em uma cena do script; não copie o nome traduzido bonito nem texto de diálogo.
2. Adicione `Style: <nome>,...` com **23 campos** conforme `Format:` na seção `[V4+ Styles]`; mantenha UTF-8 sem BOM e CRLF. Nomes com vírgula não são válidos.
3. Use cores `&HAABBGGRR` explicitamente aprovadas. Não normalize ou renomeie outros estilos. Verifique duplicatas.
4. Execute `python -B games/school-days/tools/test_styles_ass.py` e outros testes de renderização de legenda disponíveis no upstream, se configurados.

## Limitações e sincronização

- **As cores novas são escolhas de design, não cores oficiais.** A correção de Setsuna foi motivada pela referência visual dos olhos vermelhos da personagem; as demais cores não são extraídas de material oficial.
- O usuário aprovou visualmente as cores anteriores em uma build local Windows, inclusive na cena de Roka/Ruka/Natsuki/Mugi. Não foram anexadas capturas ao PR; os três contornos agora escurecidos não foram reavaliados visualmente.
- A proposta foi criada sobre o arquivo atual do upstream. Se o upstream modificar o arquivo em um futuro sync, **preserve as linhas novas dele e reaplique nossas adições e a correção de Setsuna**, resolvendo conflitos sem substituir o arquivo inteiro.
