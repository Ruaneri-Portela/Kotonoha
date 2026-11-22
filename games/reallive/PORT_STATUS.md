# Port status

## Implemented and exercised

- ISO9660/Joliet resources are read directly with `DiscImageAssetProvider`,
  without extracting files or creating a temporary game directory. Seen.txt
  and loose SEEN overrides are parsed from owned memory; voice lookup uses
  logical provider paths. Folder data and overlays retain the same identifiers.
  `AirEngineTests /path/to/REALLIVE.iso --direct-iso` checks script overrides,
  original voice/image decoding and disabling the overlay on a direct ISO.
- Original boot scenarios 9030/9031 reach original title 9032.
- Original G00 title graphics and script-controlled button positions, patterns,
  mouse hit testing and alpha/frame-counter animation.
- All 1,229 G00 and both PDT images decode; 96 script scenarios parse
  (1,386,166 bytecode elements).
- RealLive expressions, packed integer banks, strings, conditionals, branches,
  gosub/return and cross-scenario calls; exact interpreter snapshots.
- rlBabel text buffer, encoded memory references, character extraction, names
  and CP1252 mapping. Native DLL was decompiled with REA/Ghidra to corroborate
  dispatch and Western encoding, including the initial-space quirk.
- Original Portuguese story text, SMW00A/B dialogue frame and PDT advance icon.
  Installed Verdana 25 is used on macOS; otherwise engine font fallback.
- Original NWA PCM music, 24,900 indexed Ogg voices, loose WAV playback and
  movie playback plumbing. All 27 music payloads are preserved by conversion;
  full synchronized movie playback has not been compared against RealLive.
- DC image loads/copies/fills, pending background objects, sprite regions,
  clipping, colour rectangles, basic image filters and configured shake steps.
- Save-name suggestions follow the original script title command (1:4:0),
  including dates such as `17 de Julho (Seg)`. The shared save menu pre-fills
  an editable name, adds suffixes for duplicates, and preserves the title in
  saved presentation state; older saves recover it from their scenario.
- Kotonoha pause/backlog, dialogue save/load, script position, call stack,
  memories, rlBabel state, DC pixels, object state and BGM resume position.

- Native refresh (1:31:0) now yields at a complete script frame, preventing the
  VM instruction budget from presenting intermediate title/button states;
  regression verifies the title refresh at 9032:859.

- Return to the title clears scene objects, DC buffers, transitions, edit
  controls and active scene effects before starting SEEN9032. Native menu_save
  and menu_load (3000/3001) open the shared save menu and suspend the script;
  regression covers 9032:911 and closing the load menu.
- Original REALLIVE Standard Edition title startup executes its edition-specific
  setup calls and reaches the scripted title sequence. The 8005 compatibility
  regression enters through its real caller sequence at 170:580, preserves the
  `gosub` context, verifies both `1:4:111` timer waits, and exercises the
  system-command visibility/skip options, index series, BGM seek, group zoom,
  G00 pattern loop, frame loop, and rectangular monochrome commands through
  targeted opcode checks. It returns to the caller without a pending opcode.
  This does not establish parity for unvisited branches or audiovisual timing
  against the native executable. Run it with
  `AirEngineTests /path/to/REALLIVE --8005-compat`.
  Long-form `grpOpenBg` transitions now honor explicit duration, style and
  opacity. The `?` form reuses DC1, and foreground-to-background object copies
  support inclusive ranges. These fixes restore the full opening background at
  8005:344 and 8005:490; `Air8005VisualCompatibility` captures both points and
  checks that the image is not black or limited to a narrow strip. Styles 4 and
  5 use orthogonal and diagonal ordered-dither fades, respectively, matching
  the dotted REALLIVE opening transition shown in the supplied reference. Other
  transition styles still need native comparison.
  `objLoop` (1:73/74:2001) cycles through G00 patterns at the script's frame
  interval; OP_D00 and OP_D01 each contain 12 patterns, advanced every 164 ms
  for the walking legs and shadow. `Air8005Walking` verifies frame changes,
  looping, pause/resume and save restoration. Presentation saves now retain
  animation timing; versions 1 and 2 still load as static objects.
  Position-adjustment slots belong to each foreground/background object,
  rather than shared VM memory. Initializing a reused object clears its old
  offsets, preventing earlier pans from shifting later 8005 sprites (including
  the walking legs). Copies and promotions retain their own adjustment slots.
  Save versions 4 and 5 store these slots; older saves import their legacy offsets.
  Script-generated REALLIVE scenes use global upscaling: DC copies, sprites,
  clipping, opacity, zoom, flashes and transitions compose at native 640x480,
  then the completed scene enters the upscaler once. This includes the KEY
  logo after the engine splash and the REALLIVE title/menu. Sprite patterns and
  offscreen DC operations do not receive a second filter. Native pixels and
  textures remain available for disabling upscaling and for script reads.
  The filtered GPU scene is cached by background identity, visible object
  state, animation pattern, effect progress and filter configuration.
  Unchanged redraws do not compose, read back, upload or filter again; each
  changed animation/effect frame gets one global filter pass. Engine dialogue
  UI, subtitles and separate video playback retain their own presentation paths.
  Rasterized script text is excluded from scene composition and its cache key,
  then drawn as an unfiltered overlay with native position, clip and opacity.
  `--script-text-upscaling` verifies pixel-identical glyphs with Anime4K on/off
  and confirms that repeated text redraws do not refilter the scene.
  Save version 5 preserves the native operation graph; older saves still load
  their native DC pixels. Dither transitions
  use a nearest-sampled 8x8 GPU mask; renderers lacking the required custom
  blend mode log a warning and use the native pixel-composition fallback.
  Transition snapshots and fallback effects compose natively before the
  global filter. Video retains the shared media player's
  decoded-frame serial, so repeated draws reuse the filtered frame.
  `Air8005Walking` compares independently composed native scenes filtered at 2x and 1.5x,
  then verifies that disabling upscaling restores native pixels without new
  uploads. Unchanged redraws reuse the globally filtered frame; animation
  changes trigger one new scene filter without sprite uploads.
  GPU dither cells are compared pixel-for-pixel at
  1x, 2x and 4x, including both transition endpoints.
  `--8005-walk-opengl` runs the same pixel comparison with real Anime4K shaders.
  `AirEngineTests /path/to/REALLIVE --texture-performance` also measures 120 draws
  at 1x, 2x and 4x after texture preparation, asserting no sprite filtering/uploads
  and one global pass per changed frame. This measures scene rendering, not complete
  gameplay FPS, loading time or shader-preparation cost.
  `Air8005UpscalingCompatibility` exercises both reported opening transitions
  with 2x upscaling enabled.
  `Air8005BackgroundUpscaling` independently composes the native OP_A01 crop
  and OP_A03 overlay, then filters the completed raster frame, verifies cache
  reuse, save/load and disabling the filter, compares the OP_A04/OP_A05 opening
  logos against independently composed and filtered scenes, and exercises the late OP_K00
  strip sequence without filtering intermediate copies. Separate counters
  verify pattern filtering and completed script-frame filtering.
  `--8005-background-upscaling-opengl` runs this check with real Anime4K shaders.
  `--boot-upscaling` checks the REALLIVE title image (`S_TT_BG00`) and clipped
  KEY logo (`KEYLOGO0`) against independently composed full scenes filtered with
  Anime4K, starting with filtering enabled before the first image load.
  It also runs the original 9030/9031 boot to the 9032 title menu, verifies
  that disabling filtering visibly changes the logos/menu, and checks that
  unchanged logo/menu redraws perform no additional filtering or uploads.
  REALLIVE zoom and Amagami position/scale/rotation/opacity tweens use the engine's
  `TransformAnimation` class. It supports absolute clock sampling (REALLIVE
  milliseconds) and explicit deltas (Amagami script frames), preserving each
  port's pause policy and native easing curves without owning texture data.
  `--8005-all-calls` steps through all 1,065 command call sites individually;
  the supplied `/Volumes/REALLIVE_SE` data blocks one call at `8005:2739` because
  the referenced `D_DASH15` graphic is absent. The audit must pass with a
  complete asset set before claiming full-call compatibility.
- Forced ASS subtitles prefer a full `scenario-position.ass` file and fall back
  to `scenario.ass`. Event-specific files use a clock that resets on event
  changes; scenario-only files use a node clock that spans event positions.
  Both clocks pause with the engine's story pause and appear together in the
  debug UI. ASS movement tags such as `\move` are rendered by libass inside
  the engine-owned forced-subtitle component.
- Dialogue fonts rasterize at physical output density, including the native
  640x480 scene transform, with fixed full-text line breaks during reveal.
  The shared UI also accounts for scene scale when rasterizing fonts.

## Shared engine behavior verified during the fidelity review

- Global opacity on native `recCopy` (1:33:1100) blends source RGB with the
  existing DC even when the per-pixel mask is disabled. The shared
  `BlitSurfaceOpacity` distinguishes these two alpha sources. Unblended buffer
  copies retain the source mask for later `maskCopy` calls; forcing their alpha
  to 255 made SEEN8613 text fade into black strips. Source state and aliased
  surfaces are handled without destroying the mask. Pixel regressions
  cover transparent, partial, opaque and masked copies. This fixes the intro's
  intermediate black text strips without hiding frames.
- Native script input receives a held pointer from finger-down through release.
  Shared buttons confirm touch on release; a second finger or navigation swipe
  cancels the underlying pointer. This prevents the first finger selecting a
  pause-menu item before the two-finger Back gesture is recognized.
- SMW00C controls use original atlas regions for hide/history/forward/auto/skip.
  Auto waits for text and voice; the native character/wait settings control its
  delay. Skip stops on unread pages; read tracking is now journaled per game and merges with presentation saves v7.

## Incomplete — do not claim 100% parity

The complete game and all endings have not been played through. Reading every
script and asset does not establish equivalent behavior. Unknown commands stop
execution with their scenario, instruction and opcode rather than silently
changing the story.

Still required:

- Exact native edit-control typography and remaining glossary presentation details.
  Name-entry commands 1700/1701/1703/1710/1711 now create, edit, focus and read the original scenario 9037 fields; regression covers 9037:118, dark foreground/white background rendering and field clipping.
  Opcode 1700 uses arguments 6–8 for text RGB and 9–11 for background RGB.
- Original save/config/gallery menus and CG/gallery unlock persistence. Dialogue read history now persists across sessions per RealLive game, independently of slots; Kotonoha presentation saves are still separate from original RealLive save files.
- Remaining composite graphics commands and particle/drift effects.
- Remaining visual parity: directional wipes (style 10), blinds (120),
  scroll/squash/slide styles 15–18 and 20–21 now have native-resolution
  implementations and synthetic geometry tests. Exact timing and interpolation
  still require comparison with the original RealLive executable; unknown SEL
  styles continue to crossfade. Font
  metrics, name-box decoration, text pacing,
  selection styling and some window controls need native comparison.
- Remaining channel/voice volume behavior and complete media timing verification. Timed BGM fades and cooperative voice/PCM playback waits are implemented with synthetic regressions.
- Native reference captures, route-by-route input replays, choice/ending checks,
  audiovisual timing comparison and save/reload equivalence beyond the tested
  dialogue point.

The Windows executable is never loaded by the port. REA evidence is retained
locally in `.research/reallive/native`, with resource findings and opcode inventory
in `.research/reallive/graphics`, `audio` and `script`.

## Reproducible native evidence

Reference `rlBabel.dll` SHA-256:
`13a874a8c056d919b80a517e74b9c03bf96ed006e7cfeb2405319e71d87d15dc`.
REA 6.7.0 with Ghidra 12.1.4 decompiled export `reallive_dll_func_call`
(0x100112d0), dispatchers 0x10010c20/0x10015120, encoding factory
0x10013f00 and CP1252 decoder 0x1000e0c0. This verifies static contracts,
not timing or observable equivalence with the running Windows application.

## Resource and opcode review (2026-11-03)

REALLIVE now extends `Kotonoha::IndexedFileSystemAssetProvider`: indexing,
case-insensitive path lookup, Windows separator normalization, bounded reads,
streaming paths and overlays use the engine implementation. The port retains
only its G00/PDT/NWA decoders and RealLive graphic-name conventions.

Run `AirInspect Seen.txt --opcodes` for a CSV inventory with counts and the
first scenario/instruction. The supplied edition contains 184 command signatures
and 669,818 command occurrences. Counts establish presence, not runtime coverage.

All 184 opcode signatures across all 96 scenarios (1,386,166 bytecode elements) are now handled by the interpreter and runtime dispatch (`FOUND 0 PENDING / UNHANDLED OPCODES`). `1:12:3300/1` (syscom menu configuration) and particle/drift object opcodes (`1:71:1300/0` and `1:81:1031/0`) are fully implemented and integrated.

In addition, referencing `eglaysher/rlvm`, RealLive engine support has been enhanced across multiple modules:
- **Audio System (Modules 20, 21, 22, 23)**:
  - BGM fade-in (`1:20:1`, `1:20:3`), status query (`1:20:8`), track name query (`1:20:11`), pause/resume (`1:20:12`, `1:20:13`), stop (`1:20:14`), and target volume fades (`1:20:101-103`).
  - PCM sound channel stop (`1:21:1`), play query (`1:21:8`, `1:21:11`), and volume getters/setters (`1:21:9`, `1:21:10`).
  - Mapped SE stop (`1:22:1`), loop play (`1:22:2`), and play state query (`1:22:8`, `1:22:11`).
  - Voice/KOE stop (`1:23:1`), play state query (`1:23:8`, `1:23:11`), volume control (`1:23:9`, `1:23:10`), and wait (`1:23:101`).
- **System & Mouse Queries (Module 4)**:
  - Mouse button click states (`1:4:131`, `1:4:132`), hover state (`1:4:134`), mouse coordinates (`1:4:200`, `1:4:201`, `1:4:202`), cursor visibility/ID/region queries & setters (`1:4:210..219`), extended mouse & touch input queries (`1:4:220..249`), fast-forward skip queries/triggers (`1:4:330`, `1:4:331`), auto-mode status queries/setters (`1:4:360..369`), and message/voice skip options (`1:4:370..389`).
- **Object Properties & Queries (Modules 60, 61, 62, 81, 82, 84, 85, 86)**:
  - Extended object queries for adjustment slots, alpha, visibility, pattern index, position, and RGBA color across foreground (`1:84`) and background (`1:85`, `1:86`) objects.
  - Object property setters for offsets (`1:81:1001`) and source clipping rectangles (`1:81:1040`).
  - Object swap (`1:60:10`) and object reset/clear (`1:61:0`, `1:61:1`).
- **String Operations (Module 10)**:
  - Added string contains search (`1:10:32`).

- **Dialogue & Selection Flow (CLANNAD & Planetarian)**:
  - Dialogue page and line wait opcodes (`0:3:0`, `0:3:1`, `0:3:2`, `0:3:4`, `0:3:16`, `0:3:200`) and offscreen concealed waits (`0:3:205`) are integrated into the text pause handler, preventing script execution halts during story progression.
  - Choice selection variants (`0:2:0..4`), intra-DC blit/copy operations (`1:33:100/101/1100/1101`), and window management commands (`1:40`) are fully handled.

Use `AirInspect Seen.txt --find '1:20:1002'` to inspect each actual call.
`1:20:1002/0` is implemented as a BGM seek, but its offset/timing semantics
still need comparison with a native reference.

