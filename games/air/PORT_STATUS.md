# Port status

## Implemented and exercised

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
  delay. Skip stops on unread pages; read tracking currently lasts one session.

## Incomplete — do not claim 100% parity

The complete game and all endings have not been played through. Reading every
script and asset does not establish equivalent behavior. Unknown commands stop
execution with their scenario, instruction and opcode rather than silently
changing the story.

Still required:

- Exact native edit-control typography and remaining glossary presentation details.
  Name-entry commands 1700/1701/1703/1710/1711 now create, edit, focus and read the original scenario 9037 fields; regression covers 9037:118, dark foreground/white background rendering and field clipping.
  Opcode 1700 uses arguments 6–8 for text RGB and 9–11 for background RGB.
- Original save/config/gallery menus and CG/read/progression persistence beyond
  the current session. Kotonoha dialogue saves are separate from RealLive saves.
- Remaining composite graphics commands and particle/drift effects.
- Exact transition styles: current transitions use a crossfade, including styles
  whose native effect differs. Font metrics, name-box decoration, text pacing,
  selection styling and some window controls need native comparison.
- Timed audio fades, some channel/volume commands, and complete media testing.
- Native reference captures, route-by-route input replays, choice/ending checks,
  audiovisual timing comparison and save/reload equivalence beyond the tested
  dialogue point.

The Windows executable is never loaded by the port. REA evidence is retained
locally in `.research/air/native`, with resource findings and opcode inventory
in `.research/air/graphics`, `audio` and `script`.

## Reproducible native evidence

Reference `rlBabel.dll` SHA-256:
`13a874a8c056d919b80a517e74b9c03bf96ed006e7cfeb2405319e71d87d15dc`.
REA 6.7.0 with Ghidra 12.1.4 decompiled export `reallive_dll_func_call`
(0x100112d0), dispatchers 0x10010c20/0x10015120, encoding factory
0x10013f00 and CP1252 decoder 0x1000e0c0. This verifies static contracts,
not timing or observable equivalence with the running Windows application.

## Resource and opcode review (2026-11-03)

AIR now extends `Kotonoha::IndexedFileSystemAssetProvider`: indexing,
case-insensitive path lookup, Windows separator normalization, bounded reads,
streaming paths and overlays use the engine implementation. The port retains
only its G00/PDT/NWA decoders and RealLive graphic-name conventions.

Run `AirInspect Seen.txt --opcodes` for a CSV inventory with counts and the
first scenario/instruction. The supplied edition contains 184 command signatures
and 669,818 command occurrences. Counts establish presence, not runtime coverage.
Confirmed commands still missing from the presentation dispatch include:

| Signature | Occurrences | First location | Remaining work |
|---|---:|---|---|
| `1:20:1002/0` | 11 | `8994:194` | Opening music command; recover native timing/offset semantics |
| `1:71:1300/0` | 4 | `8996:77` | Drift/particle object creation |
| `1:81:1031/0` | 4 | `8996:101` | Drift animation parameters |

Use `AirInspect Seen.txt --find '1:20:1002'` to inspect each actual call.
These commands continue to stop with an explicit diagnostic; their behavior is
not approximated by silently consuming them.
