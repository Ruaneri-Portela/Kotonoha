# PONscripter Port Status

**English (US)** | [Português (Brasil)](STATUS.pt-BR.md)

## Implementation Coverage

- **Script Discovery & Directives (`ScriptBuffer`)**: Concatenates `0.utf`, `00.utf`..`99.utf` (and `.txt` fallbacks), strips UTF-8 BOM (`EF BB BF`) prior to directive parsing (`;mode800` -> `800x600`, `;value<N>`), and indexes statements and `*labels`.
- **Archive & Asset Provider (`PonscripterAssets` + `ArchiveReader`)**: Supports SAR and NSA (v1/v2) archives with raw, LZSS, SPB, and NBZ decompression, case-insensitive virtual path resolution on macOS/Linux, and `sound/<dir>/<clip>.ogg` -> `voice/<dir>/<clip>.ogg` fallback for the 15 script path typos in `01.utf`.
- **PONscripter VM (`PonscripterVm`)**: Implements all 75 commands used across `00.utf` (`1,679` lines) and `01.utf` (`11,576` lines), plus the full standard ONScripter/PONscripter command family (`numalias`, `stralias`, `defsub`, `if`/`notif` with `&&`/`||`/`<>`, `goto`/`gosub`/`return`, `for`/`next`/`break`, `select`/`csel`, `mov`/`inc`/`dec`/`add`/`sub`/`mul`/`div`/`mod`/`rnd`/`cmp`/`atoi`/`itoa`, `bg`, `ld`, `cl`, `lsp`/`csp`/`vsp`/`msp`/`amsp`, `bar`/`barclear`, `btndef`/`btn`/`spbtn`/`btnwait`, `print`/`effect`/`windoweffect`, `setwindow3`/`setcursor`/`h_mapfont`, `mp3`/`mp3loop`/`dwave`/`dwaveloop`, `mpegplay`, `rmenu`, `systemcall`).
- **Sprite Tag & Compositor Semantics**: Supports `:a` (native PNG alpha or right-half mask for non-alpha BMPs like `system\uoncur.bmp`), `:c`, `:l`, `:r`, `:m`, and `:s[;][#RRGGBB]^text` string sprites, multi-cell animation modes `0` (loop), `1` (once), `2` (ping-pong), and `3` (manual button hover cells), and PONscripter `999 -> 0` sprite Z-order.
- **Proportional Text & Inline Tags (`TextFormatter`)**: Handles `^` proportional text, `@` inline click-wait, `\` page-end click-wait + backlog push, `~i~`/`~b~`/`~c~`/`~s~` inline tags, `~~` literal tilde escape, `#RRGGBB` inline colors, and smart ligatures (`...` -> `…`, `--` -> `–`, `---` -> `—`).
- **Save State & Extras (`SaveSerializer`)**: Persists global variables (`gloval.sav`: `%lic_accepted`, `%end1_clear..%end3_clear`, `%cgset_0..4`, `%gro`, `%vol*`, `%textspeed`, `%autospeed`, `%movplay`) and 20 save slots (`save1.dat`..`save20.dat`), unlocking the CG Gallery (`97` CGs), Music Box (`15` tracks), and Staff Notes across all 3 endings.

## Kotonoha UI Integration

- Native `rmenu` (`Salvar`, `Carregar`, `Histórico`, `Tela inicial`) opens `PonscripterMenu`, a PONscripter implementation injected through the shared `Kotonoha::InGameMenu` component. Its entries map to the native system calls (`save`, `load`, `lookback`, `reset`) and its rendering replaces the generic Kotonoha panel.
- Native `lookback` opens `PonscripterBacklog`, a PONscripter implementation injected through `Kotonoha::VNBacklog`; Save/Load remain in the PONscripter-native slot overlay and do not use `Kotonoha::VNSaveMenu`.
- The VM remains in `WaitSystemCall` until a PONscripter modal closes, with close callbacks restoring the previous wait state through `finishSystemCall()`.
- PONscripter backlog navigation supports Up/Left/PageUp and Down/Right/PageDown, mouse wheel, `B`, `Escape`, and right-click close, and displays the native-style page counter and instruction line.
- PONscripter backlog selection is clamped to the valid `[0, entries.size() - 1]` range at open, navigation, wheel movement, and render time.
- PONscripter backlog and menu entries use `Engine::GetVNUI()` and apply `vnTextColorOverride`, `vnSpeakerColor`, and `vnTextColor`; generic VN backlog entries use the same shared controls.
- Backgrounds, standing sprites, extended sprites, cursors, and text-window backdrops are cached after individual image processing and use the engine upscaler when enabled.
- `PonscripterRuntimeTests` covers both a synthetic script and the retail Saya no Uta data: right-click `rmenu`, Save/Load, History, direct `lookback`/`save`, and VM resumption after each modal closes.
