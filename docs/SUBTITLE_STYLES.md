# School Days HQ subtitle styles

The subtitles use canonical `PrintText.character` names from the scripts and select an ASS style using an **exact, case-sensitive** comparison: no trimming, accent normalization or prefix/suffix removal. Translating dialogue does not translate the speaker identifier. Any unmatched name (including empty strings) uses `Default`.

The same ASS file applies to **all languages** (EN, PT-BR, ES, RU, ZH) as long as a script retains the literal speaker identifier. Translated identifiers require their own exact style row.

## Palette by slot

Individual `game-assets/styles.ass` lines use the **exact speaker identifier**, not the slot label; speakers in the same slot share colors. ASS uses `&HAABBGGRR`; outline RGB uses `#RRGGBB`.

| Slot | Exact names | FILL ASS | OUT ASS | Outline RGB |
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

Only `PrimaryColour=&H00EEEEF9` and `OutlineColour=&H003F43BE` changed in the existing `Setsuna` style; all other fields were preserved. `Makoto` retains its original colors.

### Light-background contrast

The upstream considered darker ExtraMan/Fairy/System outlines, but the fork retains the original user-approved colors above. The contrast proposed upstream has not been substituted silently; any further changes require a separate visual QA.

## ALIASES ADDED - REVIEW

| Exact name | Slot | Rationale |
|---|---|---|
| `Credits` | System | Locale variant of `Creditos` at the same LxH event |
| `Reporter` | Role | Locale variant of `Periodista` at the same LxH event |
| `Child` | ExtraMan | Locale variant of `Niño` at the same three LxH events |
| `Boy B` | ExtraMan | Generic supporting speaker present in T1; same-type rule |

Some speaker-to-character and role assignments are **inferred**; cross-locale aliases should be reviewed. When uncertain, do not create an automatic alias.

The fork retains the previously authorized Ameria and Chris aliases in Fairy; unlike the upstream proposal, their exact style rows are preserved pending explicit visual reassessment.

## Adding a character

1. Identify the **literal** `PrintText.character` string in the script and verify that label in a source script scene; do not use a beautified/translated display name or dialogue text.
2. Append `Style: <name>,...` with **23 fields** matching the `[V4+ Styles]` `Format:` order, preserving UTF-8 without BOM and CRLF. Commas are invalid in ASS names.
3. Apply only specifically approved `&HAABBGGRR` colors. Do not normalize or rename existing styles, and reject duplicate names.
4. Run `python -B games/school-days/tools/test_styles_ass.py` and any upstream subtitle renderer tests, if configured.

## Limitations and future syncs

- **The new colors are design choices, not official colors.** Setsuna's correction was visually motivated by the character's red eyes; the other colors are not taken from official material.
- The previously implemented fork palette was approved for retention. The alternative darkened upstream outlines have not been visually verified against this fork and are not claimed as applied.
- This proposal started from the current upstream file. When upstream changes the file in a future sync, **retain upstream's new lines and reapply this feature's speaker lines and Setsuna adjustment**, resolving conflicts without replacing the entire file.
