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
- Kotonoha pause/backlog, dialogue save/load, script position, call stack,
  memories, rlBabel state, DC pixels, object state and BGM resume position.

## Incomplete — do not claim 100% parity

The complete game and all endings have not been played through. Reading every
script and asset does not establish equivalent behavior. Unknown commands stop
execution with their scenario, instruction and opcode rather than silently
changing the story.

Still required:

- Exact native edit-control typography and remaining glossary presentation details.
  Name-entry commands 1700/1701/1703/1710/1711 now create, edit, focus and read the original scenario 9037 fields; regression covers 9037:118.
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
