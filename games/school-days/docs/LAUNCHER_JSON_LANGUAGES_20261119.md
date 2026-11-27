# School Days HQ launcher — installed JSON languages (2026-11-19)

## Intent and interface

- A **GPK** section and a **JSON** section are shown in the single existing
  Language dropdown. Each item displays only the language name, without
  `.GPK`, `.json`, `Script.GPK`, `legacy`, or other format text in its label.
- Installed JSON presets are: **English** (`en.json`), **Português (Brasil)**
  (`pt-BR.json`), **Español** (`es.json`), **Chinese - Simplified**
  (`zh-CN.json`), and **Russian** (`ru.json`). A missing base file disables its
  item rather than selecting a different language.
- The **Custom JSON** dropdown item and generic Advanced translation picker
  are removed. Internal `CustomJson` support is retained exclusively for
  explicit CLI `--translation` and legacy saved profiles; their original
  paths are not silently discarded.
- Existing native GPK locale selection is unchanged. The base JSON and its
  corresponding `*-lxh.json` are selected together by exact filename, without
  selecting a different locale or replacing retail GPK assets.

## Source-identical text policy

The five **installed canonical** JSONs can intentionally provide text equal to
the English source. `IsInstalledLocaleJson` verifies the selected on-disk file
matches one of the exact installed locale filenames, not an arbitrary external
file. `CheckLayered` reports these as `supplied_identity` (distinct from a
`reviewed_identity`). It **still fails closed** on missing keys, blank values,
invalid LxH context keys, and nonreviewed source-identical text in external
JSONs. This supports the user's supplied complete dictionaries without
claiming all identical sentences were linguistically reviewed.

English `en.json` is an explicit fully keyed JSON profile; native English
without an HQ JSON remains a separate original-script mode. Neither mode
alters the 29 original retail GPKs or the original LxH media pack.

## Checks performed

- DEV Release targets compiled; focused CTest **8/8 passed** (includes
  launcher, layout, LxH, native GPK profile, input/MenuBar and port smoke).
- DIST Release targets compiled; focused CTest **5/5 passed** (launcher,
  layout, LxH, native GPK profile and port smoke).
- Actual DEV binary: isolated one-frame **5/5 successful JSON starts** with
  `en`, `es`, `pt-BR`, `ru`, and `zh-CN`, paired LxH JSONs, and player-owned
  retail Packs mounted **externally**, with SDL software/dummy test renderer.
- DIST binary: isolated PT-BR JSON one-frame smoke **exit 0**; default
  native PT-BR GPK from its translated-script profile **exit 0**.
- Separate private DEV/DIST QA profiles contain 91 hash-verified files each,
  no retail Packs and no existing saves. Their launch scripts default to
  OpenGL. Visual gameplay/launcher review on a real GPU is **not certified**.
- Windows private parallel release prepared without overwriting the user's
  existing distribution or transferring `Save`, `Config.DAT`, `GlobalFlag.DAT`
  or `settings.json`.

## Approved Brazilian sources

All four were copied **byte-for-byte** from
`D:\Dev\Kotonoha-v3\dist\SchoolDaysHQ-Windows-Clean-Distribution-v3\translations`:

| Filename | SHA-256 |
| --- | --- |
| `scripts/Script - Brazilian.GPK` | `E0B8C9D366736D2B2F5CC6DF43332BFAE4C24330D0E466C7FAC1EA41FD701D84` |
| `jsons/pt-BR.json` | `B4C02BC83F377DF48BB9EF142BE076E434B313153FFC621FFDC3962B21325833` |
| `scripts/pt-BR-lxh.GPK` | `17D1879E5E286777B4D2951E48B9CB836DC502EFBABE6FD1B545162DC0065949` |
| `jsons/pt-BR-lxh.json` | `AF6E74B5710A78E1A31B12C05E91E0469D4C57F71EB0A9D85C0FED48ADC50C5C` |

## Local delivery (not pushed)

- Parallel private build:
  `D:\Dev\Kotonoha-v3\dist\SchoolDaysHQ-Windows-JSON-Languages-20261119\SchoolDays.exe`
- Isolated QA launchers:
  `D:\Dev\_worktrees\school-days-menubar-cursor-20261119\build\json-dist-qa\homologation-json-languages-20261119\INICIAR-QA-JSON-DEV.cmd`
  and `INICIAR-QA-JSON-DIST.cmd` in the same folder.
- Original distribution is unchanged, including player saves/preferences.
  No commits or pushes to `origin`/`upstream` were required for the build.

Private QA and successful smoke tests are **not** a publication license for
retail assets or translated scripts.
