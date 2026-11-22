# N2System & PONscripter Port Status

**English (US)** | [Português (Brasil)](STATUS.pt-BR.md)

## Implementation Coverage

### 1. Nitroplus N2System (`.nss` / `.npa`)
- **NsScript VM & Indexing**: Chapters, labels, subroutines (`Call`, `CallLocal`, `Return`), flow control (`Goto`, `Jump`, `Chapter`).
- **Variable Store & Expressions**: Integer (`$var`), string (`@str`), boolean (`#flag`) variables, expression evaluator with arithmetic, comparison, and logical operators.
- **Scene Graph**: Entity tree (Container, Sprite, Text, Choice) with transforms (`x`, `y`, `z`, `scale`, `angle`, `opacity`, `tint`) and animations (`fade`, `move`, `zoom`).
- **Audio & Dialogue Systems**: BGM, Voice, and SE channels with fading, looping, and volume control; typewriter text animation, speaker tracking, and backlog buffer.
- **Save State**: Binary (`N2SV`) and JSON save state serialization.

### 2. PONscripter / `Saya no Uta PT-BR` (`00.utf` / `01.utf` / `arc.nsa`)
- **Script Discovery & Directives (`Ponscripter::ScriptBuffer`)**: Concatenates `0.utf`, `00.utf`..`99.utf` (and `.txt` fallbacks), strips UTF-8 BOM (`EF BB BF`) prior to directive parsing (`;mode800` -> `800x600`, `;value<N>`), and indexes statements and `*labels`.
- **Archive & Asset Provider (`Ponscripter::PonscripterAssets` + `ArchiveReader`)**: Supports SAR and NSA (v1/v2) archives with raw, LZSS, SPB, and NBZ decompression, case-insensitive virtual path resolution on macOS/Linux, and `sound/<dir>/<clip>.ogg` -> `voice/<dir>/<clip>.ogg` fallback for the 15 script path typos in `01.utf`.
- **PONscripter VM (`Ponscripter::PonscripterVm`)**: Implements all 75 commands used across `00.utf` (`1,679` lines) and `01.utf` (`11,576` lines), plus the full standard ONScripter/PONscripter command family (`numalias`, `stralias`, `defsub`, `if`/`notif` with `&&`/`||`/`<>`, `goto`/`gosub`/`return`, `for`/`next`/`break`, `select`/`csel`, `mov`/`inc`/`dec`/`add`/`sub`/`mul`/`div`/`mod`/`rnd`/`cmp`/`atoi`/`itoa`, `bg`, `ld`, `cl`, `lsp`/`csp`/`vsp`/`msp`/`amsp`, `bar`/`barclear`, `btndef`/`btn`/`spbtn`/`btnwait`, `print`/`effect`/`windoweffect`, `setwindow3`/`setcursor`/`h_mapfont`, `mp3`/`mp3loop`/`dwave`/`dwaveloop`, `mpegplay`, `rmenu`, `systemcall`).
- **Sprite Tag & Compositor Semantics**: Supports `:a` (native PNG alpha or right-half mask for non-alpha BMPs like `system\uoncur.bmp`), `:c`, `:l`, `:r`, `:m`, and `:s[;][#RRGGBB]^text` string sprites, multi-cell animation modes `0` (loop), `1` (once), `2` (ping-pong), and `3` (manual button hover cells), and PONscripter `999 -> 0` sprite Z-order.
- **Proportional Text & Inline Tags (`Ponscripter::TextFormatter`)**: Handles `^` proportional text, `@` inline click-wait, `\` page-end click-wait + backlog push, `~i~`/`~b~`/`~c~`/`~s~` inline tags, `~~` literal tilde escape, `#RRGGBB` inline colors, and smart ligatures (`...` -> `…`, `--` -> `–`, `---` -> `—`).
- **Save State & Extras (`Ponscripter::SaveSerializer`)**: Persists global variables (`gloval.sav`: `%lic_accepted`, `%end1_clear..%end3_clear`, `%cgset_0..4`, `%gro`, `%vol*`, `%textspeed`, `%autospeed`, `%movplay`) and 20 save slots (`save1.dat`..`save20.dat`), unlocking the CG Gallery (`97` CGs), Music Box (`15` tracks), and Staff Notes across all 3 endings.

### 3. Shared Kotonoha UI
- **Pause menu (`InGameMenu`)**: the runtime uses the engine's shared menu, rather than the removed `N2GameMenu`. Settings are delegated to the `Engine`; the current return-to-menu callback requests application exit.
- **Backlog (`VNBacklog`)**: `B` toggles the VM's backlog. While a modal scene is open, input is routed to the capturing scene.
- **Quick save/load**: `F5` writes `n2system_quick.n2sv`; `F6` loads it. There is currently no runtime-owned `VNSaveMenu` save browser.
- **Steins;Gate text**: the original TTF font is loaded from provider bytes into `Kotonoha::UI`, using renderer-backed cached text instead of per-frame surfaces.
- **Movies**: looping background movies, including the title, cannot be dismissed through generic advance/skip input and do not capture native choice navigation. Non-looping narrative movies still require deliberate skip gestures.

**Validation remaining:** retail assets and target platforms still require end-to-end visual, audio/video, gameplay, save/load, and text-rendering validation. Synthetic tests do not certify complete parity with the original N2System.
