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

## Compatibility Improvements (November 2026)

- Added subroutine argument passing for `gosub`, `defsub`, and `getparam`, including round-trip serialization of pending arguments.
- Added proportional text interpolation for `{$variable}`, `{%variable}`, and `{?array[index]}`; interpolated text is kept literal.
- Fixed out-of-range `for` loops (including nested loops) and strengthened numeric variable-reference validation.
- Added zero-based `tablegoto`/`debugtablegoto`, one-based `tablegoto1`, Unicode-safe `len` and `mid`, and `movz` array zero-filling without altering the distinct behavior of `movl`.
- Differentiated `select` (jump), `selgosub` (call/return), and `selnum` (zero-based choice index); pending choices preserve their mode and destination through save/load, and older V1 slots remain readable.
- Added standalone C++17 regressions for the VM, lexer, tagged image/archive support, and menu/flow behavior without proprietary game data.

### Current Script, Menu and Graphics Coverage

- Explicit multi-line continuation is assembled before statement indexing for `select`, `selgosub`, `selnum` and `csel`.
- `csel` invokes `*customsel`, exposes `getcselnum`/`getcselstr`, supports text-only `cselbtn` hit boxes through the GPU-backed UI and `cselgoto`, and restores the pending selection and custom buttons from saves.
- `getparam i%N` and `s%N` preserve the original caller variable identity (`CALLREF`), even across save/load. Subroutine returns restore the caller scene label (`CALLFROM`).
- Definition-level `pretextgosub` and `textgosub` callbacks are supported with a recursion guard and page clearing via `texec`; interrupted callbacks and effect progress are serialized.
- The graphics group has explicit handlers for standing opacity (`tal`), string sprites (`strsp`/`strsph`), independent `lsp2` visibility, additive/subtractive texture modes, retained `draw*`/`blt` framebuffer commands, `bgcopy`/`bgcpy` scene-layer snapshots, `spstr` sprite/sound controls, directional `quake`, `monocro`, and `nega`.
- Added C++17 regressions for every group above, a Linux/macOS VM test matrix, Linux GCC/Clang full runtime syntax checks, and a dedicated native CMake game/runtime smoke-build workflow.

### Integration and Fidelity Limitations

- The game data for commercial scenarios is not included. A passing standalone VM test and native synthetic runtime test cannot establish universal game compatibility; gameplay routes and edge-case script syntax still need asset-backed verification.
- `bgcopy` captures independently retained rendered layers rather than copying the physical output surface byte-for-byte; some ephemeral overlays may not be represented. Affine and blending behavior is implemented through SDL3 and should be visually compared against the reference interpreter.
- `monocro` and `nega` use an optional SDL3 render-readback fallback when enabled (there is no custom fragment shader path for this renderer); this can be costly on high-resolution scenes and must be performance-tested on device.
- Mid-line text click markers and platform-specific operations beyond the explicitly implemented graphics command group can still differ from the original. Native iOS/Android/WebAssembly builds and retail visual comparisons have not been certified by these workflows.
- The implementation is clean-room, not a 1:1 code transfer from the GPL upstream project.
